/* file_explorer.c — File-tree population, sorting, navigation, and sync */

#include "file_explorer.h"
#include "editor_tab.h"
#include "explorer_context_menu.h"
#include "explorer_dnd.h"
#include "git_manager.h"
#include "vcodex_window_private.h"

/* ------------------------------------------------------------------ */
/* Tree population                                                      */
/* ------------------------------------------------------------------ */

void
populate_tree_model (GtkTreeStore *store, GtkTreeIter *parent, const gchar *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    if (!dir) return;

    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        if (name[0] == '.') continue;

        gchar    *full_path = g_build_filename (path, name, NULL);
        gboolean  is_dir    = g_file_test (full_path, G_FILE_TEST_IS_DIR);

        GFile     *gfile = g_file_new_for_path (full_path);
        GFileInfo *info  = g_file_query_info (gfile,
                                              G_FILE_ATTRIBUTE_STANDARD_ICON,
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL, NULL);
        GIcon *icon = NULL;
        if (info) {
            icon = g_file_info_get_icon (info);
        }

        GtkTreeIter iter;
        gtk_tree_store_append (store, &iter, parent);
        gtk_tree_store_set (store, &iter,
                            COLUMN_ICON,   icon,
                            COLUMN_NAME,   name,
                            COLUMN_PATH,   full_path,
                            COLUMN_IS_DIR, is_dir,
                            -1);

        if (info) g_object_unref (info);
        g_object_unref (gfile);

        if (is_dir) {
            populate_tree_model (store, &iter, full_path);
        }
        g_free (full_path);
    }
    g_dir_close (dir);
}

/* ------------------------------------------------------------------ */
/* Sorting                                                              */
/* ------------------------------------------------------------------ */

gint
sort_tree_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
                gpointer user_data)
{
    gboolean is_dir_a, is_dir_b;
    gchar   *name_a,  *name_b;

    gtk_tree_model_get (model, a, COLUMN_IS_DIR, &is_dir_a, COLUMN_NAME, &name_a, -1);
    gtk_tree_model_get (model, b, COLUMN_IS_DIR, &is_dir_b, COLUMN_NAME, &name_b, -1);

    gint ret = 0;
    if      ( is_dir_a && !is_dir_b) ret = -1;
    else if (!is_dir_a &&  is_dir_b) ret =  1;
    else if ( name_a   &&  name_b  ) ret = g_ascii_strcasecmp (name_a, name_b);

    g_free (name_a);
    g_free (name_b);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Recursive search                                                     */
/* ------------------------------------------------------------------ */

gboolean
search_tree_recursive (GtkTreeModel *model, GtkTreeIter *iter,
                       const gchar *target, GtkTreeIter *out_iter)
{
    do {
        gchar *path;
        gtk_tree_model_get (model, iter, COLUMN_PATH, &path, -1);
        gboolean match = (g_strcmp0 (path, target) == 0);
        g_free (path);

        if (match) {
            *out_iter = *iter;
            return TRUE;
        }
        if (gtk_tree_model_iter_has_child (model, iter)) {
            GtkTreeIter child;
            gtk_tree_model_iter_children (model, &child, iter);
            if (search_tree_recursive (model, &child, target, out_iter))
                return TRUE;
        }
    } while (gtk_tree_model_iter_next (model, iter));

    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Row activation                                                       */
/* ------------------------------------------------------------------ */

void
on_tree_row_activated (GtkTreeView *tree_view, GtkTreePath *path,
                       GtkTreeViewColumn *column, AetherIdeWindow *self)
{
    GtkWidget    *notebook = aether_ide_window_get_notebook (self);
    GtkTreeIter   iter;
    GtkTreeModel *model    = gtk_tree_view_get_model (tree_view);

    if (!gtk_tree_model_get_iter (model, &iter, path)) return;

    gchar    *filepath;
    gboolean  is_dir;
    gtk_tree_model_get (model, &iter,
                        COLUMN_PATH,   &filepath,
                        COLUMN_IS_DIR, &is_dir,
                        -1);

    if (!is_dir && g_file_test (filepath, G_FILE_TEST_IS_REGULAR)) {
        /* Check if file is already open */
        gint num_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));
        gboolean already_open = FALSE;

        for (gint i = 0; i < num_pages; i++) {
            GtkWidget  *page        = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), i);
            GtkWidget  *source_view = gtk_bin_get_child (GTK_BIN (page));
            if (source_view && GTK_SOURCE_IS_VIEW (source_view)) {
                const gchar *open_path =
                    g_object_get_data (G_OBJECT (source_view), "filepath");
                if (g_strcmp0 (open_path, filepath) == 0) {
                    gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), i);
                    already_open = TRUE;
                    break;
                }
            }
        }

        if (!already_open) {
            gchar *content = NULL;
            gsize  length;
            if (g_file_get_contents (filepath, &content, &length, NULL)) {
                if (g_utf8_validate (content, length, NULL)) {
                    gchar *basename = g_path_get_basename (filepath);
                    create_editor_tab (self, basename, filepath, content);
                    g_free (basename);
                } else {
                    g_printerr ("Cannot open binary or non-UTF8 file: %s\n", filepath);
                }
                g_free (content);
            }
        }
    } else if (is_dir) {
        /* Toggle folder expansion */
        if (gtk_tree_view_row_expanded (tree_view, path))
            gtk_tree_view_collapse_row (tree_view, path);
        else
            gtk_tree_view_expand_row (tree_view, path, FALSE);
    }

    g_free (filepath);
}

/* ------------------------------------------------------------------ */
/* Open Folder dialog                                                   */
/* ------------------------------------------------------------------ */

void
on_open_folder_clicked (GtkButton *button, AetherIdeWindow *self)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new ("Open Folder",
                                     GTK_WINDOW (self),
                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                     "_Open",   GTK_RESPONSE_ACCEPT,
                                     NULL);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        aether_ide_window_set_workspace_dir (self, folder);
        
        /* Initialize Git Manager for the new workspace */
        git_manager_init (folder);

        GtkTreeStore *tree_store = aether_ide_window_get_tree_store (self);
        gtk_tree_store_clear (tree_store);
        populate_tree_model (tree_store, NULL, folder);
        g_free (folder);
    }
    gtk_widget_destroy (dialog);
}

/* ------------------------------------------------------------------ */
/* Notebook page switch — sync tree selection to open file             */
/* ------------------------------------------------------------------ */

void
on_notebook_switch_page (GtkNotebook *notebook, GtkWidget *page,
                         guint page_num, AetherIdeWindow *self)
{
    GtkWidget *source_view = gtk_bin_get_child (GTK_BIN (page));
    if (!source_view || !GTK_SOURCE_IS_VIEW (source_view)) return;

    const gchar *filepath =
        g_object_get_data (G_OBJECT (source_view), "filepath");
    if (!filepath) return;

    GtkTreeStore *tree_store = aether_ide_window_get_tree_store (self);
    GtkWidget    *tree_view  = aether_ide_window_get_tree_view  (self);

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tree_store), &iter)) {
        GtkTreeIter result;
        if (search_tree_recursive (GTK_TREE_MODEL (tree_store), &iter,
                                   filepath, &result)) {
            GtkTreePath *tree_path =
                gtk_tree_model_get_path (GTK_TREE_MODEL (tree_store), &result);
            gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree_view),
                                      tree_path, NULL, FALSE);
            gtk_tree_path_free (tree_path);
        }
    }
}
