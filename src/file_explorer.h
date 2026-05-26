#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

#include <gtk/gtk.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/* Column indices for the file-explorer GtkTreeStore */
enum {
    COLUMN_ICON,
    COLUMN_NAME,
    COLUMN_PATH,
    COLUMN_IS_DIR,
    NUM_COLUMNS
};

/*
 * Recursively fills 'store' with the filesystem entries under 'path'.
 * Pass parent=NULL to populate from the root of the tree.
 */
void populate_tree_model (GtkTreeStore *store,
                          GtkTreeIter  *parent,
                          const gchar  *path);

/*
 * GtkTreeSortable comparison function; sorts directories before files,
 * then alphabetically (case-insensitive).
 */
gint sort_tree_func (GtkTreeModel *model,
                     GtkTreeIter  *a,
                     GtkTreeIter  *b,
                     gpointer      user_data);

/*
 * Recursively searches 'model' for a row whose COLUMN_PATH equals 'target'.
 * Returns TRUE and sets *out_iter on success.
 */
gboolean search_tree_recursive (GtkTreeModel *model,
                                GtkTreeIter  *iter,
                                const gchar  *target,
                                GtkTreeIter  *out_iter);

/* Activating a file row opens it; activating a dir row toggles expansion. */
void on_tree_row_activated (GtkTreeView       *tree_view,
                            GtkTreePath       *path,
                            GtkTreeViewColumn *column,
                            AetherIdeWindow   *self);

/*
 * Opens a folder-chooser dialog and populates the file-explorer tree
 * with the chosen directory.
 */
void on_open_folder_clicked (GtkButton       *button,
                             AetherIdeWindow *self);

/* Called when the active editor tab changes; highlights the file in the tree. */
void on_notebook_switch_page (GtkNotebook     *notebook,
                              GtkWidget       *page,
                              guint            page_num,
                              AetherIdeWindow *self);

G_END_DECLS

#endif /* FILE_EXPLORER_H */
