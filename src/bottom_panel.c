/* bottom_panel.c — Bottom panel with multiple tool tabs */

#include "bottom_panel.h"
#include "terminal_panel.h"

static GtkWidget *
create_placeholder (const gchar *text)
{
    GtkWidget *label = gtk_label_new (text);
    gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
    
    // Wrap in a scrolled window just in case, similar to other panels
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scroll), label);
    return scroll;
}

GtkWidget *
bottom_panel_new (void)
{
    GtkWidget *notebook = gtk_notebook_new ();
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);

    // 1. Problems
    GtkWidget *problems_page = create_placeholder ("No problems have been detected in the workspace.");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), problems_page, gtk_label_new ("Problems"));

    // 2. Output
    GtkWidget *output_page = create_placeholder ("Output will appear here.");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), output_page, gtk_label_new ("Output"));

    // 3. Debug Console
    GtkWidget *debug_page = create_placeholder ("Please start a debug session to evaluate expressions.");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), debug_page, gtk_label_new ("Debug Console"));

    // 4. Terminal (Uses the actual terminal implementation)
    GtkWidget *terminal_page = terminal_panel_new ();
    gint term_idx = gtk_notebook_append_page (GTK_NOTEBOOK (notebook), terminal_page, gtk_label_new ("Terminal"));

    // 5. Ports
    GtkWidget *ports_page = create_placeholder ("No forwarded ports.");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), ports_page, gtk_label_new ("Ports"));

    // Make Terminal the default selected tab
    gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), term_idx);

    gtk_widget_show_all (notebook);
    return notebook;
}
