#ifndef AETHER_IDE_WINDOW_H
#define AETHER_IDE_WINDOW_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define AETHER_TYPE_IDE_WINDOW (aether_ide_window_get_type())
G_DECLARE_FINAL_TYPE (AetherIdeWindow, aether_ide_window, AETHER, IDE_WINDOW, GtkApplicationWindow)

AetherIdeWindow *aether_ide_window_new (GtkApplication *app);

G_END_DECLS

#endif /* AETHER_IDE_WINDOW_H */
