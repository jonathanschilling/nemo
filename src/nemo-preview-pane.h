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

#ifndef NEMO_PREVIEW_PANE_H
#define NEMO_PREVIEW_PANE_H

#include <gtk/gtk.h>
#include <libnemo-private/nemo-file.h>
#include "nemo-window-types.h"

#define NEMO_TYPE_PREVIEW_PANE nemo_preview_pane_get_type()
#define NEMO_PREVIEW_PANE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), NEMO_TYPE_PREVIEW_PANE, NemoPreviewPane))
#define NEMO_PREVIEW_PANE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), NEMO_TYPE_PREVIEW_PANE, NemoPreviewPaneClass))
#define NEMO_IS_PREVIEW_PANE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NEMO_TYPE_PREVIEW_PANE))

typedef struct _NemoPreviewPane NemoPreviewPane;
typedef struct _NemoPreviewPaneClass NemoPreviewPaneClass;
typedef struct _NemoPreviewPanePrivate NemoPreviewPanePrivate;

struct _NemoPreviewPane {
    GtkScrolledWindow parent;
    
    /* Private data - will be expanded in Phase 2 */
    NemoPreviewPanePrivate *priv;
};

struct _NemoPreviewPaneClass {
    GtkScrolledWindowClass parent_class;
};

GType nemo_preview_pane_get_type (void);
GtkWidget *nemo_preview_pane_new (NemoWindow *window);

/* Functions for later phases */
void nemo_preview_pane_set_file (NemoPreviewPane *preview_pane, NemoFile *file);
void nemo_preview_pane_clear (NemoPreviewPane *preview_pane);

#endif /* NEMO_PREVIEW_PANE_H */