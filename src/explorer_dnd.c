/* explorer_dnd.c — Drag and Drop support for file explorer */

#include "explorer_dnd.h"
#include "file_explorer.h"
#include "vcodex_window_private.h"
#include <gio/gio.h>
#include <string.h>

static const GtkTargetEntry drag_targets[] = {
    { "text/uri-list", 0, 0 }
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void
refresh_tree (AetherIdeWindow *window)
{
    const gchar *workspace = aether_ide_window_get_workspace_dir (window);
    if (!workspace) return;
    
    GtkTreeStore *store = aether_ide_window_get_tree_store (window);
    gtk_tree_store_clear (store);
    populate_tree_model (store, NULL, workspace);
}

/* ------------------------------------------------------------------ */
/* Drag Source Callbacks                                                */
/* ------------------------------------------------------------------ */

static void
on_drag_data_get (GtkWidget *widget, GdkDragContext *context,
                  GtkSelectionData *selection_data, guint info,
                  guint time, gpointer user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);
    GtkTreeModel *model;
    GList *selected_rows = gtk_tree_selection_get_selected_rows (selection, &model);
    
    if (!selected_rows) return;

    GPtrArray *uris = g_ptr_array_new ();
    for (GList *l = selected_rows; l != NULL; l = l->next) {
        GtkTreePath *path = (GtkTreePath *) l->data;
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter (model, &iter, path)) {
            gchar *filepath = NULL;
            // Reusing COLUMN_PATH from file_explorer.h (index 2)
            gtk_tree_model_get (model, &iter, 2 /* COLUMN_PATH */, &filepath, -1);
            if (filepath) {
                gchar *uri = g_filename_to_uri (filepath, NULL, NULL);
                if (uri) {
                    g_ptr_array_add (uris, uri);
                }
                g_free (filepath);
            }
        }
    }
    
    g_ptr_array_add (uris, NULL); // NULL-terminate the array
    
    gtk_selection_data_set_uris (selection_data, (gchar **) uris->pdata);
    
    for (guint i = 0; i < uris->len - 1; i++) {
        g_free (g_ptr_array_index (uris, i));
    }
    g_ptr_array_free (uris, TRUE);
    g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);
}

/* ------------------------------------------------------------------ */
/* Drag Destination Callbacks                                           */
/* ------------------------------------------------------------------ */

static gboolean
on_drag_motion (GtkWidget *widget, GdkDragContext *context,
                gint x, gint y, guint time, gpointer user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    GtkTreePath *path = NULL;
    GtkTreeViewDropPosition pos;
    
    gboolean is_row = gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, &pos);
    
    if (is_row) {
        // Highlight the row we are hovering over
        gtk_tree_view_set_drag_dest_row (tree_view, path, GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
        gtk_tree_path_free (path);
    } else {
        // Not hovering over a row, drop to workspace root
        gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
    }
    
    gdk_drag_status (context, GDK_ACTION_MOVE, time);
    return TRUE;
}

static void
on_drag_leave (GtkWidget *widget, GdkDragContext *context, guint time, gpointer user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    gtk_tree_view_set_drag_dest_row (tree_view, NULL, GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
}

static gboolean
on_drag_drop (GtkWidget *widget, GdkDragContext *context,
              gint x, gint y, guint time, gpointer user_data)
{
    GdkAtom target = gtk_drag_dest_find_target (widget, context, NULL);
    if (target != GDK_NONE) {
        gtk_drag_get_data (widget, context, target, time);
        return TRUE;
    }
    return FALSE;
}

static void
on_drag_data_received (GtkWidget *widget, GdkDragContext *context,
                       gint x, gint y, GtkSelectionData *selection_data,
                       guint info, guint time, gpointer user_data)
{
    AetherIdeWindow *window = AETHER_IDE_WINDOW (user_data);
    gchar **uris = gtk_selection_data_get_uris (selection_data);
    
    if (!uris) {
        gtk_drag_finish (context, FALSE, FALSE, time);
        return;
    }

    GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
    GtkTreePath *path = NULL;
    GtkTreeViewDropPosition pos;
    gchar *dest_folder = NULL;
    
    gboolean is_row = gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, &pos);
    if (is_row && path) {
        GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter (model, &iter, path)) {
            gchar *target_path = NULL;
            gboolean is_dir = FALSE;
            // 2: COLUMN_PATH, 3: COLUMN_IS_DIR
            gtk_tree_model_get (model, &iter, 2, &target_path, 3, &is_dir, -1);
            
            if (target_path) {
                if (is_dir) {
                    dest_folder = g_strdup (target_path);
                } else {
                    dest_folder = g_path_get_dirname (target_path);
                }
                g_free (target_path);
            }
        }
        gtk_tree_path_free (path);
    }
    
    if (!dest_folder) {
        const gchar *ws = aether_ide_window_get_workspace_dir (window);
        if (ws) {
            dest_folder = g_strdup (ws);
        }
    }

    gboolean success = FALSE;
    if (dest_folder) {
        for (gint i = 0; uris[i] != NULL; i++) {
            gchar *local_path = g_filename_from_uri (uris[i], NULL, NULL);
            if (local_path) {
                gchar *basename = g_path_get_basename (local_path);
                gchar *new_path = g_build_filename (dest_folder, basename, NULL);
                
                // Don't move a file to itself
                if (g_strcmp0 (local_path, new_path) != 0) {
                    GFile *src = g_file_new_for_path (local_path);
                    GFile *dst = g_file_new_for_path (new_path);
                    
                    if (g_file_move (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL)) {
                        success = TRUE;
                    }
                    
                    g_object_unref (src);
                    g_object_unref (dst);
                }
                
                g_free (basename);
                g_free (new_path);
                g_free (local_path);
            }
        }
        g_free (dest_folder);
    }

    g_strfreev (uris);
    
    if (success) {
        refresh_tree (window);
    }
    
    // GDK_ACTION_MOVE implies the source should delete the original file, 
    // but g_file_move already does that. If dropping from an external app,
    // telling it we moved it might cause it to try deleting it again.
    // For internal dnd, it's fine. We return TRUE for del parameter if we want source to delete.
    // Since g_file_move handles the move, we pass FALSE for delete.
    gtk_drag_finish (context, success, FALSE, time);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
setup_explorer_dnd (GtkWidget *tree_view, AetherIdeWindow *window)
{
    // Enable dragging from the tree view
    gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (tree_view),
                                            GDK_BUTTON1_MASK,
                                            drag_targets,
                                            G_N_ELEMENTS (drag_targets),
                                            GDK_ACTION_MOVE | GDK_ACTION_COPY);
                                            
    g_signal_connect (tree_view, "drag-data-get", G_CALLBACK (on_drag_data_get), window);

    // Enable dropping onto the tree view
    gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (tree_view),
                                          drag_targets,
                                          G_N_ELEMENTS (drag_targets),
                                          GDK_ACTION_MOVE | GDK_ACTION_COPY);

    g_signal_connect (tree_view, "drag-motion", G_CALLBACK (on_drag_motion), window);
    g_signal_connect (tree_view, "drag-leave", G_CALLBACK (on_drag_leave), window);
    g_signal_connect (tree_view, "drag-drop", G_CALLBACK (on_drag_drop), window);
    g_signal_connect (tree_view, "drag-data-received", G_CALLBACK (on_drag_data_received), window);
}
