#ifndef vcodex_WINDOW_H
#define vcodex_WINDOW_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define AETHER_TYPE_IDE_WINDOW (vcodex_window_get_type())
G_DECLARE_FINAL_TYPE (AetherIdeWindow, vcodex_window, AETHER, IDE_WINDOW, GtkApplicationWindow)

AetherIdeWindow *vcodex_window_new (GtkApplication *app);

G_END_DECLS

#endif /* vcodex_WINDOW_H */
