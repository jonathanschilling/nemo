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

typedef enum {
    PREVIEW_TYPE_NONE,
    PREVIEW_TYPE_TEXT,
    PREVIEW_TYPE_IMAGE,
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
    
    /* Current state */
    NemoFile *current_file;
    PreviewType current_preview_type;
    gulong selection_changed_id;
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
    } else {
        DEBUG ("detect_preview_type: Unsupported file type: %s", mime_type);
    }
    
    g_free (mime_type);
    g_free (file_name);
    return type;
}

/* Preview content creation functions */
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

static GtkWidget *
create_image_preview (const char *file_path)
{
    GtkWidget *image;
    GdkPixbuf *pixbuf;
    GdkPixbuf *scaled_pixbuf;
    GError *error = NULL;
    int width, height;
    int max_width = 400;
    int max_height = 300;
    
    DEBUG ("create_image_preview: Attempting to preview image file: %s", file_path ? file_path : "NULL");
    
    if (!file_path) {
        DEBUG ("create_image_preview: NULL file path provided");
        return NULL;
    }
    
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
        gtk_box_pack_start (GTK_BOX (priv->content_box), content_widget, TRUE, TRUE, 0);
        gtk_widget_show (content_widget);
        DEBUG ("show_preview_content: Successfully added content widget to box");
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
    
    /* Initialize state */
    preview_pane->priv->current_file = NULL;
    preview_pane->priv->current_preview_type = PREVIEW_TYPE_NONE;
    preview_pane->priv->preview_content_widget = NULL;
    
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
            content_widget = create_image_preview (file_path);
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
            content_widget = create_image_preview (file_path);
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