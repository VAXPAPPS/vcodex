#ifndef BOTTOM_PANEL_H
#define BOTTOM_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Creates the bottom panel notebook which contains tabs for:
 * Problems, Output, Debug Console, Terminal, and Ports.
 */
GtkWidget *bottom_panel_new (void);

G_END_DECLS

#endif /* BOTTOM_PANEL_H */
