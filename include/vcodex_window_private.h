#ifndef VCODEX_WINDOW_PRIVATE_H
#define VCODEX_WINDOW_PRIVATE_H

/*
 * vcodex_window_private.h
 *
 * Internal accessor functions for AetherIdeWindow fields.
 * Only the sub-modules (editor_tab, file_explorer, search_panel, …)
 * that need to reach into the window's private state should include
 * this header.  External consumers use vcodex_window.h only.
 */

#include "vcodex_window.h"
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

/* --- Notebooks / editors --- */
GtkWidget    *aether_ide_window_get_notebook     (AetherIdeWindow *self);

/* --- File-explorer tree --- */
GtkTreeStore *aether_ide_window_get_tree_store   (AetherIdeWindow *self);
GtkWidget    *aether_ide_window_get_tree_view    (AetherIdeWindow *self);

/* --- Search panel --- */
GtkTreeStore *aether_ide_window_get_search_store (AetherIdeWindow *self);
GtkWidget    *aether_ide_window_get_search_view  (AetherIdeWindow *self);

/* --- Workspace --- */
const gchar  *aether_ide_window_get_workspace_dir (AetherIdeWindow *self);
void          aether_ide_window_set_workspace_dir (AetherIdeWindow *self,
                                                   const gchar     *dir);

G_END_DECLS

#endif /* VCODEX_WINDOW_PRIVATE_H */
