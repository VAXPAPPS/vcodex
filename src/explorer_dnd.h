#ifndef EXPLORER_DND_H
#define EXPLORER_DND_H

#include <gtk/gtk.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/*
 * Sets up drag-and-drop for the file explorer tree view.
 * Enables dragging multiple files out of the tree view and
 * dropping files into the tree view to move them.
 */
void setup_explorer_dnd (GtkWidget *tree_view, AetherIdeWindow *window);

G_END_DECLS

#endif /* EXPLORER_DND_H */
