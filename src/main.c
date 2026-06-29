#include <gtk/gtk.h>
#include "vcodex_window.h"
#include "vcodex_window_private.h"
#include "editor_tab.h"
#include "file_explorer.h"
#include "git_manager.h"

static void
on_activate (GtkApplication *app, gpointer user_data)
{
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);
    AetherIdeWindow *window = vcodex_window_new (app);
    gtk_window_present (GTK_WINDOW (window));
}

static void
on_open (GtkApplication  *app,
         GFile          **files,
         gint             n_files,
         const gchar     *hint,
         gpointer         user_data)
{
    GtkWindow *active_window = gtk_application_get_active_window (app);
    AetherIdeWindow *window;
    
    if (active_window) {
        window = AETHER_IDE_WINDOW (active_window);
    } else {
        g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);
        window = vcodex_window_new (app);
    }
    
    for (gint i = 0; i < n_files; i++) {
        GFile *file = files[i];
        gchar *filename = g_file_get_path (file);
        if (filename) {
            if (g_file_test (filename, G_FILE_TEST_IS_DIR)) {
                aether_ide_window_set_workspace_dir (window, filename);
                git_manager_init (filename);
                GtkTreeStore *tree_store = aether_ide_window_get_tree_store (window);
                gtk_tree_store_clear (tree_store);
                populate_tree_model (tree_store, NULL, filename);
            } else {
                gchar *content = NULL;
                if (g_file_get_contents (filename, &content, NULL, NULL)) {
                    gchar *basename = g_path_get_basename (filename);
                    create_editor_tab (window, basename, filename, content);
                    g_free (basename);
                    g_free (content);
                }
            }
            g_free (filename);
        }
    }
    
    gtk_window_present (GTK_WINDOW (window));
}

int
main (int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new ("org.aether.ide", G_APPLICATION_HANDLES_OPEN);
    
    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
    g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);
    
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);
    
    return status;
}
