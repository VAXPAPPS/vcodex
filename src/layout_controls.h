#ifndef LAYOUT_CONTROLS_H
#define LAYOUT_CONTROLS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/*
 * Creates a GtkBox containing three toggle buttons.
 * These buttons control the visibility of the sidebar, bottom panel,
 * and the right AI panel respectively.
 * The buttons use custom cairo drawing to render the layout icons.
 */
GtkWidget *layout_controls_new (GtkWidget *sidebar_wrapper,
                                GtkWidget *sidebar_stack,
                                GtkWidget *bottom_panel,
                                GtkWidget *ai_panel);

G_END_DECLS

#endif /* LAYOUT_CONTROLS_H */
