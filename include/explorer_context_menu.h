#ifndef EXPLORER_CONTEXT_MENU_H
#define EXPLORER_CONTEXT_MENU_H

#include <gtk/gtk.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/*
 * Sets up the context menu for the file explorer tree view.
 * Connects the necessary signals to handle right-clicks and
 * context-menu key presses.
 */
void setup_explorer_context_menu (GtkWidget *tree_view, AetherIdeWindow *window);

G_END_DECLS

#endif /* EXPLORER_CONTEXT_MENU_H */
