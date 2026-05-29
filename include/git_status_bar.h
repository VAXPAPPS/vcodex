#ifndef GIT_STATUS_BAR_H
#define GIT_STATUS_BAR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define AETHER_TYPE_GIT_STATUS_BAR (aether_git_status_bar_get_type ())
G_DECLARE_FINAL_TYPE (AetherGitStatusBar, aether_git_status_bar, AETHER, GIT_STATUS_BAR, GtkBox)

GtkWidget *aether_git_status_bar_new (void);

/* Manually force update of the status bar text */
void aether_git_status_bar_update (AetherGitStatusBar *self);

G_END_DECLS

#endif /* GIT_STATUS_BAR_H */
