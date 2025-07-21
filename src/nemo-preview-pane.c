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
};

G_DEFINE_TYPE_WITH_PRIVATE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_SCROLLED_WINDOW)

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

/* Enhanced image preview using thumbnails when available */
static GtkWidget *
create_thumbnail_image_preview (NemoFile *file, int available_width)
{
    GtkWidget *image = NULL;
    GdkPixbuf *pixbuf = NULL;
    NemoIconInfo *icon_info = NULL;
    int max_width, max_height;
    int icon_size;
    
    DEBUG ("create_thumbnail_image_preview: Creating thumbnail for file (available_width: %d)", available_width);
    
    if (!file) {
        DEBUG ("create_thumbnail_image_preview: NULL file provided");
        return NULL;
    }
    
    /* Calculate max dimensions based on available width */
    max_width = MAX(200, available_width - 60);  /* Leave some margin */
    max_height = (int)(max_width * 0.75);        /* 4:3 aspect ratio limit */
    
    /* Use the smaller of the two as our icon size for thumbnail generation */
    icon_size = MIN(max_width, max_height);
    
    /* Limit icon size to reasonable maximum */
    icon_size = MIN(icon_size, NEMO_ICON_MAXIMUM_SIZE);
    
    DEBUG ("create_thumbnail_image_preview: Requesting thumbnail of size %d", icon_size);
    
    /* Try to get thumbnail using Nemo's thumbnail system */
    icon_info = nemo_file_get_icon (file, 
                                   icon_size, 
                                   max_width,  /* max_width */
                                   1,          /* scale */
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
    int max_width, max_height;
    
    DEBUG ("create_image_preview: Attempting to preview image file: %s (available_width: %d)", 
           file_path ? file_path : "NULL", available_width);
    
    if (!file_path) {
        DEBUG ("create_image_preview: NULL file path provided");
        return NULL;
    }
    
    /* Calculate max dimensions based on available width */
    max_width = MAX(200, available_width - 60);  /* Leave some margin */
    max_height = (int)(max_width * 0.75);        /* 4:3 aspect ratio limit */
    
    DEBUG ("create_image_preview: Using max dimensions %dx%d for available width %d", 
           max_width, max_height, available_width);
    
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
    
    /* Scale image if too large */
    if (width > max_width || height > max_height) {
        double scale = MIN ((double)max_width / width, (double)max_height / height);
        int new_width = (int)(width * scale);
        int new_height = (int)(height * scale);
        
        DEBUG ("create_image_preview: Scaling image to %dx%d (scale: %.2f)", new_width, new_height, scale);
        
        scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf, new_width, new_height, GDK_INTERP_BILINEAR);
        g_object_unref (pixbuf);
        pixbuf = scaled_pixbuf;
    }
    
    image = gtk_image_new_from_pixbuf (pixbuf);
    g_object_unref (pixbuf);
    
    DEBUG ("create_image_preview: Created image widget successfully");
    
    return image;
}

/* Helper function to get available width for preview content */
static int
get_available_preview_width (NemoPreviewPane *preview_pane)
{
    GtkAllocation allocation;
    int available_width = 300; /* fallback default */
    
    if (gtk_widget_get_realized (GTK_WIDGET (preview_pane))) {
        gtk_widget_get_allocation (GTK_WIDGET (preview_pane), &allocation);
        available_width = allocation.width;
        DEBUG ("get_available_preview_width: Pane width is %d", available_width);
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

/* Immediate rescaling of current preview content */
static void
rescale_current_preview_immediate (NemoPreviewPane *preview_pane, int new_width)
{
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    GtkWidget *image_widget;
    GdkPixbuf *current_pixbuf, *scaled_pixbuf;
    int max_width, max_height;
    int current_width, current_height;
    double scale_factor;
    
    DEBUG ("rescale_current_preview_immediate: Rescaling to width %d", new_width);
    
    /* Only rescale image-based content */
    if (priv->current_preview_type != PREVIEW_TYPE_IMAGE && 
        priv->current_preview_type != PREVIEW_TYPE_VIDEO &&
        priv->current_preview_type != PREVIEW_TYPE_PDF) {
        DEBUG ("rescale_current_preview_immediate: Not an image-based preview, skipping");
        return;
    }
    
    if (!priv->preview_content_widget || !GTK_IS_IMAGE (priv->preview_content_widget)) {
        DEBUG ("rescale_current_preview_immediate: No image widget to rescale");
        return;
    }
    
    image_widget = priv->preview_content_widget;
    current_pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (image_widget));
    
    if (!current_pixbuf) {
        DEBUG ("rescale_current_preview_immediate: No pixbuf to rescale");
        return;
    }
    
    /* Calculate new dimensions */
    max_width = MAX(200, new_width - 60);  /* Leave margin */
    max_height = (int)(max_width * 0.75);  /* 4:3 aspect ratio limit */
    
    current_width = gdk_pixbuf_get_width (current_pixbuf);
    current_height = gdk_pixbuf_get_height (current_pixbuf);
    
    /* Calculate scale factor to fit in new dimensions */
    scale_factor = MIN((double)max_width / current_width, (double)max_height / current_height);
    
    /* Don't scale up beyond original size */
    if (scale_factor > 1.0) {
        scale_factor = 1.0;
    }
    
    int new_pixbuf_width = (int)(current_width * scale_factor);
    int new_pixbuf_height = (int)(current_height * scale_factor);
    
    DEBUG ("rescale_current_preview_immediate: Scaling from %dx%d to %dx%d (scale=%.2f)", 
           current_width, current_height, new_pixbuf_width, new_pixbuf_height, scale_factor);
    
    /* Scale the pixbuf */
    scaled_pixbuf = gdk_pixbuf_scale_simple (current_pixbuf, 
                                            new_pixbuf_width, 
                                            new_pixbuf_height, 
                                            GDK_INTERP_BILINEAR);
    
    if (scaled_pixbuf) {
        /* Update the image widget with the scaled pixbuf */
        gtk_image_set_from_pixbuf (GTK_IMAGE (image_widget), scaled_pixbuf);
        g_object_unref (scaled_pixbuf);
        DEBUG ("rescale_current_preview_immediate: Successfully rescaled preview");
    } else {
        DEBUG ("rescale_current_preview_immediate: Failed to scale pixbuf");
    }
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
    
    DEBUG ("on_async_render_complete: Async render completed");
    
    /* Check if operation was cancelled */
    if (g_cancellable_is_cancelled (priv->async_render_cancellable)) {
        DEBUG ("on_async_render_complete: Operation was cancelled");
        priv->rendering_in_progress = FALSE;
        return;
    }
    
    /* Get the result */
    new_content_widget = g_task_get_source_tag (G_TASK (result));
    
    if (new_content_widget && priv->preview_content_widget) {
        DEBUG ("on_async_render_complete: Replacing preview content with new render");
        
        /* Remove old content */
        gtk_container_remove (GTK_CONTAINER (priv->content_box), priv->preview_content_widget);
        
        /* Add new content */
        priv->preview_content_widget = new_content_widget;
        gtk_box_pack_start (GTK_BOX (priv->content_box), new_content_widget, TRUE, TRUE, 0);
        gtk_widget_show (new_content_widget);
        
        DEBUG ("on_async_render_complete: Successfully replaced preview content");
    } else {
        DEBUG ("on_async_render_complete: No new content widget or old widget to replace");
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
        case PREVIEW_TYPE_PDF:
            new_content_widget = create_thumbnail_image_preview (priv->current_file, target_width);
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
    
    /* Create and start async task */
    task = g_task_new (preview_pane, priv->async_render_cancellable, on_async_render_complete, preview_pane);
    g_task_set_task_data (task, GINT_TO_POINTER (target_width), NULL);
    g_task_run_in_thread (task, async_render_preview_task);
    g_object_unref (task);
    
    DEBUG ("start_async_render: Async render task started");
}

/* Preview pane resize handling */
static void
on_preview_pane_size_allocate (GtkWidget *widget,
                               GtkAllocation *allocation,
                               gpointer user_data)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (user_data);
    NemoPreviewPanePrivate *priv = preview_pane->priv;
    int current_width = allocation->width;
    
    DEBUG ("on_preview_pane_size_allocate: Pane resized to %dx%d (was %d wide)", 
           allocation->width, allocation->height, priv->last_preview_width);
    
    /* React to ANY width change (1px or more) for dynamic preview */
    if (current_width != priv->last_preview_width && 
        priv->current_file && 
        priv->preview_content_widget) {
        
        DEBUG ("on_preview_pane_size_allocate: Width change (%d -> %d), implementing dynamic preview", 
               priv->last_preview_width, current_width);
               
        /* Only process image-based content that benefits from dynamic resizing */
        if (priv->current_preview_type == PREVIEW_TYPE_IMAGE || 
            priv->current_preview_type == PREVIEW_TYPE_VIDEO ||
            priv->current_preview_type == PREVIEW_TYPE_PDF) {
            
            DEBUG ("on_preview_pane_size_allocate: Processing dynamic preview for type %d", priv->current_preview_type);
            
            /* Step 1: Immediately rescale current preview to new width */
            rescale_current_preview_immediate (preview_pane, current_width);
            
            /* Step 2: Start async re-rendering for optimal quality at new width */
            start_async_render (preview_pane, current_width);
            
            DEBUG ("on_preview_pane_size_allocate: Started immediate rescaling + async re-render");
        }
    }
    
    priv->last_preview_width = current_width;
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
    
    /* Remove current preview content */
    if (priv->preview_content_widget) {
        gtk_container_remove (GTK_CONTAINER (priv->content_box), priv->preview_content_widget);
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
        
        /* Add content widget */
        gtk_box_pack_start (GTK_BOX (priv->content_box), content_widget, TRUE, TRUE, 0);
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
    
    /* Basic setup */
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (preview_pane),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (preview_pane),
                                        GTK_SHADOW_IN);
    
    /* Set minimum width to ensure preview pane isn't too narrow */
    gtk_widget_set_size_request (GTK_WIDGET (preview_pane), 300, -1);
                                        
    DEBUG ("nemo_preview_pane_init: Basic scrolled window setup complete");
    
    /* Create basic content structure for Phase 2 expansion */
    preview_pane->priv->content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width (GTK_CONTAINER (preview_pane->priv->content_box), 12);
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
    
    /* Connect resize signal for dynamic preview updating */
    g_signal_connect (preview_pane, "size-allocate",
                      G_CALLBACK (on_preview_pane_size_allocate), preview_pane);
    
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
                DEBUG ("nemo_preview_pane_set_file: Creating PDF preview with available_width=%d", available_width);
                /* Use thumbnail system for PDF files - shows first page */
                content_widget = create_thumbnail_image_preview (file, available_width);
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