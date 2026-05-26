#ifndef TERMINAL_PANEL_H
#define TERMINAL_PANEL_H

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

/*
 * Creates a fully-configured VteTerminal widget connected to the user's
 * default shell and returns a GtkScrolledWindow that wraps it.
 * The terminal is also wired up for Ctrl+Shift+C/V clipboard shortcuts
 * and a right-click context menu.
 */
GtkWidget *terminal_panel_new (void);

/* Right-click context menu handler (copy / paste). */
gboolean on_terminal_button_press (GtkWidget      *widget,
                                   GdkEventButton *event,
                                   gpointer        user_data);

/* Keyboard shortcut handler (Ctrl+Shift+C / Ctrl+Shift+V). */
gboolean on_terminal_key_press    (GtkWidget   *widget,
                                   GdkEventKey *event,
                                   gpointer     user_data);

G_END_DECLS

#endif /* TERMINAL_PANEL_H */
