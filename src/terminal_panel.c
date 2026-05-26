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

static int terminal_id_counter = 1;

static void
on_tab_close_clicked (GtkButton *button, GtkWidget *child)
{
    GtkWidget *notebook = gtk_widget_get_parent (child);
    if (GTK_IS_NOTEBOOK (notebook)) {
        gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);
        if (page_num != -1) {
            gtk_notebook_remove_page (GTK_NOTEBOOK (notebook), page_num);
        }
    }
}

static void
on_terminal_child_exited (VteTerminal *terminal, gint status, GtkWidget *child)
{
    on_tab_close_clicked (NULL, child);
}

static void
add_terminal_tab (GtkNotebook *notebook)
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

    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll), terminal);
    gtk_widget_show_all (scroll);

    /* Tab label with title and close button */
    GtkWidget *tab_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gchar *tab_title = g_strdup_printf ("Terminal %d", terminal_id_counter++);
    GtkWidget *label = gtk_label_new (tab_title);
    g_free (tab_title);
    
    GtkWidget *close_btn = gtk_button_new_from_icon_name ("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief (GTK_BUTTON (close_btn), GTK_RELIEF_NONE);
    GtkStyleContext *context = gtk_widget_get_style_context (close_btn);
    gtk_style_context_add_class (context, "flat");
    gtk_style_context_add_class (context, "circular");
    
    gtk_box_pack_start (GTK_BOX (tab_box), label, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all (tab_box);

    g_signal_connect (close_btn, "clicked", G_CALLBACK (on_tab_close_clicked), scroll);
    g_signal_connect (terminal, "child-exited", G_CALLBACK (on_terminal_child_exited), scroll);

    gint page_idx = gtk_notebook_append_page (notebook, scroll, tab_box);
    gtk_notebook_set_tab_reorderable (notebook, scroll, TRUE);
    gtk_notebook_set_current_page (notebook, page_idx);
}

static void
on_add_terminal_clicked (GtkButton *button, GtkNotebook *notebook)
{
    add_terminal_tab (notebook);
}

GtkWidget *
terminal_panel_new (void)
{
    GtkWidget *notebook = gtk_notebook_new ();
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_RIGHT);
    
    GtkWidget *add_btn = gtk_button_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text (add_btn, "New Terminal");
    gtk_button_set_relief (GTK_BUTTON (add_btn), GTK_RELIEF_NONE);
    
    g_signal_connect (add_btn, "clicked", G_CALLBACK (on_add_terminal_clicked), notebook);
    
    gtk_notebook_set_action_widget (GTK_NOTEBOOK (notebook), add_btn, GTK_PACK_END);
    gtk_widget_show_all (add_btn);
    
    add_terminal_tab (GTK_NOTEBOOK (notebook));
    
    return notebook;
}
