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

struct _NemoPreviewPanePrivate {
    NemoWindow *window;
    
    /* UI Components - will be expanded in Phase 2 */
    GtkWidget *content_box;
    GtkWidget *no_selection_label;
    
    /* Current state - will be used in Phase 3 */
    NemoFile *current_file;
    gulong selection_changed_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (NemoPreviewPane, nemo_preview_pane, GTK_TYPE_SCROLLED_WINDOW)

static void
nemo_preview_pane_dispose (GObject *object)
{
    NemoPreviewPane *preview_pane = NEMO_PREVIEW_PANE (object);
    
    /* Cleanup will be expanded in later phases */
    if (preview_pane->priv->selection_changed_id) {
        /* Will be implemented in Phase 3 */
        preview_pane->priv->selection_changed_id = 0;
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
    preview_pane->priv = nemo_preview_pane_get_instance_private (preview_pane);
    
    /* Basic setup */
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (preview_pane),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (preview_pane),
                                        GTK_SHADOW_IN);
    
    /* Create basic content structure for Phase 2 expansion */
    preview_pane->priv->content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width (GTK_CONTAINER (preview_pane->priv->content_box), 12);
    gtk_container_add (GTK_CONTAINER (preview_pane), preview_pane->priv->content_box);
    
    /* Default "no selection" state */
    preview_pane->priv->no_selection_label = gtk_label_new (_("No file selected"));
    gtk_widget_set_halign (preview_pane->priv->no_selection_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (preview_pane->priv->no_selection_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (preview_pane->priv->content_box),
                        preview_pane->priv->no_selection_label, TRUE, TRUE, 0);
    
    gtk_widget_show_all (preview_pane->priv->content_box);
}

GtkWidget *
nemo_preview_pane_new (NemoWindow *window)
{
    NemoPreviewPane *preview_pane;
    
    preview_pane = g_object_new (NEMO_TYPE_PREVIEW_PANE, NULL);
    preview_pane->priv->window = window;
    
    return GTK_WIDGET (preview_pane);
}

/* Stub implementations for later phases */
void
nemo_preview_pane_set_file (NemoPreviewPane *preview_pane, NemoFile *file)
{
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (preview_pane));
    /* Implementation in Phase 3 */
}

void
nemo_preview_pane_clear (NemoPreviewPane *preview_pane)
{
    g_return_if_fail (NEMO_IS_PREVIEW_PANE (preview_pane));
    /* Implementation in Phase 3 */
}