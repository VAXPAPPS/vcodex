/* terminal_panel.c — VTE terminal widget setup and event handling */

#include "terminal_panel.h"

/* ------------------------------------------------------------------ */
/* Copy/Paste helpers                                                   */
/* ------------------------------------------------------------------ */

static void
do_terminal_copy (GtkWidget *item, VteTerminal *terminal)
{
    vte_terminal_copy_clipboard_format (terminal, VTE_FORMAT_TEXT);
}

static void
do_terminal_paste (GtkWidget *item, VteTerminal *terminal)
{
    vte_terminal_paste_clipboard (terminal);
}

/* ------------------------------------------------------------------ */
/* Event handlers                                                       */
/* ------------------------------------------------------------------ */

gboolean
on_terminal_button_press (GtkWidget *widget, GdkEventButton *event,
                          gpointer user_data)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;

    GtkWidget *menu = gtk_menu_new ();

    GtkWidget *copy_item  = gtk_menu_item_new_with_label ("Copy (Ctrl+Shift+C)");
    GtkWidget *paste_item = gtk_menu_item_new_with_label ("Paste (Ctrl+Shift+V)");

    g_signal_connect (copy_item,  "activate", G_CALLBACK (do_terminal_copy),  widget);
    g_signal_connect (paste_item, "activate", G_CALLBACK (do_terminal_paste), widget);

    gtk_menu_shell_append (GTK_MENU_SHELL (menu), copy_item);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), paste_item);

    gtk_widget_show_all (menu);
    gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
    return TRUE;
}

gboolean
on_terminal_key_press (GtkWidget *widget, GdkEventKey *event,
                       gpointer user_data)
{
    VteTerminal *terminal  = VTE_TERMINAL (widget);
    guint        modifiers = event->state & gtk_accelerator_get_default_mod_mask ();

    if (modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (event->keyval == GDK_KEY_C || event->keyval == GDK_KEY_c) {
            vte_terminal_copy_clipboard_format (terminal, VTE_FORMAT_TEXT);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_V || event->keyval == GDK_KEY_v) {
            vte_terminal_paste_clipboard (terminal);
            return TRUE;
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Factory                                                              */
/* ------------------------------------------------------------------ */

GtkWidget *
terminal_panel_new (void)
{
    GtkWidget *terminal = vte_terminal_new ();

    /* Transparent background */
    GdkRGBA bg = {0.0, 0.0, 0.0, 0.0};
    vte_terminal_set_color_background (VTE_TERMINAL (terminal), &bg);

    /* Wire up keyboard and mouse events */
    g_signal_connect (terminal, "key-press-event",
                      G_CALLBACK (on_terminal_key_press), NULL);
    g_signal_connect (terminal, "button-press-event",
                      G_CALLBACK (on_terminal_button_press), NULL);

    /* Spawn the user's default shell */
    gchar **envp    = g_get_environ ();
    gchar **command = g_new0 (gchar *, 2);
    command[0] = g_strdup (g_environ_getenv (envp, "SHELL"));
    if (!command[0])
        command[0] = g_strdup ("/bin/bash");

    vte_terminal_spawn_async (VTE_TERMINAL (terminal),
                              VTE_PTY_DEFAULT,
                              NULL,       /* working directory */
                              command,
                              NULL,       /* environment */
                              G_SPAWN_DEFAULT,
                              NULL, NULL, NULL,
                              -1,         /* timeout */
                              NULL, NULL, NULL);
    g_strfreev (command);
    g_strfreev (envp);

    /* Wrap in a scrolled window and return */
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll), terminal);
    return scroll;
}
