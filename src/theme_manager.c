#include "theme_manager.h"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static GtkCssProvider *dynamic_provider = NULL;
static GFileMonitor *file_monitor = NULL;

static void parse_color(const char *vaxp_color, char *css_color, size_t max_len) {
    if (vaxp_color[0] == '#' && strlen(vaxp_color) >= 9) {
        unsigned int a, r, g, b;
        if (sscanf(vaxp_color, "#%02x%02x%02x%02x", &a, &r, &g, &b) == 4) {
            snprintf(css_color, max_len, "rgba(%u, %u, %u, %.3f)", r, g, b, a / 255.0);
            return;
        }
    } else if (vaxp_color[0] == '#' && strlen(vaxp_color) >= 7) {
        strncpy(css_color, vaxp_color, max_len);
        return;
    }
    strncpy(css_color, vaxp_color, max_len);
}

static void apply_theme(const char *bg_color, const char *fg_color) {
    if (!dynamic_provider) {
        dynamic_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(dynamic_provider),
            GTK_STYLE_PROVIDER_PRIORITY_USER
        );
    }

    char css[2048];
    snprintf(css, sizeof(css),
        "window.background, headerbar, dialog, window.dialog { background-color: %s; background-image: none; }\n"
        "label { color: %s; }\n"
        ".title-label { color: %s; }\n"
        ".subtitle-label { color: %s; }\n"
        "textview text { color: %s; background-color: transparent; }\n",
        bg_color, fg_color, fg_color, fg_color, fg_color);

    gtk_css_provider_load_from_data(dynamic_provider, css, -1, NULL);
}

static void load_vaxp_settings(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char line[256];
    char bg_vaxp[64] = "#FF000000";
    char fg_vaxp[64] = "#FFFFFFFF";

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "background_color:")) {
            char *start = strchr(line, '"');
            if (start) {
                char *end = strchr(start + 1, '"');
                if (end) {
                    *end = '\0';
                    strncpy(bg_vaxp, start + 1, sizeof(bg_vaxp) - 1);
                    bg_vaxp[sizeof(bg_vaxp) - 1] = '\0';
                }
            }
        } else if (strstr(line, "text_color:")) {
            char *start = strchr(line, '"');
            if (start) {
                char *end = strchr(start + 1, '"');
                if (end) {
                    *end = '\0';
                    strncpy(fg_vaxp, start + 1, sizeof(fg_vaxp) - 1);
                    fg_vaxp[sizeof(fg_vaxp) - 1] = '\0';
                }
            }
        }
    }
    fclose(f);

    char bg_css[64], fg_css[64];
    parse_color(bg_vaxp, bg_css, sizeof(bg_css));
    parse_color(fg_vaxp, fg_css, sizeof(fg_css));

    apply_theme(bg_css, fg_css);
}

static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor; (void)other_file; (void)user_data;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event_type == G_FILE_MONITOR_EVENT_CREATED) {
        char *path = g_file_get_path(file);
        if (path) {
            load_vaxp_settings(path);
            g_free(path);
        }
    }
}

void theme_manager_init(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/.config/venom/settings.vaxp", home);

    load_vaxp_settings(filepath);

    GFile *file = g_file_new_for_path(filepath);
    file_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (file_monitor) {
        g_signal_connect(file_monitor, "changed", G_CALLBACK(on_file_changed), NULL);
    }
    g_object_unref(file);
}
