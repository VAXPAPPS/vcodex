#ifndef GIT_PANEL_H
#define GIT_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define AETHER_TYPE_GIT_PANEL (aether_git_panel_get_type ())
G_DECLARE_FINAL_TYPE (AetherGitPanel, aether_git_panel, AETHER, GIT_PANEL, GtkBox)

GtkWidget *aether_git_panel_new (void);

/* Forces a refresh of the lists from the git_manager */
void aether_git_panel_refresh (AetherGitPanel *self);

G_END_DECLS

#endif /* GIT_PANEL_H */
