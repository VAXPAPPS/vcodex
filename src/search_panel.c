/* search_panel.c — In-project text search and result navigation */

#include "search_panel.h"
#include "editor_tab.h"
#include "vcodex_window_private.h"

/* ------------------------------------------------------------------ */
/* Recursive file search                                                */
/* ------------------------------------------------------------------ */

void
search_in_files_recursive (const gchar *dir_path, const gchar *query,
                            GtkTreeStore *store)
{
    GDir *dir = g_dir_open (dir_path, 0, NULL);
    if (!dir) return;

    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        if (name[0] == '.') continue;

        gchar *full_path = g_build_filename (dir_path, name, NULL);

        if (g_file_test (full_path, G_FILE_TEST_IS_DIR)) {
            search_in_files_recursive (full_path, query, store);
        } else if (g_file_test (full_path, G_FILE_TEST_IS_REGULAR)) {
            gchar *content = NULL;
            gsize  length;

            if (g_file_get_contents (full_path, &content, &length, NULL) &&
                g_utf8_validate (content, length, NULL))
            {
                gchar    **lines = g_strsplit (content, "\n", -1);
                gboolean   file_node_created = FALSE;
                GtkTreeIter file_iter;

                for (gint i = 0; lines[i] != NULL; i++) {
                    if (!g_strstr_len (lines[i], -1, query)) continue;

                    if (!file_node_created) {
                        GFile     *gfile = g_file_new_for_path (full_path);
                        GFileInfo *info  = g_file_query_info (
                            gfile, G_FILE_ATTRIBUTE_STANDARD_ICON,
                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
                        GIcon *icon = info ? g_file_info_get_icon (info) : NULL;

                        gtk_tree_store_append (store, &file_iter, NULL);
                        gtk_tree_store_set (store, &file_iter,
                                            SEARCH_COL_ICON, icon,
                                            SEARCH_COL_TEXT, name,
                                            SEARCH_COL_PATH, full_path,
                                            SEARCH_COL_LINE, -1,
                                            -1);

                        if (info) g_object_unref (info);
                        g_object_unref (gfile);
                        file_node_created = TRUE;
                    }

                    gchar *chugged  = g_strchug (g_strdup (lines[i]));
                    gchar *snippet  = g_strdup_printf ("%d: %s", i + 1, chugged);
                    GtkTreeIter line_iter;

                    gtk_tree_store_append (store, &line_iter, &file_iter);
                    gtk_tree_store_set (store, &line_iter,
                                        SEARCH_COL_ICON, NULL,
                                        SEARCH_COL_TEXT, snippet,
                                        SEARCH_COL_PATH, full_path,
                                        SEARCH_COL_LINE, i,
                                        -1);
                    g_free (snippet);
                    g_free (chugged);
                }
                g_strfreev (lines);
            }
            g_free (content);
        }
        g_free (full_path);
    }
    g_dir_close (dir);
}

/* ------------------------------------------------------------------ */
/* Search-entry signal handler                                          */
/* ------------------------------------------------------------------ */

void
on_sidebar_search_changed (GtkSearchEntry *entry, AetherIdeWindow *self)
{
    GtkTreeStore *search_store        = aether_ide_window_get_search_store (self);
    GtkWidget    *search_results_view = aether_ide_window_get_search_view  (self);
    const gchar  *workspace_dir       = aether_ide_window_get_workspace_dir (self);

    gtk_tree_store_clear (search_store);

    const gchar *query = gtk_entry_get_text (GTK_ENTRY (entry));
    if (query && query[0] != '\0' && workspace_dir) {
        search_in_files_recursive (workspace_dir, query, search_store);
        gtk_tree_view_expand_all (GTK_TREE_VIEW (search_results_view));
    }
}

/* ------------------------------------------------------------------ */
/* Result row activation                                                */
/* ------------------------------------------------------------------ */

void
on_search_row_activated (GtkTreeView *tree_view, GtkTreePath *path,
                         GtkTreeViewColumn *column, AetherIdeWindow *self)
{
    GtkWidget    *notebook = aether_ide_window_get_notebook (self);
    GtkTreeIter   iter;
    GtkTreeModel *model    = gtk_tree_view_get_model (tree_view);

    if (!gtk_tree_model_get_iter (model, &iter, path)) return;

    gchar *filepath;
    gint   line_number;
    gtk_tree_model_get (model, &iter,
                        SEARCH_COL_PATH, &filepath,
                        SEARCH_COL_LINE, &line_number,
                        -1);

    if (!filepath || !g_file_test (filepath, G_FILE_TEST_IS_REGULAR)) {
        g_free (filepath);
        return;
    }

    GtkWidget *target_source_view = NULL;
    gint       num_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));

    /* Try to find an already-open tab */
    for (gint i = 0; i < num_pages; i++) {
        GtkWidget *page        = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), i);
        GtkWidget *source_view = gtk_bin_get_child (GTK_BIN (page));
        if (source_view && GTK_SOURCE_IS_VIEW (source_view)) {
            const gchar *open_path =
                g_object_get_data (G_OBJECT (source_view), "filepath");
            if (g_strcmp0 (open_path, filepath) == 0) {
                gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), i);
                target_source_view = source_view;
                break;
            }
        }
    }

    /* Open a new tab if needed */
    if (!target_source_view) {
        gchar *content = NULL;
        gsize  length;
        if (g_file_get_contents (filepath, &content, &length, NULL) &&
            g_utf8_validate (content, length, NULL))
        {
            gchar *basename = g_path_get_basename (filepath);
            create_editor_tab (self, basename, filepath, content);
            g_free (basename);

            gint       new_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
            GtkWidget *page     = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), new_page);
            target_source_view  = gtk_bin_get_child (GTK_BIN (page));
        }
        g_free (content);
    }

    /* Scroll to the matching line */
    if (target_source_view && line_number >= 0) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (target_source_view));
        GtkTextIter    text_iter;
        gtk_text_buffer_get_iter_at_line (buffer, &text_iter, line_number);
        gtk_text_buffer_place_cursor (buffer, &text_iter);
        gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (target_source_view),
                                      &text_iter, 0.0, TRUE, 0.5, 0.5);
        gtk_widget_grab_focus (target_source_view);
    }

    g_free (filepath);
}
