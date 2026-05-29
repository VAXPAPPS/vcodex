#ifndef GIT_LOG_VIEW_H
#define GIT_LOG_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define AETHER_TYPE_GIT_LOG_VIEW (aether_git_log_view_get_type ())
G_DECLARE_FINAL_TYPE (AetherGitLogView, aether_git_log_view, AETHER, GIT_LOG_VIEW, GtkBox)

GtkWidget *aether_git_log_view_new (void);

void aether_git_log_view_refresh (AetherGitLogView *self);

G_END_DECLS

#endif /* GIT_LOG_VIEW_H */
