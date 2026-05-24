#include <gtk/gtk.h>
#include "aether_ide_window.h"

static void
on_activate (GtkApplication *app, gpointer user_data)
{
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);
    AetherIdeWindow *window = aether_ide_window_new (app);
    gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new ("org.aether.ide", G_APPLICATION_DEFAULT_FLAGS);
    
    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
    
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    
    return status;
}
