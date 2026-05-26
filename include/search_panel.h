#ifndef SEARCH_PANEL_H
#define SEARCH_PANEL_H

#include <gtk/gtk.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/* Column indices for the search-results GtkTreeStore */
enum {
    SEARCH_COL_ICON,
    SEARCH_COL_TEXT,
    SEARCH_COL_PATH,
    SEARCH_COL_LINE,
    NUM_SEARCH_COLUMNS
};

/*
 * Recursively searches all text files under 'dir_path' for 'query'
 * and appends matching results to 'store'.
 */
void search_in_files_recursive (const gchar  *dir_path,
                                const gchar  *query,
                                GtkTreeStore *store);

/* Called when the sidebar search entry text changes. */
void on_sidebar_search_changed (GtkSearchEntry  *entry,
                                AetherIdeWindow *self);

/* Opens the matching file/line when a search-result row is activated. */
void on_search_row_activated   (GtkTreeView       *tree_view,
                                GtkTreePath       *path,
                                GtkTreeViewColumn *column,
                                AetherIdeWindow   *self);

G_END_DECLS

#endif /* SEARCH_PANEL_H */
