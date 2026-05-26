/* explorer_context_menu.c — Context menu for file explorer */

#include "explorer_context_menu.h"
#include "file_explorer.h"
#include "vcodex_window_private.h"
#include <gio/gio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Global Clipboard State for File Explorer                             */
/* ------------------------------------------------------------------ */

typedef enum {
    CLIPBOARD_OP_NONE,
    CLIPBOARD_OP_COPY,
    CLIPBOARD_OP_CUT
} ClipboardOp;

static ClipboardOp current_clip_op = CLIPBOARD_OP_NONE;
static gchar *current_clip_path = NULL;

/* ------------------------------------------------------------------ */
/* Data Structures                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    AetherIdeWindow *window;
    gchar *target_path;
    gboolean is_dir;
} ContextActionData;

static ContextActionData *
context_action_data_new (AetherIdeWindow *window, const gchar *path, gboolean is_dir)
{
    ContextActionData *data = g_new0 (ContextActionData, 1);
    data->window = window;
    data->target_path = g_strdup (path);
    data->is_dir = is_dir;
    return data;
}

static void
context_action_data_free (ContextActionData *data)
{
    if (!data) return;
    g_free (data->target_path);
    g_free (data);
}

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

static void
make_dialog_transparent (GtkWidget *dialog)
{
    gtk_widget_set_app_paintable (dialog, TRUE);
    GdkScreen *screen = gtk_widget_get_screen (dialog);
    GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
    if (visual && gdk_screen_is_composited (screen))
        gtk_widget_set_visual (dialog, visual);
}

/* Prompts user with a simple dialog containing an entry */
static gchar *
prompt_user_input (AetherIdeWindow *window, const gchar *title, const gchar *initial_text)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons (title,
                                                     GTK_WINDOW (window),
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_OK", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    make_dialog_transparent (dialog);
    
    GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    GtkWidget *entry = gtk_entry_new ();
    if (initial_text) {
        gtk_entry_set_text (GTK_ENTRY (entry), initial_text);
    }
    gtk_widget_set_margin_top (entry, 10);
    gtk_widget_set_margin_bottom (entry, 10);
    gtk_widget_set_margin_start (entry, 10);
    gtk_widget_set_margin_end (entry, 10);
    
    gtk_container_add (GTK_CONTAINER (content_area), entry);
    gtk_widget_show_all (dialog);
    
    gchar *result = NULL;
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        result = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
    }
    
    gtk_widget_destroy (dialog);
    return result;
}

static gboolean
delete_recursive (GFile *file, GError **error)
{
    GFileInfo *info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
    if (!info) return FALSE;
    
    if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
        GFileEnumerator *enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
        if (enumerator) {
            GFileInfo *child_info;
            while ((child_info = g_file_enumerator_next_file (enumerator, NULL, error)) != NULL) {
                GFile *child = g_file_get_child (file, g_file_info_get_name (child_info));
                delete_recursive (child, error);
                g_object_unref (child);
                g_object_unref (child_info);
            }
            g_object_unref (enumerator);
        }
    }
    g_object_unref (info);
    return g_file_delete (file, NULL, error);
}

/* ------------------------------------------------------------------ */
/* Action Callbacks                                                     */
/* ------------------------------------------------------------------ */

static void
on_new_file (GtkMenuItem *item, ContextActionData *data)
{
    gchar *name = prompt_user_input (data->window, "New File Name", "");
    if (name && name[0] != '\0') {
        gchar *dir = data->is_dir ? g_strdup (data->target_path) : g_path_get_dirname (data->target_path);
        gchar *new_path = g_build_filename (dir, name, NULL);
        
        GFile *file = g_file_new_for_path (new_path);
        g_file_create (file, G_FILE_CREATE_NONE, NULL, NULL);
        g_object_unref (file);
        
        g_free (new_path);
        g_free (dir);
        refresh_tree (data->window);
    }
    g_free (name);
}

static void
on_new_folder (GtkMenuItem *item, ContextActionData *data)
{
    gchar *name = prompt_user_input (data->window, "New Folder Name", "");
    if (name && name[0] != '\0') {
        gchar *dir = data->is_dir ? g_strdup (data->target_path) : g_path_get_dirname (data->target_path);
        gchar *new_path = g_build_filename (dir, name, NULL);
        
        GFile *file = g_file_new_for_path (new_path);
        g_file_make_directory (file, NULL, NULL);
        g_object_unref (file);
        
        g_free (new_path);
        g_free (dir);
        refresh_tree (data->window);
    }
    g_free (name);
}

static void
on_rename (GtkMenuItem *item, ContextActionData *data)
{
    gchar *basename = g_path_get_basename (data->target_path);
    gchar *new_name = prompt_user_input (data->window, "Rename", basename);
    
    if (new_name && new_name[0] != '\0' && g_strcmp0 (basename, new_name) != 0) {
        gchar *dir = g_path_get_dirname (data->target_path);
        gchar *new_path = g_build_filename (dir, new_name, NULL);
        
        GFile *src = g_file_new_for_path (data->target_path);
        GFile *dst = g_file_new_for_path (new_path);
        
        g_file_move (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
        
        g_object_unref (src);
        g_object_unref (dst);
        g_free (new_path);
        g_free (dir);
        refresh_tree (data->window);
    }
    g_free (new_name);
    g_free (basename);
}

static void
on_delete (GtkMenuItem *item, ContextActionData *data)
{
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (data->window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_QUESTION,
                                                GTK_BUTTONS_YES_NO,
                                                "Are you sure you want to delete '%s'?",
                                                g_path_get_basename (data->target_path));
    make_dialog_transparent (dialog);
    
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_YES) {
        GFile *file = g_file_new_for_path (data->target_path);
        delete_recursive (file, NULL);
        g_object_unref (file);
        refresh_tree (data->window);
    }
    gtk_widget_destroy (dialog);
}

static void
on_cut (GtkMenuItem *item, ContextActionData *data)
{
    current_clip_op = CLIPBOARD_OP_CUT;
    g_free (current_clip_path);
    current_clip_path = g_strdup (data->target_path);
}

static void
on_copy (GtkMenuItem *item, ContextActionData *data)
{
    current_clip_op = CLIPBOARD_OP_COPY;
    g_free (current_clip_path);
    current_clip_path = g_strdup (data->target_path);
}

static void
on_paste (GtkMenuItem *item, ContextActionData *data)
{
    if (!current_clip_path || current_clip_op == CLIPBOARD_OP_NONE) return;
    
    gchar *dir = data->is_dir ? g_strdup (data->target_path) : g_path_get_dirname (data->target_path);
    gchar *basename = g_path_get_basename (current_clip_path);
    gchar *dest_path = g_build_filename (dir, basename, NULL);
    
    GFile *src = g_file_new_for_path (current_clip_path);
    GFile *dst = g_file_new_for_path (dest_path);
    
    if (current_clip_op == CLIPBOARD_OP_COPY) {
        // Simple copy (recursive copying is complex in glib, using basic copy for now)
        g_file_copy (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
    } else if (current_clip_op == CLIPBOARD_OP_CUT) {
        g_file_move (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL);
        current_clip_op = CLIPBOARD_OP_NONE;
        g_free (current_clip_path);
        current_clip_path = NULL;
    }
    
    g_object_unref (src);
    g_object_unref (dst);
    g_free (dest_path);
    g_free (basename);
    g_free (dir);
    refresh_tree (data->window);
}

static void
on_copy_current_path (GtkMenuItem *item, ContextActionData *data)
{
    const gchar *workspace = aether_ide_window_get_workspace_dir (data->window);
    gchar *rel_path = NULL;
    
    if (workspace) {
        GFile *ws_file = g_file_new_for_path (workspace);
        GFile *target_file = g_file_new_for_path (data->target_path);
        rel_path = g_file_get_relative_path (ws_file, target_file);
        g_object_unref (ws_file);
        g_object_unref (target_file);
    }
    
    GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    if (rel_path) {
        gtk_clipboard_set_text (clip, rel_path, -1);
        g_free (rel_path);
    } else {
        gtk_clipboard_set_text (clip, data->target_path, -1);
    }
}

static void
on_copy_absolute_path (GtkMenuItem *item, ContextActionData *data)
{
    GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text (clip, data->target_path, -1);
}

/* ------------------------------------------------------------------ */
/* Menu Construction                                                    */
/* ------------------------------------------------------------------ */

static void
show_context_menu (GtkWidget *tree_view, GdkEventButton *event, AetherIdeWindow *window)
{
    GtkTreePath *path = NULL;
    gboolean is_dir = TRUE;
    gchar *target_path = NULL;
    
    if (event) {
        gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (tree_view),
                                       (gint) event->x, (gint) event->y,
                                       &path, NULL, NULL, NULL);
    } else {
        gtk_tree_view_get_cursor (GTK_TREE_VIEW (tree_view), &path, NULL);
    }
    
    if (path) {
        GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree_view));
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter (model, &iter, path)) {
            // Reusing COLUMN_PATH and COLUMN_IS_DIR from file_explorer.h indirectly
            // Since enum is defined there, we can just use the indexes 2 and 3 safely
            // but it's better to use the constants if they were accessible. 
            // In file_explorer.h they are defined.
            gtk_tree_model_get (model, &iter,
                                COLUMN_PATH, &target_path,
                                COLUMN_IS_DIR, &is_dir,
                                -1);
        }
        gtk_tree_path_free (path);
    } else {
        // Fallback to workspace root
        const gchar *ws = aether_ide_window_get_workspace_dir (window);
        if (ws) {
            target_path = g_strdup (ws);
            is_dir = TRUE;
        } else {
            return; // No workspace, no action
        }
    }
    
    if (!target_path) return;
    
    ContextActionData *data = context_action_data_new (window, target_path, is_dir);
    g_free (target_path);
    
    GtkWidget *menu = gtk_menu_new ();
    g_object_set_data_full (G_OBJECT (menu), "action-data", data, (GDestroyNotify) context_action_data_free);
    
    GtkWidget *item_new_file = gtk_menu_item_new_with_label ("New File");
    GtkWidget *item_new_folder = gtk_menu_item_new_with_label ("New Folder");
    GtkWidget *sep1 = gtk_separator_menu_item_new ();
    GtkWidget *item_cut = gtk_menu_item_new_with_label ("Cut");
    GtkWidget *item_copy = gtk_menu_item_new_with_label ("Copy");
    GtkWidget *item_paste = gtk_menu_item_new_with_label ("Paste");
    GtkWidget *sep2 = gtk_separator_menu_item_new ();
    GtkWidget *item_rename = gtk_menu_item_new_with_label ("Rename");
    GtkWidget *item_delete = gtk_menu_item_new_with_label ("Delete");
    GtkWidget *sep3 = gtk_separator_menu_item_new ();
    GtkWidget *item_copy_rel = gtk_menu_item_new_with_label ("Copy Current Path");
    GtkWidget *item_copy_abs = gtk_menu_item_new_with_label ("Copy Absolute Path");
    
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_new_file);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_new_folder);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep1);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_cut);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_copy);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_paste);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep2);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_rename);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_delete);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep3);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_copy_rel);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), item_copy_abs);
    
    if (current_clip_op == CLIPBOARD_OP_NONE) {
        gtk_widget_set_sensitive (item_paste, FALSE);
    }
    
    g_signal_connect (item_new_file, "activate", G_CALLBACK (on_new_file), data);
    g_signal_connect (item_new_folder, "activate", G_CALLBACK (on_new_folder), data);
    g_signal_connect (item_cut, "activate", G_CALLBACK (on_cut), data);
    g_signal_connect (item_copy, "activate", G_CALLBACK (on_copy), data);
    g_signal_connect (item_paste, "activate", G_CALLBACK (on_paste), data);
    g_signal_connect (item_rename, "activate", G_CALLBACK (on_rename), data);
    g_signal_connect (item_delete, "activate", G_CALLBACK (on_delete), data);
    g_signal_connect (item_copy_rel, "activate", G_CALLBACK (on_copy_current_path), data);
    g_signal_connect (item_copy_abs, "activate", G_CALLBACK (on_copy_absolute_path), data);
    
    gtk_widget_show_all (menu);
    
    if (event) {
        gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
    } else {
        gtk_menu_popup_at_widget (GTK_MENU (menu), tree_view, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Signal Handlers for Tree View                                        */
/* ------------------------------------------------------------------ */

static gboolean
on_tree_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    AetherIdeWindow *window = AETHER_IDE_WINDOW (user_data);
    
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { // Right click
        show_context_menu (widget, event, window);
        return TRUE;
    }
    return FALSE;
}

static gboolean
on_tree_popup_menu (GtkWidget *widget, gpointer user_data)
{
    AetherIdeWindow *window = AETHER_IDE_WINDOW (user_data);
    show_context_menu (widget, NULL, window);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
setup_explorer_context_menu (GtkWidget *tree_view, AetherIdeWindow *window)
{
    g_signal_connect (tree_view, "button-press-event", G_CALLBACK (on_tree_button_press), window);
    g_signal_connect (tree_view, "popup-menu", G_CALLBACK (on_tree_popup_menu), window);
}
