#include "git_panel.h"
#include "git_manager.h"

struct _AetherGitPanel {
    GtkBox parent_instance;

    GtkWidget *commit_text_view;
    GtkWidget *commit_button;
    GtkWidget *refresh_button;
    
    GtkWidget *tree_view;
    GtkTreeStore *tree_store;
};

G_DEFINE_TYPE (AetherGitPanel, aether_git_panel, GTK_TYPE_BOX)

enum {
    COL_ICON,
    COL_NAME,
    COL_STATUS_TEXT,
    COL_PATH,
    COL_IS_HEADER,
    NUM_COLS
};

/* Forward declaration */
void aether_git_panel_refresh (AetherGitPanel *self);

static void
on_status_changed (GObject *manager, gpointer user_data)
{
    AetherGitPanel *self = AETHER_GIT_PANEL (user_data);
    aether_git_panel_refresh (self);
}

static void
on_commit_clicked (GtkButton *btn, gpointer user_data)
{
    AetherGitPanel *self = AETHER_GIT_PANEL (user_data);
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->commit_text_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
    
    if (text && strlen(text) > 0) {
        if (git_manager_commit (text)) {
            gtk_text_buffer_set_text (buf, "", -1);
            g_print ("Commit successful!\n");
        } else {
            g_printerr ("Commit failed (maybe nothing to commit or no repo).\n");
        }
    }
    g_free (text);
}

static void
on_refresh_clicked (GtkButton *btn, gpointer user_data)
{
    AetherGitPanel *self = AETHER_GIT_PANEL (user_data);
    aether_git_panel_refresh (self);
}

static void
aether_git_panel_class_init (AetherGitPanelClass *klass)
{
}

static void
aether_git_panel_init (AetherGitPanel *self)
{
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_box_set_spacing (GTK_BOX (self), 4);
    
    /* Top Action Bar */
    GtkWidget *action_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_top (action_box, 4);
    gtk_widget_set_margin_start (action_box, 4);
    gtk_widget_set_margin_end (action_box, 4);
    
    GtkWidget *lbl = gtk_label_new ("Source Control");
    gtk_widget_set_halign (lbl, GTK_ALIGN_START);
    gtk_label_set_markup (GTK_LABEL (lbl), "<b>Source Control</b>");
    
    self->refresh_button = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text (self->refresh_button, "Refresh");
    g_signal_connect (self->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);
    
    self->commit_button = gtk_button_new_from_icon_name ("object-select-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text (self->commit_button, "Commit");
    g_signal_connect (self->commit_button, "clicked", G_CALLBACK(on_commit_clicked), self);
    
    gtk_box_pack_start (GTK_BOX (action_box), lbl, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (action_box), self->refresh_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (action_box), self->commit_button, FALSE, FALSE, 0);
    
    /* Commit Message Area */
    GtkWidget *scroll_msg = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll_msg), GTK_SHADOW_IN);
    gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll_msg), 60);
    gtk_widget_set_margin_start (scroll_msg, 4);
    gtk_widget_set_margin_end (scroll_msg, 4);
    
    self->commit_text_view = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (self->commit_text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin (GTK_TEXT_VIEW (self->commit_text_view), 4);
    gtk_text_view_set_right_margin (GTK_TEXT_VIEW (self->commit_text_view), 4);
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->commit_text_view));
    gtk_text_buffer_set_text (buf, "Message", -1);
    
    gtk_container_add (GTK_CONTAINER (scroll_msg), self->commit_text_view);
    
    /* Tree View for changes */
    self->tree_store = gtk_tree_store_new (NUM_COLS,
        G_TYPE_STRING,   /* Icon */
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Status char (M, A, D) */
        G_TYPE_STRING,   /* Path */
        G_TYPE_BOOLEAN   /* Is Header */
    );
    
    self->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->tree_store));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->tree_view), FALSE);
    
    GtkTreeViewColumn *col = gtk_tree_view_column_new ();
    
    GtkCellRenderer *rend_icon = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_column_pack_start (col, rend_icon, FALSE);
    gtk_tree_view_column_add_attribute (col, rend_icon, "icon-name", COL_ICON);
    
    GtkCellRenderer *rend_name = gtk_cell_renderer_text_new ();
    gtk_tree_view_column_pack_start (col, rend_name, TRUE);
    gtk_tree_view_column_add_attribute (col, rend_name, "text", COL_NAME);
    
    GtkCellRenderer *rend_status = gtk_cell_renderer_text_new ();
    g_object_set (rend_status, "foreground", "gray", NULL);
    gtk_tree_view_column_pack_end (col, rend_status, FALSE);
    gtk_tree_view_column_add_attribute (col, rend_status, "text", COL_STATUS_TEXT);
    
    gtk_tree_view_append_column (GTK_TREE_VIEW (self->tree_view), col);
    
    GtkWidget *scroll_tree = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_vexpand (scroll_tree, TRUE);
    gtk_container_add (GTK_CONTAINER (scroll_tree), self->tree_view);
    
    gtk_box_pack_start (GTK_BOX (self), action_box, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self), scroll_msg, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self), scroll_tree, TRUE, TRUE, 0);
    
    /* Connect to signals */
    GObject *hub = git_manager_get_instance ();
    if (hub) {
        g_signal_connect (hub, "status-changed", G_CALLBACK(on_status_changed), self);
    }
}

GtkWidget *
aether_git_panel_new (void)
{
    return g_object_new (AETHER_TYPE_GIT_PANEL, NULL);
}

void
aether_git_panel_refresh (AetherGitPanel *self)
{
    gtk_tree_store_clear (self->tree_store);
    
    GPtrArray *status_arr = git_manager_get_status ();
    if (!status_arr) return;
    
    /* Create Headers */
    GtkTreeIter staged_iter, changes_iter;
    gint staged_count = 0, changes_count = 0;
    
    gtk_tree_store_append (self->tree_store, &staged_iter, NULL);
    gtk_tree_store_set (self->tree_store, &staged_iter,
                        COL_NAME, "Staged Changes",
                        COL_IS_HEADER, TRUE,
                        -1);
                        
    gtk_tree_store_append (self->tree_store, &changes_iter, NULL);
    gtk_tree_store_set (self->tree_store, &changes_iter,
                        COL_NAME, "Changes",
                        COL_IS_HEADER, TRUE,
                        -1);
                        
    for (guint i = 0; i < status_arr->len; i++) {
        GitFileEntry *entry = g_ptr_array_index (status_arr, i);
        
        gchar *basename = g_path_get_basename (entry->path);
        gchar *status_text = "?";
        GtkTreeIter *parent_iter = &changes_iter;
        
        if (entry->status & GIT_FILE_STATUS_STAGED) {
            parent_iter = &staged_iter;
            staged_count++;
        } else {
            changes_count++;
        }
        
        if (entry->status & GIT_FILE_STATUS_DELETED) status_text = "D";
        else if (entry->status & GIT_FILE_STATUS_MODIFIED) status_text = "M";
        else if (entry->status & GIT_FILE_STATUS_UNTRACKED) status_text = "U";
        else if (entry->status & GIT_FILE_STATUS_RENAMED) status_text = "R";
        
        GtkTreeIter child;
        gtk_tree_store_append (self->tree_store, &child, parent_iter);
        gtk_tree_store_set (self->tree_store, &child,
                            COL_ICON, "text-x-generic-symbolic",
                            COL_NAME, basename,
                            COL_STATUS_TEXT, status_text,
                            COL_PATH, entry->path,
                            COL_IS_HEADER, FALSE,
                            -1);
        g_free (basename);
    }
    
    /* Update headers with counts */
    gchar *staged_title = g_strdup_printf ("Staged Changes (%d)", staged_count);
    gchar *changes_title = g_strdup_printf ("Changes (%d)", changes_count);
    
    gtk_tree_store_set (self->tree_store, &staged_iter, COL_NAME, staged_title, -1);
    gtk_tree_store_set (self->tree_store, &changes_iter, COL_NAME, changes_title, -1);
    
    g_free (staged_title);
    g_free (changes_title);
    
    gtk_tree_view_expand_all (GTK_TREE_VIEW (self->tree_view));
    
    g_ptr_array_free (status_arr, TRUE);
}
