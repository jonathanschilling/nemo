/* -*- Mode: C; indent-tabs-mode: f; c-basic-offset: 4; tab-width: 4 -*- */

/*
 *  Nemo
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 */

#include "nemo-preview-pane.h"
#include "nemo-window.h"
#include <glib/gi18n.h>

#define DEBUG_FLAG NEMO_DEBUG_WINDOW
#include <libnemo-private/nemo-debug.h>
#include <libnemo-private/nemo-icon-info.h>
#include <libnemo-private/nemo-thumbnails.h>

/* PDF rendering support */
#include <cairo.h>
#include <cairo-pdf.h>

/* Check for config.h HAVE_POPPLER macro */
#include "config.h"
#ifdef HAVE_POPPLER
#include <poppler.h>
#endif

typedef enum {
    PREVIEW_TYPE_NONE,
    PREVIEW_TYPE_TEXT,
    PREVIEW_TYPE_IMAGE,
    PREVIEW_TYPE_VIDEO,
    PREVIEW_TYPE_PDF,
    PREVIEW_TYPE_UNSUPPORTED
} PreviewType;

struct _NemoPreviewPanePrivate {
    NemoWindow *window;
    
    /* UI Components */
    GtkWidget *content_box;
    GtkWidget *no_selection_label;
    GtkWidget *preview_content_widget;
    GtkWidget *loading_label;
    GtkWidget *error_label;
    GtkWidget *metadata_box;
    GtkWidget *filename_label;
    GtkWidget *filesize_label;
    GtkWidget *filetype_label;
    GtkWidget *modified_label;
    
    /* Current state */
    NemoFile *current_file;
    PreviewType current_preview_type;
    gulong selection_changed_id;
    
    /* Resize tracking */
    int last_preview_width;
    
    /* Async rendering state */
    GCancellable *async_render_cancellable;
    gboolean rendering_in_progress;
    int target_render_width;
    guint async_render_sequence;
    
    /* Resize debouncing */
    guint resize_timeout_id;
    int pending_resize_width;
    gboolean resize_in_progress;
    
    /* Ultra-lightweight resize handling */
    guint immediate_feedback_timeout_id;  /* For deferred visual updates */
    gboolean needs_visual_update;         /* Flag for pending visual updates */
    int pending_visual_width;             /* Target width for visual update */
};

G_DEFINE_TYPE_WITH_PRIVATE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_SCROLLED_WINDOW)

/* Forward declarations */
static gboolean validate_preview_state (NemoPreviewPane *preview_pane);
static gboolean ensure_preview_visible (NemoPreviewPane *preview_pane);
static void show_no_selection_state (NemoPreviewPane *preview_pane);
static GtkWidget *create_image_preview (const char *file_path, int available_width);
static GtkWidget *create_pdf_preview_direct (const char *file_path, int target_width);
static gboolean on_resize_timeout (gpointer user_data);
static void cancel_resize_timeout (NemoPreviewPane *preview_pane);
static void start_async_render (NemoPreviewPane *preview_pane, int target_width);
static gboolean apply_immediate_visual_feedback (gpointer user_data);
static void cancel_immediate_feedback_timeout (NemoPreviewPane *preview_pane);

/* File type detection functions */
static gboolean
is_text_file (const char *mime_type)
{
    if (!mime_type) {
        return FALSE;
    }
    
    return g_str_has_prefix (mime_type, "text/") ||
           g_strcmp0 (mime_type, "application/json") == 0 ||
           g_strcmp0 (mime_type, "application/javascript") == 0 ||
           g_strcmp0 (mime_type, "application/xml") == 0 ||
           g_strcmp0 (mime_type, "application/x-shellscript") == 0;
}

static gboolean
is_image_file (const char *mime_type)
{
    if (!mime_type) {
        return FALSE;
    }
    
    return g_str_has_prefix (mime_type, "image/");
}

static gboolean
is_video_file (const char *mime_type)
{
    if (!mime_type) {
        return FALSE;
    }
    
    return g_str_has_prefix (mime_type, "video/");
}

static gboolean
is_pdf_file (const char *mime_type)
{
    if (!mime_type) {
        return FALSE;
    }
    
    return g_strcmp0 (mime_type, "application/pdf") == 0;
}

static PreviewType
detect_preview_type (NemoFile *file)
{
    char *mime_type;
    char *file_name;
    PreviewType type = PREVIEW_TYPE_UNSUPPORTED;
    
    if (!file) {
        DEBUG ("detect_preview_type: No file provided");
        return PREVIEW_TYPE_NONE;
    }
    
    file_name = nemo_file_get_display_name (file);
    mime_type = nemo_file_get_mime_type (file);
    
    DEBUG ("detect_preview_type: File '%s' has MIME type '%s'", 
           file_name ? file_name : "unknown", 
           mime_type ? mime_type : "unknown");
    
    if (!mime_type) {
        DEBUG ("detect_preview_type: No MIME type available");
        g_free (file_name);
        return PREVIEW_TYPE_UNSUPPORTED;
    }
    
    if (is_text_file (mime_type)) {
        type = PREVIEW_TYPE_TEXT;
        DEBUG ("detect_preview_type: Detected as TEXT file");
    } else if (is_image_file (mime_type)) {
        type = PREVIEW_TYPE_IMAGE;
        DEBUG ("detect_preview_type: Detected as IMAGE file");
    } else if (is_video_file (mime_type)) {
        type = PREVIEW_TYPE_VIDEO;
        DEBUG ("detect_preview_type: Detected as VIDEO file");
    } else if (is_pdf_file (mime_type)) {
        type = PREVIEW_TYPE_PDF;
        DEBUG ("detect_preview_type: Detected as PDF file");
    } else {
        DEBUG ("detect_preview_type: Unsupported file type: %s", mime_type);
    }
    
    g_free (mime_type);
    g_free (file_name);
    return type;
}

/* Preview content creation functions */
static GtkWidget *create_image_preview (const char *file_path, int available_width);

static GtkWidget *
create_text_preview (const char *file_path)
{
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    GError *error = NULL;
    char *contents = NULL;
    gsize length;
    
    DEBUG ("create_text_preview: Attempting to preview text file: %s", file_path ? file_path : "NULL");
    
    if (!file_path) {
        DEBUG ("create_text_preview: NULL file path provided");
        return NULL;
    }
    
    if (!g_file_get_contents (file_path, &contents, &length, &error)) {
        DEBUG ("create_text_preview: Failed to read file '%s': %s", 
               file_path, error ? error->message : "unknown error");
        g_clear_error (&error);
        return NULL;
    }
    
    DEBUG ("create_text_preview: Successfully read %zu bytes from file", length);
    
    /* Limit text preview to reasonable size (64KB) */
    if (length > 65536) {
        contents = g_realloc (contents, 65537);
        contents[65536] = '\0';
        length = 65536;
        DEBUG ("create_text_preview: Truncated file to 64KB");
    }
    
    text_view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (text_view), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (text_view), FALSE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);
    
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
    gtk_text_buffer_set_text (buffer, contents, -1);
    
    DEBUG ("create_text_preview: Created text view widget successfully");
    
    g_free (contents);
    return text_view;
}

/* Enhanced image preview using thumbnails when available - MAXIMUM SIZE */
static GtkWidget *
create_thumbnail_image_preview (NemoFile *file, int available_width)
{
    GtkWidget *image = NULL;
    GdkPixbuf *pixbuf = NULL;
    NemoIconInfo *icon_info = NULL;
    int target_width;
    int icon_size;
    
    DEBUG ("create_thumbnail_image_preview: Creating FULL-SIZE thumbnail for file (available_width: %d)", available_width);
    
    if (!file) {
        DEBUG ("create_thumbnail_image_preview: NULL file provided");
        return NULL;
    }
    
    /* Use maximum available width with minimal margin */
    target_width = MAX(200, available_width - 20);  /* Minimal 10px margin on each side */
    
    /* Request largest possible thumbnail - remove artificial limits */
    icon_size = target_width;
    
    /* Allow very large thumbnails for readability */
    icon_size = MIN(icon_size, 2048);  /* Only limit to prevent memory issues */
    
    DEBUG ("create_thumbnail_image_preview: Requesting LARGE thumbnail of size %d for maximum readability", icon_size);
    
    /* Try to get thumbnail using Nemo's thumbnail system - request maximum quality */
    icon_info = nemo_file_get_icon (file, 
                                   icon_size, 
                                   target_width,  /* Use full target width */
                                   1,             /* scale */
                                   NEMO_FILE_ICON_FLAGS_USE_THUMBNAILS | 
                                   NEMO_FILE_ICON_FLAGS_FORCE_THUMBNAIL_SIZE);
    
    if (icon_info) {
        pixbuf = nemo_icon_info_get_pixbuf (icon_info);
        
        if (pixbuf) {
            int width = gdk_pixbuf_get_width (pixbuf);
            int height = gdk_pixbuf_get_height (pixbuf);
            
            DEBUG ("create_thumbnail_image_preview: Got thumbnail %dx%d", width, height);
            
            /* Create image widget */
            image = gtk_image_new_from_pixbuf (pixbuf);
            
            /* Don't unref pixbuf - it's owned by icon_info */
        } else {
            DEBUG ("create_thumbnail_image_preview: No pixbuf from icon_info");
        }
        
        nemo_icon_info_unref (icon_info);
    } else {
        DEBUG ("create_thumbnail_image_preview: Failed to get icon_info");
    }
    
    /* If thumbnail failed, check if we can create one */
    if (!image && nemo_can_thumbnail (file)) {
        DEBUG ("create_thumbnail_image_preview: File can be thumbnailed, requesting thumbnail creation");
        nemo_create_thumbnail (file);
        
        /* For now, fall back to direct image loading while thumbnail is being created */
        char *file_path = nemo_file_get_path (file);
        if (file_path) {
            image = create_image_preview (file_path, available_width);
            g_free (file_path);
        }
    }
    
    DEBUG ("create_thumbnail_image_preview: Returning image widget: %p", image);
    return image;
}

static GtkWidget *
create_image_preview (const char *file_path, int available_width)
{
    GtkWidget *image;
    GdkPixbuf *pixbuf;
    GdkPixbuf *scaled_pixbuf;
    GError *error = NULL;
    int width, height;
    int target_width;
    
    DEBUG ("create_image_preview: Creating FULL-SIZE image preview: %s (available_width: %d)", 
           file_path ? file_path : "NULL", available_width);
    
    if (!file_path) {
        DEBUG ("create_image_preview: NULL file path provided");
        return NULL;
    }
    
    /* Use maximum available width with minimal margin */
    target_width = MAX(200, available_width - 20);  /* Minimal 10px margin on each side */
    
    DEBUG ("create_image_preview: Using FULL target width %d for maximum readability", target_width);
    
    pixbuf = gdk_pixbuf_new_from_file (file_path, &error);
    if (!pixbuf) {
        DEBUG ("create_image_preview: Failed to load image '%s': %s", 
               file_path, error ? error->message : "unknown error");
        g_clear_error (&error);
        return NULL;
    }
    
    width = gdk_pixbuf_get_width (pixbuf);
    height = gdk_pixbuf_get_height (pixbuf);
    
    DEBUG ("create_image_preview: Loaded image %dx%d", width, height);
    
    /* Scale image ONLY if wider than target, preserve aspect ratio, NO HEIGHT LIMIT */
    if (width > target_width) {
        double scale = (double)target_width / width;
        int new_width = target_width;
        int new_height = (int)(height * scale);
        
        DEBUG ("create_image_preview: Scaling ONLY width-wise to %dx%d (scale: %.2f) - HEIGHT UNLIMITED", 
               new_width, new_height, scale);
        
        scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf, new_width, new_height, GDK_INTERP_BILINEAR);
        g_object_unref (pixbuf);
        pixbuf = scaled_pixbuf;
    } else {
        DEBUG ("create_image_preview: Image fits horizontally (%dx%d) - no scaling needed", width, height);
    }
    
    image = gtk_image_new_from_pixbuf (pixbuf);
    g_object_unref (pixbuf);
    
    DEBUG ("create_image_preview: Created image widget successfully");
    
    return image;
}

/* Direct PDF preview renderer - PIXEL-PERFECT at exact target resolution */
static GtkWidget *
create_pdf_preview_direct (const char *file_path, int target_width)
{
#ifdef HAVE_POPPLER
    GtkWidget *image = NULL;
    PopplerDocument *document = NULL;
    PopplerPage *page = NULL;
    GdkPixbuf *pixbuf = NULL;
    cairo_surface_t *surface = NULL;
    cairo_t *cr = NULL;
    GError *error = NULL;
    double page_width, page_height;
    double scale_factor;
    int render_width, render_height;
    char *uri;
    
    DEBUG ("create_pdf_preview_direct: Rendering PDF at EXACT target width %d for pixel-perfect quality", target_width);
    
    if (!file_path) {
        DEBUG ("create_pdf_preview_direct: NULL file path provided");
        return NULL;
    }
    
    /* Convert file path to URI for Poppler */
    uri = g_filename_to_uri (file_path, NULL, &error);
    if (!uri) {
        DEBUG ("create_pdf_preview_direct: Failed to convert path to URI: %s", 
               error ? error->message : "unknown error");
        g_clear_error (&error);
        return NULL;
    }
    
    /* Load PDF document */
    document = poppler_document_new_from_file (uri, NULL, &error);
    g_free (uri);
    
    if (!document) {
        DEBUG ("create_pdf_preview_direct: Failed to load PDF document: %s", 
               error ? error->message : "unknown error");
        g_clear_error (&error);
        return NULL;
    }
    
    /* Get first page */
    page = poppler_document_get_page (document, 0);
    if (!page) {
        DEBUG ("create_pdf_preview_direct: Failed to get first page");
        g_object_unref (document);
        return NULL;
    }
    
    /* Get page dimensions */
    poppler_page_get_size (page, &page_width, &page_height);
    DEBUG ("create_pdf_preview_direct: PDF page size: %.2fx%.2f points", page_width, page_height);
    
    /* Calculate scale factor to fit target width EXACTLY */
    scale_factor = (double)target_width / page_width;
    render_width = target_width;
    render_height = (int)(page_height * scale_factor);
    
    DEBUG ("create_pdf_preview_direct: Rendering at EXACT resolution %dx%d (scale=%.4f) - NO RESCALING", 
           render_width, render_height, scale_factor);
    
    /* Create Cairo surface at EXACT target resolution */
    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, render_width, render_height);
    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
        DEBUG ("create_pdf_preview_direct: Failed to create Cairo surface");
        g_object_unref (page);
        g_object_unref (document);
        return NULL;
    }
    
    /* Create Cairo context */
    cr = cairo_create (surface);
    if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
        DEBUG ("create_pdf_preview_direct: Failed to create Cairo context");
        cairo_surface_destroy (surface);
        g_object_unref (page);
        g_object_unref (document);
        return NULL;
    }
    
    /* Fill with white background */
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);
    
    /* Scale to exact target resolution */
    cairo_scale (cr, scale_factor, scale_factor);
    
    /* Render PDF page at exact target resolution */
    poppler_page_render (page, cr);
    
    /* Convert Cairo surface to GdkPixbuf */
    pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, render_width, render_height);
    
    /* Cleanup */
    cairo_destroy (cr);
    cairo_surface_destroy (surface);
    g_object_unref (page);
    g_object_unref (document);
    
    if (pixbuf) {
        DEBUG ("create_pdf_preview_direct: Successfully rendered PDF at %dx%d - PIXEL PERFECT", 
               gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf));
        
        /* Create image widget from pixbuf - NO SCALING */
        image = gtk_image_new_from_pixbuf (pixbuf);
        g_object_unref (pixbuf);
    } else {
        DEBUG ("create_pdf_preview_direct: Failed to create pixbuf from Cairo surface");
    }
    
    return image;
    
#else
    DEBUG ("create_pdf_preview_direct: Poppler support not available, falling back to thumbnail");
    return NULL;
#endif
}

/* Helper function to get MAXIMUM available width for preview content */
static int
get_available_preview_width (NemoPreviewPane *preview_pane)
{
    GtkAllocation allocation;
    int available_width = 300; /* fallback default */
    int scrollbar_width = 0;
    
    if (gtk_widget_get_realized (GTK_WIDGET (preview_pane))) {
        gtk_widget_get_allocation (GTK_WIDGET (preview_pane), &allocation);
        available_width = allocation.width;
        
        /* Account for vertical scrollbar when it's visible */
        GtkScrolledWindow *scrolled = GTK_SCROLLED_WINDOW (preview_pane);
        GtkWidget *vscrollbar = gtk_scrolled_window_get_vscrollbar (scrolled);
        if (vscrollbar && gtk_widget_get_visible (vscrollbar)) {
            GtkAllocation scrollbar_allocation;
            gtk_widget_get_allocation (vscrollbar, &scrollbar_allocation);
            scrollbar_width = scrollbar_allocation.width;
        }
        
        available_width -= scrollbar_width;
        DEBUG ("get_available_preview_width: Pane width %d, scrollbar width %d, available %d", 
               allocation.width, scrollbar_width, available_width);
    } else {
        DEBUG ("get_available_preview_width: Pane not realized, using default %d", available_width);
    }
    
    return available_width;
}

/* Metadata display functions */
static void
update_metadata_display (NemoPreviewPane *preview_pane, NemoFile *file)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    char *file_name, *file_size_str, *mime_type, *modified_str;
    guint64 file_size;
    time_t modified_time;
    GDateTime *date_time;
    
    DEBUG ("update_metadata_display: Updating metadata for file");
    
    if (!file) {
        /* Hide metadata box when no file */
        gtk_widget_hide (priv->metadata_box);
        return;
    }
    
    /* Get file information */
    file_name = nemo_file_get_display_name (file);
    file_size = nemo_file_get_size (file);
    mime_type = nemo_file_get_mime_type (file);
    modified_time = nemo_file_get_mtime (file);
    
    /* Format file size */
    file_size_str = g_format_size (file_size);
    
    /* Format modification time */
    date_time = g_date_time_new_from_unix_local (modified_time);
    if (date_time) {
        modified_str = g_date_time_format (date_time, "%x %X");
        g_date_time_unref (date_time);
    } else {
        modified_str = g_strdup (_("Unknown"));
    }
    
    /* Update labels */
    gtk_label_set_text (GTK_LABEL (priv->filename_label), file_name ? file_name : _("Unknown"));
    gtk_label_set_text (GTK_LABEL (priv->filesize_label), file_size_str ? file_size_str : _("Unknown"));
    gtk_label_set_text (GTK_LABEL (priv->filetype_label), mime_type ? mime_type : _("Unknown"));
    gtk_label_set_text (GTK_LABEL (priv->modified_label), modified_str ? modified_str : _("Unknown"));
    
    /* Show metadata box */
    gtk_widget_show (priv->metadata_box);
    
    DEBUG ("update_metadata_display: Metadata updated - %s, %s, %s", 
           file_name ? file_name : "null", 
           file_size_str ? file_size_str : "null",
           mime_type ? mime_type : "null");
    
    /* Cleanup */
    g_free (file_name);
    g_free (file_size_str);
    g_free (mime_type);
    g_free (modified_str);
}

static GtkWidget *
create_metadata_box (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    GtkWidget *box, *grid;
    GtkWidget *name_title, *size_title, *type_title, *modified_title;
    int row = 0;
    
    DEBUG ("create_metadata_box: Creating metadata display");
    
    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    
    /* Title */
    GtkWidget *title = gtk_label_new (_("File Information"));
    gtk_widget_set_halign (title, GTK_ALIGN_START);
    gtk_widget_set_margin_bottom (title, 6);
    PangoAttrList *attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes (GTK_LABEL (title), attrs);
    pango_attr_list_unref (attrs);
    gtk_box_pack_start (GTK_BOX (box), title, FALSE, FALSE, 0);
    
    /* Grid for metadata */
    grid = gtk_grid_new ();
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_grid_set_row_spacing (GTK_GRID (grid), 3);
    gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
    
    /* Name */
    name_title = gtk_label_new (_("Name:"));
    gtk_widget_set_halign (name_title, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), name_title, 0, row, 1, 1);
    
    priv->filename_label = gtk_label_new ("");
    gtk_widget_set_halign (priv->filename_label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL (priv->filename_label), TRUE);
    gtk_grid_attach (GTK_GRID (grid), priv->filename_label, 1, row, 1, 1);
    row++;
    
    /* Size */
    size_title = gtk_label_new (_("Size:"));
    gtk_widget_set_halign (size_title, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), size_title, 0, row, 1, 1);
    
    priv->filesize_label = gtk_label_new ("");
    gtk_widget_set_halign (priv->filesize_label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL (priv->filesize_label), TRUE);
    gtk_grid_attach (GTK_GRID (grid), priv->filesize_label, 1, row, 1, 1);
    row++;
    
    /* Type */
    type_title = gtk_label_new (_("Type:"));
    gtk_widget_set_halign (type_title, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), type_title, 0, row, 1, 1);
    
    priv->filetype_label = gtk_label_new ("");
    gtk_widget_set_halign (priv->filetype_label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL (priv->filetype_label), TRUE);
    gtk_grid_attach (GTK_GRID (grid), priv->filetype_label, 1, row, 1, 1);
    row++;
    
    /* Modified */
    modified_title = gtk_label_new (_("Modified:"));
    gtk_widget_set_halign (modified_title, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), modified_title, 0, row, 1, 1);
    
    priv->modified_label = gtk_label_new ("");
    gtk_widget_set_halign (priv->modified_label, GTK_ALIGN_START);
    gtk_label_set_selectable (GTK_LABEL (priv->modified_label), TRUE);
    gtk_grid_attach (GTK_GRID (grid), priv->modified_label, 1, row, 1, 1);
    row++;
    
    return box;
}

/* Schedule immediate PURE GTK visual feedback - no pixbuf operations */
static void
schedule_immediate_visual_feedback (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("schedule_immediate_visual_feedback: Scheduling PURE GTK widget scaling");
    
    /* Only schedule visual feedback for image-based content */
    if (priv->current_preview_type != PREVIEW_TYPE_IMAGE && 
        priv->current_preview_type != PREVIEW_TYPE_VIDEO &&
        priv->current_preview_type != PREVIEW_TYPE_PDF) {
        return;
    }
    
    /* Mark that we need visual update */
    priv->needs_visual_update = TRUE;
    
    /* Cancel any existing immediate feedback timeout */
    if (priv->immediate_feedback_timeout_id > 0) {
        g_source_remove (priv->immediate_feedback_timeout_id);
    }
    
    /* Schedule pure GTK widget scaling in next idle cycle */
    priv->immediate_feedback_timeout_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
                                                          apply_immediate_visual_feedback,
                                                          preview_pane,
                                                          NULL);
    
    DEBUG ("schedule_immediate_visual_feedback: Scheduled pure GTK widget scaling");
}

/* Debouncing helper functions for smooth resize operations */
static void
cancel_resize_timeout (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    if (priv->resize_timeout_id > 0) {
        g_source_remove (priv->resize_timeout_id);
        priv->resize_timeout_id = 0;
        DEBUG ("cancel_resize_timeout: Cancelled pending resize timeout");
    }
}

static gboolean
on_resize_timeout (gpointer user_data)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    int target_width = priv->pending_resize_width;
    
    DEBUG ("on_resize_timeout: Processing debounced resize to width %d", target_width);
    
    /* Clear timeout ID first */
    priv->resize_timeout_id = 0;
    priv->resize_in_progress = FALSE;
    
    /* Only process if we still have valid content and the file hasn't changed */
    if (priv->current_file && priv->preview_content_widget) {
        /* Start async re-rendering for optimal quality at new width */
        start_async_render (preview_pane, target_width);
        
        /* Schedule final validation to ensure content is still visible */
        g_idle_add_full (G_PRIORITY_LOW, 
                       (GSourceFunc) ensure_preview_visible, 
                       g_object_ref (preview_pane), 
                       (GDestroyNotify) g_object_unref);
                       
        DEBUG ("on_resize_timeout: Started async re-render and final validation");
    }
    
    return G_SOURCE_REMOVE;  /* Don't repeat */
}

/* Ultra-lightweight immediate visual feedback using GTK native scaling */
static void
cancel_immediate_feedback_timeout (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    if (priv->immediate_feedback_timeout_id > 0) {
        g_source_remove (priv->immediate_feedback_timeout_id);
        priv->immediate_feedback_timeout_id = 0;
        DEBUG ("cancel_immediate_feedback_timeout: Cancelled pending visual update");
    }
}

static gboolean
apply_immediate_visual_feedback (gpointer user_data)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("apply_immediate_visual_feedback: Applying PURE GTK widget scaling");
    
    /* Clear timeout ID first */
    priv->immediate_feedback_timeout_id = 0;
    priv->needs_visual_update = FALSE;
    
    /* Only apply visual feedback if we have valid content */
    if (!priv->current_file || 
        !priv->preview_content_widget || 
        !GTK_IS_IMAGE (priv->preview_content_widget)) {
        return G_SOURCE_REMOVE;
    }
    
    /* Let GTK handle the widget scaling naturally - NO pixbuf operations */
    GtkWidget *image_widget = priv->preview_content_widget;
    if (GTK_IS_WIDGET (image_widget)) {
        /* Force the widget to recalculate its size and redraw */
        gtk_widget_queue_resize (image_widget);
        gtk_widget_queue_draw (image_widget);
        
        DEBUG ("apply_immediate_visual_feedback: Applied pure GTK widget scaling");
    }
    
    return G_SOURCE_REMOVE;  /* Don't repeat */
}

/* Async rendering completion callback */
static void
on_async_render_complete (GObject *source_object,
                         GAsyncResult *result,
                         gpointer user_data)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    GtkWidget *new_content_widget;
    GError *error = NULL;
    
    DEBUG ("on_async_render_complete: Async render completed");
    
    /* Check if operation was cancelled */
    if (g_cancellable_is_cancelled (priv->async_render_cancellable)) {
        DEBUG ("on_async_render_complete: Operation was cancelled");
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* Validate that we still have the same file and we're still showing preview content */
    if (!priv->current_file || !priv->preview_content_widget) {
        DEBUG ("on_async_render_complete: State changed - no current file or preview widget");
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* Get the result */
    gboolean success = g_task_propagate_boolean (G_TASK (result), &error);
    new_content_widget = g_task_get_source_tag (G_TASK (result));
    
    if (!success || !new_content_widget) {
        DEBUG ("on_async_render_complete: Async render failed or no widget produced");
        if (error) {
            DEBUG ("on_async_render_complete: Error: %s", error->message);
            g_error_free (error);
        }
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* ULTIMATE SAFETY: Multiple validation checks to prevent ANY disappearing content */
    
    /* First validation: Basic state check */
    if (!priv->current_file || !priv->preview_content_widget) {
        DEBUG ("on_async_render_complete: No current file or preview widget - aborting");
        gtk_widget_destroy (new_content_widget);
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* Second validation: Widget integrity check */
    if (!GTK_IS_WIDGET (priv->preview_content_widget) || 
        !GTK_IS_WIDGET (priv->content_box) ||
        gtk_widget_get_parent (priv->preview_content_widget) != priv->content_box) {
        DEBUG ("on_async_render_complete: Widget hierarchy corrupted - forcing recreation");
        gtk_widget_destroy (new_content_widget);
        ensure_preview_visible (preview_pane);
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* Third validation: Type compatibility check for safe update */
    if (GTK_IS_IMAGE (priv->preview_content_widget) && GTK_IS_IMAGE (new_content_widget)) {
        
        DEBUG ("on_async_render_complete: Safely updating existing image widget");
        
        /* Get the new pixbuf from the async rendered widget */
        GdkPixbuf *new_pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (new_content_widget));
        
        if (new_pixbuf && GDK_IS_PIXBUF (new_pixbuf)) {
            /* CRITICAL: Final validation before update */
            if (GTK_IS_IMAGE (priv->preview_content_widget) && GTK_IS_WIDGET (priv->preview_content_widget)) {
                /* This is the safest approach - update content, don't replace widget */
                gtk_image_set_from_pixbuf (GTK_IMAGE (priv->preview_content_widget), new_pixbuf);
                
                DEBUG ("on_async_render_complete: Successfully updated preview image content");
            } else {
                DEBUG ("on_async_render_complete: Widget became invalid during update - forcing recreation");
                gtk_widget_destroy (new_content_widget);
                ensure_preview_visible (preview_pane);
                priv->rendering_in_progress = FALSE;
                return;
            }
        } else {
            DEBUG ("on_async_render_complete: No valid pixbuf from async render");
        }
        
        /* Clean up the temporary widget */
        gtk_widget_destroy (new_content_widget);
        
        DEBUG ("on_async_render_complete: Image update completed successfully");
        
    } else {
        DEBUG ("on_async_render_complete: Content widgets are not compatible images - skipping update");
        gtk_widget_destroy (new_content_widget);
        
        /* Don't attempt risky widget replacement - just keep what we have */
        DEBUG ("on_async_render_complete: Keeping existing content for safety");
    }
    
    priv->rendering_in_progress = FALSE;
}

/* Async rendering task */
static void
async_render_preview_task (GTask *task,
                          gpointer source_object,
                          gpointer task_data,
                          GCancellable *cancellable)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (source_object);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    int target_width = GPOINTER_TO_INT (task_data);
    GtkWidget *new_content_widget = NULL;
    
    DEBUG ("async_render_preview_task: Starting async render for width %d", target_width);
    
    /* Check for cancellation before starting work */
    if (g_cancellable_is_cancelled (cancellable)) {
        DEBUG ("async_render_preview_task: Cancelled before starting");
        return;
    }
    
    /* Create new preview content with target width */
    switch (priv->current_preview_type) {
        case PREVIEW_TYPE_IMAGE:
            new_content_widget = create_thumbnail_image_preview (priv->current_file, target_width);
            if (!new_content_widget) {
                char *file_path = nemo_file_get_path (priv->current_file);
                if (file_path) {
                    new_content_widget = create_image_preview (file_path, target_width);
                    g_free (file_path);
                }
            }
            break;
        case PREVIEW_TYPE_VIDEO:
            new_content_widget = create_thumbnail_image_preview (priv->current_file, target_width);
            break;
        case PREVIEW_TYPE_PDF:
            {
                char *file_path = nemo_file_get_path (priv->current_file);
                if (file_path) {
                    /* Use direct PDF rendering for pixel-perfect quality */
                    int actual_target_width = MAX(200, target_width - 20);  /* Account for margins */
                    new_content_widget = create_pdf_preview_direct (file_path, actual_target_width);
                    
                    /* Fall back to thumbnail if direct rendering fails */
                    if (!new_content_widget) {
                        new_content_widget = create_thumbnail_image_preview (priv->current_file, target_width);
                    }
                    
                    g_free (file_path);
                } else {
                    new_content_widget = create_thumbnail_image_preview (priv->current_file, target_width);
                }
            }
            break;
        default:
            DEBUG ("async_render_preview_task: Unsupported preview type for async rendering: %d", priv->current_preview_type);
            break;
    }
    
    /* Check for cancellation again before returning result */
    if (g_cancellable_is_cancelled (cancellable)) {
        DEBUG ("async_render_preview_task: Cancelled after rendering");
        if (new_content_widget) {
            gtk_widget_destroy (new_content_widget);
        }
        return;
    }
    
    if (new_content_widget) {
        DEBUG ("async_render_preview_task: Successfully created new preview widget");
        g_task_set_source_tag (task, new_content_widget);
        g_task_return_boolean (task, TRUE);
    } else {
        DEBUG ("async_render_preview_task: Failed to create new preview widget");
        g_task_return_boolean (task, FALSE);
    }
}

/* Start async rendering for new width */
static void
start_async_render (NemoPreviewPane *preview_pane, int target_width)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    GTask *task;
    
    DEBUG ("start_async_render: Starting async render for width %d", target_width);
    
    /* Validate state before starting async operation */
    if (!validate_preview_state (preview_pane)) {
        DEBUG ("start_async_render: Invalid preview state - aborting async render");
        ensure_preview_visible (preview_pane);
        return;
    }
    
    /* Cancel any existing render operation */
    if (priv->async_render_cancellable) {
        DEBUG ("start_async_render: Cancelling previous render operation");
        g_cancellable_cancel (priv->async_render_cancellable);
        g_object_unref (priv->async_render_cancellable);
    }
    
    /* Create new cancellable */
    priv->async_render_cancellable = g_cancellable_new ();
    priv->rendering_in_progress = TRUE;
    priv->target_render_width = target_width;
    
    /* Increment sequence number for this render operation */
    priv->async_render_sequence++;
    
    /* Create and start async task */
    task = g_task_new (preview_pane, priv->async_render_cancellable, on_async_render_complete, preview_pane);
    g_task_set_task_data (task, GINT_TO_POINTER (target_width), NULL);
    g_task_run_in_thread (task, async_render_preview_task);
    g_object_unref (task);
    
    DEBUG ("start_async_render: Async render task started");
}

/* ABSOLUTELY MINIMAL resize handling - ONLY for perfect cursor tracking */
static void
on_preview_pane_size_allocate (GtkWidget *widget,
                               GtkAllocation *allocation,
                               gpointer user_data)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    int current_width = allocation->width;
    
    /* ABSOLUTELY CRITICAL: Do ONLY the minimum required for cursor tracking */
    priv->last_preview_width = current_width;
    
    /* SCHEDULE high-quality rendering with a much longer delay to avoid ANY interference */
    /* Cancel previous timeout for debouncing */
    if (priv->resize_timeout_id > 0) {
        g_source_remove (priv->resize_timeout_id);
    }
    
    /* Store current width for later use */
    priv->pending_resize_width = current_width;
    
    /* Schedule debounced async rendering with LONG delay to ensure no interference with cursor tracking */
    priv->resize_timeout_id = g_timeout_add (500, on_resize_timeout, preview_pane);
    
    /* HANDLER COMPLETE - Absolute minimum operations for perfect cursor responsiveness */
}

/* Validate and ensure preview integrity */
static gboolean
validate_preview_state (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    /* Check if we should have preview content but don't */
    if (priv->current_file && priv->current_preview_type != PREVIEW_TYPE_NONE) {
        /* We should have content, check if it's valid */
        if (!priv->preview_content_widget ||
            !GTK_IS_WIDGET (priv->preview_content_widget) ||
            gtk_widget_get_parent (priv->preview_content_widget) != priv->content_box) {
            DEBUG ("validate_preview_state: Preview content is missing or invalid - will recreate");
            return FALSE;
        }
    }
    
    return TRUE;
}

/* Emergency preview recovery - ensures there's always visible content */
static gboolean
ensure_preview_visible (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("ensure_preview_visible: Checking preview visibility");
    
    if (!validate_preview_state (preview_pane)) {
        if (priv->current_file) {
            DEBUG ("ensure_preview_visible: Recreating preview for current file");
            nemo_preview_pane_set_file (preview_pane, priv->current_file);
        } else {
            DEBUG ("ensure_preview_visible: No current file, showing no-selection state");
            show_no_selection_state (preview_pane);
        }
    }
    
    return FALSE; /* Remove this idle callback */
}

/* Preview content management functions */
static void
clear_preview_content (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    /* Hide all state widgets */
    if (priv->no_selection_label) {
        gtk_widget_hide (priv->no_selection_label);
    }
    if (priv->loading_label) {
        gtk_widget_hide (priv->loading_label);
    }
    if (priv->error_label) {
        gtk_widget_hide (priv->error_label);
    }
    if (priv->metadata_box) {
        gtk_widget_hide (priv->metadata_box);
    }
    
    /* Cancel any async rendering */
    if (priv->async_render_cancellable) {
        DEBUG ("clear_preview_content: Cancelling async render operation");
        g_cancellable_cancel (priv->async_render_cancellable);
        g_object_unref (priv->async_render_cancellable);
        priv->async_render_cancellable = NULL;
    }
    priv->rendering_in_progress = FALSE;
    
    /* Remove current preview content safely */
    if (priv->preview_content_widget) {
        if (GTK_IS_WIDGET (priv->preview_content_widget) &&
            gtk_widget_get_parent (priv->preview_content_widget) == priv->content_box) {
            gtk_container_remove (GTK_CONTAINER (priv->content_box), priv->preview_content_widget);
        }
        priv->preview_content_widget = NULL;
    }
    
    priv->current_preview_type = PREVIEW_TYPE_NONE;
}

static void
show_loading_state (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("show_loading_state: Showing loading state");
    clear_preview_content (preview_pane);
    gtk_widget_show (priv->loading_label);
}

static void
show_error_state (NemoPreviewPane *preview_pane, const char *error_message)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("show_error_state: Showing error state: %s", error_message ? error_message : "default error");
    clear_preview_content (preview_pane);
    
    if (error_message) {
        gtk_label_set_text (GTK_LABEL (priv->error_label), error_message);
    } else {
        gtk_label_set_text (GTK_LABEL (priv->error_label), _("Unable to preview this file"));
    }
    
    gtk_widget_show (priv->error_label);
    
    /* Show metadata even when preview fails (if we have a file) */
    if (priv->current_file) {
        update_metadata_display (preview_pane, priv->current_file);
    }
}

static void
show_no_selection_state (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("show_no_selection_state: Showing no selection state");
    clear_preview_content (preview_pane);
    gtk_widget_show (priv->no_selection_label);
}

static void
show_preview_content (NemoPreviewPane *preview_pane, GtkWidget *content_widget, PreviewType type)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    DEBUG ("show_preview_content: Showing preview content (widget: %p, type: %d)", content_widget, type);
    clear_preview_content (preview_pane);
    
    if (content_widget) {
        priv->preview_content_widget = content_widget;
        priv->current_preview_type = type;
        
        DEBUG ("show_preview_content: Set current_preview_type to %d, content_widget to %p", type, content_widget);
        
        /* Add content widget with proper alignment for large images */
        if (GTK_IS_IMAGE (content_widget)) {
            /* Center images horizontally, top-align vertically for scrolling */
            gtk_widget_set_halign (content_widget, GTK_ALIGN_CENTER);
            gtk_widget_set_valign (content_widget, GTK_ALIGN_START);
        }
        
        gtk_box_pack_start (GTK_BOX (priv->content_box), content_widget, FALSE, FALSE, 0);
        gtk_widget_show (content_widget);
        
        /* Show metadata for the current file */
        update_metadata_display (preview_pane, priv->current_file);
        
        DEBUG ("show_preview_content: Successfully added content widget and metadata");
    } else {
        DEBUG ("show_preview_content: NULL content widget, showing error state");
        show_error_state (preview_pane, NULL);
    }
}

static void
nemo_preview_pane_dispose (GObject *object)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (object);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    
    /* Clean up file reference */
    if (priv->current_file) {
        nemo_file_unref (priv->current_file);
        priv->current_file = NULL;
    }
    
    /* Cancel any async rendering */
    if (priv->async_render_cancellable) {
        g_cancellable_cancel (priv->async_render_cancellable);
        g_object_unref (priv->async_render_cancellable);
        priv->async_render_cancellable = NULL;
    }
    
    /* Cancel any pending resize timeout */
    cancel_resize_timeout (preview_pane);
    
    /* Cancel any immediate feedback timeout */
    cancel_immediate_feedback_timeout (preview_pane);
    
    /* Clean up selection connection - will be implemented in Phase 3 */
    if (priv->selection_changed_id) {
        priv->selection_changed_id = 0;
    }
    
    G_OBJECT_CLASS (nemo_preview_pane_parent_class)->dispose (object);
}

static void
nemo_preview_pane_class_init (NemoPreviewPaneClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    
    object_class->dispose = nemo_preview_pane_dispose;
}

static void
nemo_preview_pane_init (NemoPreviewPane *preview_pane)
{
    DEBUG ("nemo_preview_pane_init: Initializing preview pane %p", preview_pane);
    
    preview_pane->priv = nemo_preview_pane_get_instance_private (preview_pane);
    
    /* Basic setup - optimized for large preview images */
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (preview_pane),
                                   GTK_POLICY_NEVER,     /* No horizontal scroll - images fit width */
                                   GTK_POLICY_AUTOMATIC); /* Vertical scroll for tall images */
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (preview_pane),
                                        GTK_SHADOW_IN);
    
    /* Optimize scrolling performance for large images */
    gtk_scrolled_window_set_kinetic_scrolling (GTK_SCROLLED_WINDOW (preview_pane), TRUE);
    
    /* Set minimum width to ensure preview pane isn't too narrow */
    gtk_widget_set_size_request (GTK_WIDGET (preview_pane), 300, -1);
                                        
    DEBUG ("nemo_preview_pane_init: Basic scrolled window setup complete");
    
    /* Create basic content structure with MINIMAL borders for maximum space */
    preview_pane->priv->content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width (GTK_CONTAINER (preview_pane->priv->content_box), 5);  /* Minimal border */
    gtk_container_add (GTK_CONTAINER (preview_pane), preview_pane->priv->content_box);
    
    /* Create state labels */
    preview_pane->priv->no_selection_label = gtk_label_new (_("No file selected"));
    gtk_widget_set_halign (preview_pane->priv->no_selection_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (preview_pane->priv->no_selection_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (preview_pane->priv->content_box),
                        preview_pane->priv->no_selection_label, TRUE, TRUE, 0);
    
    preview_pane->priv->loading_label = gtk_label_new (_("Loading..."));
    gtk_widget_set_halign (preview_pane->priv->loading_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (preview_pane->priv->loading_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (preview_pane->priv->content_box),
                        preview_pane->priv->loading_label, TRUE, TRUE, 0);
    
    preview_pane->priv->error_label = gtk_label_new ("");
    gtk_widget_set_halign (preview_pane->priv->error_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (preview_pane->priv->error_label, GTK_ALIGN_CENTER);
    gtk_label_set_line_wrap (GTK_LABEL (preview_pane->priv->error_label), TRUE);
    gtk_box_pack_start (GTK_BOX (preview_pane->priv->content_box),
                        preview_pane->priv->error_label, TRUE, TRUE, 0);
    
    /* Create metadata display */
    preview_pane->priv->metadata_box = create_metadata_box (preview_pane);
    gtk_box_pack_start (GTK_BOX (preview_pane->priv->content_box),
                        preview_pane->priv->metadata_box, FALSE, FALSE, 0);
    gtk_widget_hide (preview_pane->priv->metadata_box); /* Hidden initially */
    
    /* Initialize state */
    preview_pane->priv->current_file = NULL;
    preview_pane->priv->current_preview_type = PREVIEW_TYPE_NONE;
    preview_pane->priv->preview_content_widget = NULL;
    preview_pane->priv->last_preview_width = 0;
    
    /* Initialize async rendering state */
    preview_pane->priv->async_render_cancellable = NULL;
    preview_pane->priv->rendering_in_progress = FALSE;
    preview_pane->priv->target_render_width = 0;
    
    /* Initialize resize debouncing state */
    preview_pane->priv->resize_timeout_id = 0;
    preview_pane->priv->pending_resize_width = 0;
    preview_pane->priv->resize_in_progress = FALSE;
    
    /* Initialize ultra-lightweight feedback system */
    preview_pane->priv->immediate_feedback_timeout_id = 0;
    preview_pane->priv->needs_visual_update = FALSE;
    preview_pane->priv->pending_visual_width = 0;
    
    /* TEMPORARILY DISABLE resize signal to ensure perfect cursor tracking */
    /* g_signal_connect (preview_pane, "size-allocate",
                      G_CALLBACK (on_preview_pane_size_allocate), preview_pane); */
    
    DEBUG ("nemo_preview_pane_init: Connected size-allocate signal for resize handling");
    
    /* Start with no selection state */
    show_no_selection_state (preview_pane);
    
    gtk_widget_show (preview_pane->priv->content_box);
    
    DEBUG ("nemo_preview_pane_init: Preview pane initialization complete");
}

GtkWidget *
nemo_preview_pane_new (NemoWindow *window)
{
    NemoPreviewPane *preview_pane;
    
    DEBUG ("nemo_preview_pane_new: Creating new preview pane for window %p", window);
    
    preview_pane = g_object_new (NEMO_TYPE_PREVIEW_PANE, NULL);
    preview_pane->priv->window = window;
    
    DEBUG ("nemo_preview_pane_new: Preview pane created successfully: %p", preview_pane);
    
    return GTK_WIDGET (preview_pane);
}

/* Main preview function - Phase 2 implementation */
void
nemo_preview_pane_set_file (NemoPreviewPane *preview_pane, NemoFile *file)
{
    NemoPreviewPanePrivate *priv;
    PreviewType preview_type;
    GtkWidget *content_widget = NULL;
    char *file_path = NULL;
    char *file_name = NULL;
    
    DEBUG ("=== nemo_preview_pane_set_file: CALLED with preview_pane=%p, file=%p ===", preview_pane, file);
    
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (preview_pane));
    
    priv = preview_pane->priv;
    
    if (file) {
        file_name = nemo_file_get_display_name (file);
        DEBUG ("nemo_preview_pane_set_file: File name: %s", file_name ? file_name : "unknown");
    }
    
    /* Clear current file reference */
    if (priv->current_file) {
        DEBUG ("nemo_preview_pane_set_file: Clearing previous file reference");
        nemo_file_unref (priv->current_file);
        priv->current_file = NULL;
    }
    
    /* Cancel any pending visual updates when changing files */
    cancel_immediate_feedback_timeout (preview_pane);
    
    /* Handle no file case */
    if (!file) {
        DEBUG ("nemo_preview_pane_set_file: No file provided, showing no selection state");
        show_no_selection_state (preview_pane);
        return;
    }
    
    /* Check if file is ready */
    if (!nemo_file_check_if_ready (file, NEMO_FILE_ATTRIBUTES_FOR_ICON)) {
        DEBUG ("nemo_preview_pane_set_file: File not ready, showing loading state");
        show_loading_state (preview_pane);
        priv->current_file = nemo_file_ref (file);
        g_free (file_name);
        return;
    }
    
    /* Show loading state initially */
    DEBUG ("nemo_preview_pane_set_file: File ready, showing loading state");
    show_loading_state (preview_pane);
    
    /* Store current file */
    priv->current_file = nemo_file_ref (file);
    
    /* Detect preview type */
    preview_type = detect_preview_type (file);
    
    if (preview_type == PREVIEW_TYPE_NONE || preview_type == PREVIEW_TYPE_UNSUPPORTED) {
        DEBUG ("nemo_preview_pane_set_file: Unsupported file type, showing error");
        show_error_state (preview_pane, _("Preview not available for this file type"));
        g_free (file_name);
        return;
    }
    
    /* Get file path */
    file_path = nemo_file_get_path (file);
    DEBUG ("nemo_preview_pane_set_file: File path: %s", file_path ? file_path : "NULL");
    if (!file_path) {
        DEBUG ("nemo_preview_pane_set_file: Cannot get file path, showing error");
        show_error_state (preview_pane, _("Cannot access file path"));
        g_free (file_name);
        return;
    }
    
    /* Create appropriate preview content */
    DEBUG ("nemo_preview_pane_set_file: Creating preview content for type %d", preview_type);
    switch (preview_type) {
        case PREVIEW_TYPE_TEXT:
            content_widget = create_text_preview (file_path);
            break;
        case PREVIEW_TYPE_IMAGE:
            {
                int available_width = get_available_preview_width (preview_pane);
                DEBUG ("nemo_preview_pane_set_file: Creating IMAGE preview with available_width=%d", available_width);
                /* Try thumbnail-based preview first */
                content_widget = create_thumbnail_image_preview (file, available_width);
                /* If that failed and we have a file path, fall back to direct loading */
                if (!content_widget && file_path) {
                    DEBUG ("nemo_preview_pane_set_file: Thumbnail failed, falling back to direct image loading");
                    content_widget = create_image_preview (file_path, available_width);
                }
            }
            break;
        case PREVIEW_TYPE_VIDEO:
            {
                int available_width = get_available_preview_width (preview_pane);
                DEBUG ("nemo_preview_pane_set_file: Creating VIDEO preview with available_width=%d", available_width);
                /* Use thumbnail system for video files */
                content_widget = create_thumbnail_image_preview (file, available_width);
            }
            break;
        case PREVIEW_TYPE_PDF:
            {
                int available_width = get_available_preview_width (preview_pane);
                int target_width = MAX(200, available_width - 20);  /* Minimal margins */
                DEBUG ("nemo_preview_pane_set_file: Creating PIXEL-PERFECT PDF preview at exact width %d", target_width);
                
                /* Try direct PDF rendering for pixel-perfect quality */
                content_widget = create_pdf_preview_direct (file_path, target_width);
                
                /* If direct rendering failed, fall back to thumbnail system */
                if (!content_widget) {
                    DEBUG ("nemo_preview_pane_set_file: Direct PDF rendering failed, falling back to thumbnail");
                    content_widget = create_thumbnail_image_preview (file, available_width);
                }
            }
            break;
        default:
            content_widget = NULL;
            break;
    }
    
    DEBUG ("nemo_preview_pane_set_file: Content widget created: %p", content_widget);
    
    /* Show the preview content or error */
    show_preview_content (preview_pane, content_widget, preview_type);
    
    DEBUG ("=== nemo_preview_pane_set_file: COMPLETED ===");
    
    g_free (file_path);
    g_free (file_name);
}

void
nemo_preview_pane_clear (NemoPreviewPane *preview_pane)
{
    NemoPreviewPanePrivate *priv;
    
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (preview_pane));
    
    priv = preview_pane->priv;
    
    /* Clear current file reference */
    if (priv->current_file) {
        nemo_file_unref (priv->current_file);
        priv->current_file = NULL;
    }
    
    /* Show no selection state */
    show_no_selection_state (preview_pane);
}

/* Test function for debugging - bypasses NemoFile and directly tests rendering */
void
nemo_preview_pane_test_with_path (NemoPreviewPane *preview_pane, const char *file_path)
{
    NemoPreviewPanePrivate *priv;
    GtkWidget *content_widget = NULL;
    char *mime_type = NULL;
    PreviewType preview_type = PREVIEW_TYPE_UNSUPPORTED;
    
    DEBUG ("=== nemo_preview_pane_test_with_path: Testing with path: %s ===", file_path ? file_path : "NULL");
    
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (preview_pane));
    g_return_if_fail (file_path != NULL);
    
    priv = preview_pane->priv;
    
    /* Get MIME type from file extension (simplified) */
    if (g_str_has_suffix (file_path, ".txt") || 
        g_str_has_suffix (file_path, ".md") ||
        g_str_has_suffix (file_path, ".log")) {
        preview_type = PREVIEW_TYPE_TEXT;
        mime_type = g_strdup ("text/plain");
    } else if (g_str_has_suffix (file_path, ".jpg") ||
               g_str_has_suffix (file_path, ".jpeg") ||
               g_str_has_suffix (file_path, ".png") ||
               g_str_has_suffix (file_path, ".gif")) {
        preview_type = PREVIEW_TYPE_IMAGE;
        mime_type = g_strdup ("image/jpeg");
    } else if (g_str_has_suffix (file_path, ".mp4") ||
               g_str_has_suffix (file_path, ".avi") ||
               g_str_has_suffix (file_path, ".mov") ||
               g_str_has_suffix (file_path, ".mkv")) {
        preview_type = PREVIEW_TYPE_VIDEO;
        mime_type = g_strdup ("video/mp4");
    } else if (g_str_has_suffix (file_path, ".pdf")) {
        preview_type = PREVIEW_TYPE_PDF;
        mime_type = g_strdup ("application/pdf");
    }
    
    DEBUG ("nemo_preview_pane_test_with_path: Detected type %d for file %s", preview_type, file_path);
    
    if (preview_type == PREVIEW_TYPE_UNSUPPORTED) {
        show_error_state (preview_pane, "Test: Unsupported file type");
        g_free (mime_type);
        return;
    }
    
    show_loading_state (preview_pane);
    
    /* Create appropriate preview content */
    switch (preview_type) {
        case PREVIEW_TYPE_TEXT:
            content_widget = create_text_preview (file_path);
            break;
        case PREVIEW_TYPE_IMAGE:
            {
                int available_width = get_available_preview_width (preview_pane);
                content_widget = create_image_preview (file_path, available_width);
            }
            break;
        case PREVIEW_TYPE_VIDEO:
            {
                int available_width = get_available_preview_width (preview_pane);
                /* For test function, just show placeholder since we don't have NemoFile */
                content_widget = gtk_label_new ("Video thumbnail preview\n(requires file selection)");
                gtk_widget_set_halign (content_widget, GTK_ALIGN_CENTER);
                gtk_widget_set_valign (content_widget, GTK_ALIGN_CENTER);
            }
            break;
        case PREVIEW_TYPE_PDF:
            {
                int available_width = get_available_preview_width (preview_pane);
                /* For test function, just show placeholder since we don't have NemoFile */
                content_widget = gtk_label_new ("PDF document preview\n(requires file selection)");
                gtk_widget_set_halign (content_widget, GTK_ALIGN_CENTER);
                gtk_widget_set_valign (content_widget, GTK_ALIGN_CENTER);
            }
            break;
        default:
            content_widget = NULL;
            break;
    }
    
    DEBUG ("nemo_preview_pane_test_with_path: Created widget: %p", content_widget);
    
    /* Show the preview content or error */
    show_preview_content (preview_pane, content_widget, preview_type);
    
    DEBUG ("=== nemo_preview_pane_test_with_path: Test completed ===");
    
    g_free (mime_type);
}