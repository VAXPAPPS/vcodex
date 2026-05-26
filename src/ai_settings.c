#include "ai_settings.h"
#include <glib.h>

static AiSettings current_settings = {
    .provider = AI_PROVIDER_LOCAL,
    .api_key = NULL,
    .base_url = NULL,
    .model_name = NULL
};

static gchar *
get_settings_file_path (void)
{
    const gchar *config_dir = g_get_user_config_dir ();
    gchar *vcodex_dir = g_build_filename (config_dir, "vcodex", NULL);
    g_mkdir_with_parents (vcodex_dir, 0755);
    gchar *path = g_build_filename (vcodex_dir, "settings.ini", NULL);
    g_free (vcodex_dir);
    return path;
}

void
ai_settings_init (void)
{
    gchar *path = get_settings_file_path ();
    GKeyFile *key_file = g_key_file_new ();

    if (g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, NULL)) {
        current_settings.provider = g_key_file_get_integer (key_file, "AI", "provider", NULL);
        current_settings.api_key = g_key_file_get_string (key_file, "AI", "api_key", NULL);
        current_settings.base_url = g_key_file_get_string (key_file, "AI", "base_url", NULL);
        current_settings.model_name = g_key_file_get_string (key_file, "AI", "model_name", NULL);
    } else {
        // Defaults
        current_settings.provider = AI_PROVIDER_LOCAL;
        current_settings.api_key = g_strdup ("");
        current_settings.base_url = g_strdup ("http://localhost:11434");
        current_settings.model_name = g_strdup ("llama3");
    }

    // Fallbacks if null
    if (!current_settings.api_key) current_settings.api_key = g_strdup ("");
    if (!current_settings.base_url) current_settings.base_url = g_strdup ("http://localhost:11434");
    if (!current_settings.model_name) current_settings.model_name = g_strdup ("llama3");

    g_key_file_free (key_file);
    g_free (path);
}

static void
ai_settings_save (void)
{
    gchar *path = get_settings_file_path ();
    GKeyFile *key_file = g_key_file_new ();

    g_key_file_set_integer (key_file, "AI", "provider", current_settings.provider);
    g_key_file_set_string (key_file, "AI", "api_key", current_settings.api_key ? current_settings.api_key : "");
    g_key_file_set_string (key_file, "AI", "base_url", current_settings.base_url ? current_settings.base_url : "");
    g_key_file_set_string (key_file, "AI", "model_name", current_settings.model_name ? current_settings.model_name : "");

    g_key_file_save_to_file (key_file, path, NULL);
    g_key_file_free (key_file);
    g_free (path);
}

const AiSettings*
ai_settings_get (void)
{
    return &current_settings;
}

static void
on_provider_changed (GtkComboBox *widget, gpointer user_data)
{
    GtkWidget **entries = (GtkWidget **) user_data;
    GtkWidget *url_entry = entries[0];
    
    gint active = gtk_combo_box_get_active (widget);
    if (active == AI_PROVIDER_LOCAL) {
        gtk_entry_set_text (GTK_ENTRY (url_entry), "http://localhost:11434");
    } else if (active == AI_PROVIDER_OPENAI) {
        gtk_entry_set_text (GTK_ENTRY (url_entry), "https://api.openai.com/v1");
    } else if (active == AI_PROVIDER_ANTHROPIC) {
        gtk_entry_set_text (GTK_ENTRY (url_entry), "https://api.anthropic.com/v1");
    } else if (active == AI_PROVIDER_GEMINI) {
        gtk_entry_set_text (GTK_ENTRY (url_entry), "https://generativelanguage.googleapis.com/v1beta");
    }
}

void
ai_settings_show_dialog (GtkWindow *parent)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons ("AI Agent Settings",
                                                     parent,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Save", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 10);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 10);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 15);

    // Provider
    GtkWidget *provider_combo = gtk_combo_box_text_new ();
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (provider_combo), "OpenAI");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (provider_combo), "Anthropic");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (provider_combo), "Google Gemini");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (provider_combo), "Local (Ollama)");
    gtk_combo_box_set_active (GTK_COMBO_BOX (provider_combo), current_settings.provider);

    // API Key
    GtkWidget *api_key_entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (api_key_entry), current_settings.api_key);
    gtk_entry_set_visibility (GTK_ENTRY (api_key_entry), FALSE);

    // Base URL
    GtkWidget *url_entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (url_entry), current_settings.base_url);

    // Model Name
    GtkWidget *model_entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (model_entry), current_settings.model_name);

    // Connect combo change
    GtkWidget *entries[] = { url_entry };
    g_signal_connect (provider_combo, "changed", G_CALLBACK (on_provider_changed), entries);

    // Layout
    gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Provider:"), 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), provider_combo, 1, 0, 1, 1);

    gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("API Key:"), 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), api_key_entry, 1, 1, 1, 1);

    gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Base URL:"), 0, 2, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), url_entry, 1, 2, 1, 1);

    gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("Model Name:"), 0, 3, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), model_entry, 1, 3, 1, 1);

    gtk_widget_set_hexpand (provider_combo, TRUE);
    gtk_widget_set_hexpand (api_key_entry, TRUE);
    gtk_widget_set_hexpand (url_entry, TRUE);
    gtk_widget_set_hexpand (model_entry, TRUE);

    gtk_box_pack_start (GTK_BOX (content_area), grid, TRUE, TRUE, 0);
    gtk_widget_show_all (dialog);

    // Apply transparency
    GtkStyleContext *ctx = gtk_widget_get_style_context (dialog);
    gtk_style_context_add_class (ctx, "transparent-dialog");

    gint result = gtk_dialog_run (GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        current_settings.provider = gtk_combo_box_get_active (GTK_COMBO_BOX (provider_combo));
        
        g_free (current_settings.api_key);
        current_settings.api_key = g_strdup (gtk_entry_get_text (GTK_ENTRY (api_key_entry)));
        
        g_free (current_settings.base_url);
        current_settings.base_url = g_strdup (gtk_entry_get_text (GTK_ENTRY (url_entry)));
        
        g_free (current_settings.model_name);
        current_settings.model_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (model_entry)));

        ai_settings_save ();
    }

    gtk_widget_destroy (dialog);
}
