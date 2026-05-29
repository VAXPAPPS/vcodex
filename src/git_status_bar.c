#include "git_status_bar.h"
#include "git_manager.h"

struct _AetherGitStatusBar {
    GtkBox parent_instance;

    GtkWidget *branch_button;
    GtkWidget *sync_button;
    
    GtkWidget *popover;
};

G_DEFINE_TYPE (AetherGitStatusBar, aether_git_status_bar, GTK_TYPE_BOX)

/* Forward declaration */
void aether_git_status_bar_update (AetherGitStatusBar *self);

static void
on_status_changed (GObject *manager, gpointer user_data)
{
    AetherGitStatusBar *self = AETHER_GIT_STATUS_BAR (user_data);
    aether_git_status_bar_update (self);
}

static void
on_sync_clicked (GtkButton *btn, gpointer user_data)
{
    g_print ("Syncing with remote...\n");
    if (git_manager_get_behind() > 0) git_manager_pull ();
    if (git_manager_get_ahead() > 0) git_manager_push ();
}

static void
aether_git_status_bar_class_init (AetherGitStatusBarClass *klass)
{
}

static void
aether_git_status_bar_init (AetherGitStatusBar *self)
{
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing (GTK_BOX (self), 0);
    
    /* Branch Button */
    self->branch_button = gtk_button_new_with_label ("🌿 main");
    gtk_widget_set_tooltip_text (self->branch_button, "Git Branch");
    
    /* Remove padding to make it look like a status bar item */
    GtkStyleContext *ctx = gtk_widget_get_style_context (self->branch_button);
    gtk_style_context_add_class (ctx, "flat");
    
    /* Sync Button */
    self->sync_button = gtk_button_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text (self->sync_button, "Synchronize Changes");
    ctx = gtk_widget_get_style_context (self->sync_button);
    gtk_style_context_add_class (ctx, "flat");
    g_signal_connect (self->sync_button, "clicked", G_CALLBACK(on_sync_clicked), self);
    
    gtk_box_pack_start (GTK_BOX (self), self->branch_button, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self), self->sync_button, FALSE, FALSE, 0);
    
    /* Popover for branch selection */
    self->popover = gtk_popover_new (self->branch_button);
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start (box, 12);
    gtk_widget_set_margin_end (box, 12);
    gtk_widget_set_margin_top (box, 12);
    gtk_widget_set_margin_bottom (box, 12);
    
    GtkWidget *lbl = gtk_label_new ("Switch Branch (TODO)");
    gtk_box_pack_start (GTK_BOX (box), lbl, FALSE, FALSE, 0);
    gtk_widget_show_all (box);
    gtk_container_add (GTK_CONTAINER (self->popover), box);
    
    /* Listen to branch button clicks to show popover */
    g_signal_connect_swapped (self->branch_button, "clicked", G_CALLBACK (gtk_popover_popup), self->popover);
    
    /* Connect to signals */
    GObject *hub = git_manager_get_instance ();
    if (hub) {
        g_signal_connect (hub, "status-changed", G_CALLBACK(on_status_changed), self);
    }
}

GtkWidget *
aether_git_status_bar_new (void)
{
    return g_object_new (AETHER_TYPE_GIT_STATUS_BAR, NULL);
}

void
aether_git_status_bar_update (AetherGitStatusBar *self)
{
    if (!git_manager_is_repo()) {
        gtk_widget_hide (GTK_WIDGET (self));
        return;
    }
    gtk_widget_show (GTK_WIDGET (self));
    
    gchar *branch = git_manager_get_current_branch ();
    if (branch) {
        gchar *label = g_strdup_printf ("🌿 %s", branch);
        gtk_button_set_label (GTK_BUTTON (self->branch_button), label);
        g_free (label);
        g_free (branch);
    } else {
        gtk_button_set_label (GTK_BUTTON (self->branch_button), "🌿 (detached)");
    }
    
    /* Check ahead/behind */
    gint ahead = git_manager_get_ahead();
    gint behind = git_manager_get_behind();
    
    if (ahead > 0 || behind > 0) {
        gtk_widget_show (self->sync_button);
    } else {
        gtk_widget_hide (self->sync_button);
    }
}
