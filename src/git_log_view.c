#include "git_log_view.h"
#include "git_manager.h"
#include <gtksourceview/gtksource.h>

struct _AetherGitLogView {
    GtkBox parent_instance;

    GtkWidget *tree_view;
    GtkListStore *list_store;
    
    GtkWidget *diff_view;
    GtkSourceBuffer *diff_buffer;
};

G_DEFINE_TYPE (AetherGitLogView, aether_git_log_view, GTK_TYPE_BOX)

enum {
    COL_SHA,
    COL_SHORT_SHA,
    COL_MESSAGE,
    COL_AUTHOR,
    COL_TIME,
    NUM_COLS
};

static void
on_refresh_clicked (GtkButton *btn, gpointer user_data)
{
    AetherGitLogView *self = AETHER_GIT_LOG_VIEW (user_data);
    aether_git_log_view_refresh (self);
}

static void
on_selection_changed (GtkTreeSelection *selection, gpointer user_data)
{
    AetherGitLogView *self = AETHER_GIT_LOG_VIEW (user_data);
    GtkTreeIter iter;
    GtkTreeModel *model;
    
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
        gchar *sha = NULL;
        gtk_tree_model_get (model, &iter, COL_SHA, &sha, -1);
        if (sha) {
            gchar *diff = git_manager_get_commit_diff (sha);
            if (diff) {
                gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->diff_buffer), diff, -1);
                g_free (diff);
            } else {
                gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->diff_buffer), "No diff available.", -1);
            }
            g_free (sha);
        }
    }
}

static void
aether_git_log_view_class_init (AetherGitLogViewClass *klass)
{
}

static void
aether_git_log_view_init (AetherGitLogView *self)
{
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    
    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start (toolbar, 4);
    gtk_widget_set_margin_end (toolbar, 4);
    gtk_widget_set_margin_top (toolbar, 4);
    gtk_widget_set_margin_bottom (toolbar, 4);
    
    GtkWidget *lbl = gtk_label_new ("<b>Commit History</b>");
    gtk_label_set_use_markup (GTK_LABEL (lbl), TRUE);
    GtkWidget *btn_refresh = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
    g_signal_connect (btn_refresh, "clicked", G_CALLBACK(on_refresh_clicked), self);
    
    gtk_box_pack_start (GTK_BOX (toolbar), lbl, FALSE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (toolbar), btn_refresh, FALSE, FALSE, 0);
    
    /* Paned */
    GtkWidget *paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand (paned, TRUE);
    
    /* Top: Commit List */
    self->list_store = gtk_list_store_new (NUM_COLS,
        G_TYPE_STRING, /* SHA full */
        G_TYPE_STRING, /* SHA short */
        G_TYPE_STRING, /* Message */
        G_TYPE_STRING, /* Author */
        G_TYPE_STRING  /* Time */
    );
    
    self->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->list_store));
    
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (self->tree_view),
                                                 -1, "Commit", gtk_cell_renderer_text_new (), "text", COL_SHORT_SHA, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (self->tree_view),
                                                 -1, "Message", gtk_cell_renderer_text_new (), "text", COL_MESSAGE, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (self->tree_view),
                                                 -1, "Author", gtk_cell_renderer_text_new (), "text", COL_AUTHOR, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (self->tree_view),
                                                 -1, "Time", gtk_cell_renderer_text_new (), "text", COL_TIME, NULL);
    
    GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->tree_view));
    g_signal_connect (sel, "changed", G_CALLBACK(on_selection_changed), self);
    
    GtkWidget *scroll_top = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll_top), self->tree_view);
    
    /* Bottom: Diff View */
    self->diff_buffer = gtk_source_buffer_new (NULL);
    GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
    GtkSourceLanguage *lang = gtk_source_language_manager_get_language (lm, "diff");
    if (lang) {
        gtk_source_buffer_set_language (self->diff_buffer, lang);
    }
    
    self->diff_view = gtk_source_view_new_with_buffer (self->diff_buffer);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (self->diff_view), FALSE);
    gtk_text_view_set_monospace (GTK_TEXT_VIEW (self->diff_view), TRUE);
    
    GtkWidget *scroll_bottom = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll_bottom), self->diff_view);
    
    gtk_paned_pack1 (GTK_PANED (paned), scroll_top, TRUE, FALSE);
    gtk_paned_pack2 (GTK_PANED (paned), scroll_bottom, TRUE, FALSE);
    gtk_paned_set_position (GTK_PANED (paned), 200);
    
    gtk_box_pack_start (GTK_BOX (self), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self), paned, TRUE, TRUE, 0);
}

GtkWidget *
aether_git_log_view_new (void)
{
    return g_object_new (AETHER_TYPE_GIT_LOG_VIEW, NULL);
}

void
aether_git_log_view_refresh (AetherGitLogView *self)
{
    gtk_list_store_clear (self->list_store);
    
    GPtrArray *commits = git_manager_get_log (100);
    if (!commits) return;
    
    for (guint i = 0; i < commits->len; i++) {
        GitCommit *c = g_ptr_array_index (commits, i);
        GtkTreeIter iter;
        
        GDateTime *dt = g_date_time_new_from_unix_local (c->timestamp);
        gchar *time_str = g_date_time_format (dt, "%Y-%m-%d %H:%M");
        g_date_time_unref (dt);
        
        gtk_list_store_append (self->list_store, &iter);
        gtk_list_store_set (self->list_store, &iter,
                            COL_SHA, c->sha,
                            COL_SHORT_SHA, c->short_sha,
                            COL_MESSAGE, c->message,
                            COL_AUTHOR, c->author,
                            COL_TIME, time_str,
                            -1);
        g_free (time_str);
    }
    
    g_ptr_array_free (commits, TRUE);
}
