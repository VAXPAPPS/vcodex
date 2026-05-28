/*
 * ai_settings.c — AI provider settings with secure key storage
 *
 * Key storage strategy (in order of preference):
 *   1. GNOME Keyring (libsecret) — secure, system-integrated
 *   2. ~/.config/vcodex/settings.ini (chmod 600) — fallback
 *   3. Environment variables (OPENAI_API_KEY, etc.) — read-only fallback
 */

#include "ai_settings.h"
#include "ai_provider.h"
#include "ai_http.h"
#include <glib.h>
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* libsecret optional integration                                       */
/* ------------------------------------------------------------------ */

#ifdef HAVE_LIBSECRET
#  include <libsecret/secret.h>
static const SecretSchema g_key_schema = {
    "dev.vcodex.aether-ide.api-key",
    SECRET_SCHEMA_NONE,
    {
        { "provider", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, 0 }
    }
};
#endif

/* ------------------------------------------------------------------ */
/* Global state                                                         */
/* ------------------------------------------------------------------ */

static AiSettings g_settings = {
    .provider    = AI_PROVIDER_OLLAMA,
    .api_key     = NULL,
    .base_url    = NULL,
    .model_name  = NULL,
    .max_tokens  = 0,
    .temperature = 0.7
};

/* ------------------------------------------------------------------ */
/* File-based persistence                                               */
/* ------------------------------------------------------------------ */

static gchar *
get_config_path (void)
{
    const gchar *config_dir = g_get_user_config_dir ();
    gchar *dir  = g_build_filename (config_dir, "vcodex", NULL);
    g_mkdir_with_parents (dir, 0700); /* restrictive permissions */
    gchar *path = g_build_filename (dir, "settings.ini", NULL);
    g_free (dir);
    return path;
}

/* ------------------------------------------------------------------ */
/* libsecret key storage                                                */
/* ------------------------------------------------------------------ */

static void
keyring_store_key (AiProviderType type, const gchar *key)
{
#ifdef HAVE_LIBSECRET
    const gchar *provider_name = ai_provider_get_name (type);
    GError *err = NULL;
    secret_password_store_sync (&g_key_schema,
                                SECRET_COLLECTION_DEFAULT,
                                "AetherIDE API Key",
                                key,
                                NULL, &err,
                                "provider", provider_name,
                                NULL);
    if (err) {
        g_warning ("Keyring store failed: %s", err->message);
        g_error_free (err);
    }
#else
    (void) type; (void) key;
#endif
}

static gchar *
keyring_load_key (AiProviderType type)
{
#ifdef HAVE_LIBSECRET
    const gchar *provider_name = ai_provider_get_name (type);
    GError *err = NULL;
    gchar *secret = secret_password_lookup_sync (&g_key_schema,
                                                  NULL, &err,
                                                  "provider", provider_name,
                                                  NULL);
    if (err) {
        g_warning ("Keyring lookup failed: %s", err->message);
        g_error_free (err);
        return NULL;
    }
    return secret; /* caller frees with secret_password_free() or g_free() */
#else
    (void) type;
    return NULL;
#endif
}

/* ------------------------------------------------------------------ */
/* Public: effective API key                                            */
/* ------------------------------------------------------------------ */

gchar *
ai_settings_get_effective_key (void)
{
    /* 1. In-memory key (from keyring or file at load time) */
    if (g_settings.api_key && *g_settings.api_key)
        return g_strdup (g_settings.api_key);

    /* 2. Environment variable */
    const gchar *env_var = ai_provider_get_env_key (g_settings.provider);
    if (env_var) {
        const gchar *env_val = g_getenv (env_var);
        if (env_val && *env_val)
            return g_strdup (env_val);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Load / save                                                          */
/* ------------------------------------------------------------------ */

void
ai_settings_init (void)
{
    gchar *path     = get_config_path ();
    GKeyFile *kf    = g_key_file_new ();

    if (g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL)) {
        gint prov = g_key_file_get_integer (kf, "AI", "provider", NULL);
        if (prov >= 0 && prov < AI_PROVIDER_COUNT)
            g_settings.provider = (AiProviderType) prov;

        g_free (g_settings.base_url);
        g_settings.base_url = g_key_file_get_string (kf, "AI", "base_url", NULL);

        g_free (g_settings.model_name);
        g_settings.model_name = g_key_file_get_string (kf, "AI", "model_name", NULL);

        g_settings.max_tokens  = g_key_file_get_integer (kf, "AI", "max_tokens",  NULL);
        g_settings.temperature = g_key_file_get_double  (kf, "AI", "temperature", NULL);
        if (g_settings.temperature < 0.01) g_settings.temperature = 0.7;

        /* Try keyring first, then fall back to plain-text key in config */
        gchar *ks_key = keyring_load_key (g_settings.provider);
        if (ks_key) {
            g_free (g_settings.api_key);
            g_settings.api_key = ks_key;
        } else {
            g_free (g_settings.api_key);
            g_settings.api_key = g_key_file_get_string (kf, "AI", "api_key_fallback", NULL);
        }
    }

    /* Apply defaults for any NULL fields */
    const AiProviderAdapter *a = ai_provider_get (g_settings.provider);
    if (!g_settings.base_url)
        g_settings.base_url = g_strdup (a->default_base_url);
    if (!g_settings.model_name && a->models_count > 0)
        g_settings.model_name = g_strdup (a->models[0].id);
    if (!g_settings.model_name)
        g_settings.model_name = g_strdup ("default");
    if (!g_settings.api_key)
        g_settings.api_key = g_strdup ("");

    g_key_file_free (kf);
    g_free (path);
}

static void
ai_settings_save_internal (void)
{
    gchar    *path = get_config_path ();
    GKeyFile *kf   = g_key_file_new ();

    g_key_file_set_integer (kf, "AI", "provider",     g_settings.provider);
    g_key_file_set_string  (kf, "AI", "base_url",     g_settings.base_url  ? g_settings.base_url  : "");
    g_key_file_set_string  (kf, "AI", "model_name",   g_settings.model_name? g_settings.model_name: "");
    g_key_file_set_integer (kf, "AI", "max_tokens",   g_settings.max_tokens);
    g_key_file_set_double  (kf, "AI", "temperature",  g_settings.temperature);

    /* Store key: prefer keyring, else save plaintext (user opted for fallback) */
    if (g_settings.api_key && *g_settings.api_key) {
#ifdef HAVE_LIBSECRET
        keyring_store_key (g_settings.provider, g_settings.api_key);
        /* Don't store in file — keyring is preferred */
#else
        g_key_file_set_string (kf, "AI", "api_key_fallback", g_settings.api_key);
#endif
    }

    GError *err = NULL;
    if (!g_key_file_save_to_file (kf, path, &err)) {
        g_warning ("Failed to save settings: %s", err->message);
        g_error_free (err);
    } else {
        /* Restrict file permissions so only the user can read it */
        chmod (path, 0600);
    }

    g_key_file_free (kf);
    g_free (path);
}

const AiSettings *
ai_settings_get (void)
{
    return &g_settings;
}

/* ================================================================== */
/* Settings Dialog                                                      */
/* ================================================================== */

typedef struct {
    GtkWidget   *provider_combo;
    GtkWidget   *model_combo;
    GtkWidget   *api_key_entry;
    GtkWidget   *base_url_entry;
    GtkWidget   *max_tokens_spin;
    GtkWidget   *temperature_spin;
    GtkWidget   *test_btn;
    GtkWidget   *test_label;
    GtkWidget   *env_var_label;
} DialogWidgets;

/* Populate model combo for the selected provider */
static void
populate_model_combo (GtkComboBoxText *combo, AiProviderType type)
{
    gtk_combo_box_text_remove_all (combo);
    guint count;
    const AiModelInfo *models = ai_provider_get_models (type, &count);
    for (guint i = 0; i < count; i++)
        gtk_combo_box_text_append (combo, models[i].id, models[i].display_name);
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
}

static void
on_provider_combo_changed (GtkComboBox *combo, gpointer user_data)
{
    DialogWidgets *w = (DialogWidgets *) user_data;
    gint active = gtk_combo_box_get_active (combo);
    if (active < 0 || active >= AI_PROVIDER_COUNT) return;

    AiProviderType type = (AiProviderType) active;
    const AiProviderAdapter *a = ai_provider_get (type);

    /* Update URL */
    gtk_entry_set_text (GTK_ENTRY (w->base_url_entry), a->default_base_url);

    /* Update model list */
    populate_model_combo (GTK_COMBO_BOX_TEXT (w->model_combo), type);

    /* Show env var hint */
    if (a->env_key_name) {
        gchar *hint = g_strdup_printf ("💡 Or set %s in environment",
                                       a->env_key_name);
        gtk_label_set_text (GTK_LABEL (w->env_var_label), hint);
        g_free (hint);
    } else {
        gtk_label_set_text (GTK_LABEL (w->env_var_label),
                            "💡 No authentication required");
    }
}

/* Test connection callback */
typedef struct { GtkWidget *label; } TestCtx;

static void
on_test_chunk (const gchar *chunk, gpointer ud)
{
    (void) chunk; (void) ud;
    /* We just need any response */
}

static void
on_test_done (const gchar *error_msg, gpointer ud)
{
    TestCtx *ctx = (TestCtx *) ud;
    if (error_msg) {
        gchar *markup = g_strdup_printf (
            "<span color='#ff6b6b'>✗ %s</span>", error_msg);
        gtk_label_set_markup (GTK_LABEL (ctx->label), markup);
        g_free (markup);
    } else {
        gtk_label_set_markup (GTK_LABEL (ctx->label),
            "<span color='#51cf66'>✓ Connection successful!</span>");
    }
    g_free (ctx);
}

static void
on_test_clicked (GtkButton *btn, gpointer user_data)
{
    DialogWidgets *w = (DialogWidgets *) user_data;
    (void) btn;

    gtk_label_set_text (GTK_LABEL (w->test_label), "Testing…");

    gint active = gtk_combo_box_get_active (GTK_COMBO_BOX (w->provider_combo));
    AiProviderType type = (AiProviderType) CLAMP (active, 0, AI_PROVIDER_COUNT - 1);
    const AiProviderAdapter *a = ai_provider_get (type);

    const gchar *key = gtk_entry_get_text (GTK_ENTRY (w->api_key_entry));
    const gchar *url = gtk_entry_get_text (GTK_ENTRY (w->base_url_entry));

    /* Build a minimal test request */
    GPtrArray *msgs = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_message_free);
    g_ptr_array_add (msgs, ai_message_new (AI_ROLE_USER, "Hi", NULL, NULL, NULL));

    gchar *model = gtk_combo_box_text_get_active_text (
                       GTK_COMBO_BOX_TEXT (w->model_combo));
    gchar *body = a->build_request_body (a, msgs, model ? model : "default",
                                         NULL, FALSE, 10, 0.0);
    g_ptr_array_free (msgs, TRUE);
    g_free (model);

    GHashTable *headers = ai_http_make_headers ();
    a->set_auth_headers (a, headers, key, url);

    gchar *test_url = a->get_endpoint_url (a, url,
        gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (w->model_combo)),
        key);

    AiHttpRequest *req = ai_http_request_new ();
    req->url        = test_url;
    req->headers    = headers;
    req->body_json  = body;
    req->stream     = FALSE;
    req->max_retries = 0;

    TestCtx *ctx  = g_new0 (TestCtx, 1);
    ctx->label    = w->test_label;
    req->on_chunk = on_test_chunk;
    req->on_done  = on_test_done;
    req->user_data = ctx;

    ai_http_send (req);

    /* Cleanup (ai_http_send copies internally) */
    g_hash_table_destroy (headers);
    g_free (body);
    g_free (test_url);
    ai_http_request_free (req);
}

void
ai_settings_show_dialog (GtkWindow *parent)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons (
        "AI Agent Settings", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_window_set_default_size (GTK_WINDOW (dialog), 500, -1);

    GtkWidget *content = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    GtkWidget *grid    = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID (grid), 12);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 20);

    DialogWidgets *w = g_new0 (DialogWidgets, 1);

    /* --- Provider combo --- */
    w->provider_combo = gtk_combo_box_text_new ();
    for (gint i = 0; i < AI_PROVIDER_COUNT; i++)
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (w->provider_combo),
                                        ai_provider_get_name ((AiProviderType) i));
    gtk_combo_box_set_active (GTK_COMBO_BOX (w->provider_combo),
                              (gint) g_settings.provider);

    /* --- Model combo --- */
    w->model_combo = gtk_combo_box_text_new ();
    populate_model_combo (GTK_COMBO_BOX_TEXT (w->model_combo), g_settings.provider);
    /* Pre-select current model */
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (w->model_combo), g_settings.model_name);

    /* --- API Key --- */
    w->api_key_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (w->api_key_entry),
                                    "sk-… (or set env var)");
    gtk_entry_set_text (GTK_ENTRY (w->api_key_entry),
                        g_settings.api_key ? g_settings.api_key : "");
    gtk_entry_set_visibility (GTK_ENTRY (w->api_key_entry), FALSE);

    /* --- Base URL --- */
    w->base_url_entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (w->base_url_entry),
                        g_settings.base_url ? g_settings.base_url : "");

    /* --- Max tokens --- */
    w->max_tokens_spin = gtk_spin_button_new_with_range (0, 128000, 256);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (w->max_tokens_spin),
                               g_settings.max_tokens);

    /* --- Temperature --- */
    w->temperature_spin = gtk_spin_button_new_with_range (0.0, 2.0, 0.05);
    gtk_spin_button_set_digits (GTK_SPIN_BUTTON (w->temperature_spin), 2);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (w->temperature_spin),
                               g_settings.temperature);

    /* --- Test button + label --- */
    w->test_btn   = gtk_button_new_with_label ("Test Connection");
    w->test_label = gtk_label_new ("");
    gtk_label_set_use_markup (GTK_LABEL (w->test_label), TRUE);

    /* --- Env var hint --- */
    w->env_var_label = gtk_label_new ("");
    GtkStyleContext *hint_ctx = gtk_widget_get_style_context (w->env_var_label);
    gtk_style_context_add_class (hint_ctx, "dim-label");

    /* --- Connect signals --- */
    g_signal_connect (w->provider_combo, "changed",
                      G_CALLBACK (on_provider_combo_changed), w);
    g_signal_connect (w->test_btn, "clicked",
                      G_CALLBACK (on_test_clicked), w);

    /* Trigger initial state */
    on_provider_combo_changed (GTK_COMBO_BOX (w->provider_combo), w);
    /* Restore user key after signal (signal resets it) */
    gtk_entry_set_text (GTK_ENTRY (w->api_key_entry),
                        g_settings.api_key ? g_settings.api_key : "");
    if (g_settings.model_name)
        gtk_combo_box_set_active_id (GTK_COMBO_BOX (w->model_combo),
                                     g_settings.model_name);

    /* --- Layout --- */
    gint row = 0;
#define ADD_ROW(label_text, widget) \
    { \
        GtkWidget *lbl = gtk_label_new (label_text); \
        gtk_widget_set_halign (lbl, GTK_ALIGN_END); \
        gtk_widget_set_hexpand (widget, TRUE); \
        gtk_grid_attach (GTK_GRID (grid), lbl,    0, row, 1, 1); \
        gtk_grid_attach (GTK_GRID (grid), widget, 1, row, 1, 1); \
        row++; \
    }

    ADD_ROW ("Provider:",      w->provider_combo);
    ADD_ROW ("Model:",         w->model_combo);
    ADD_ROW ("API Key:",       w->api_key_entry);
    ADD_ROW ("Base URL:",      w->base_url_entry);
    ADD_ROW ("Max Tokens:",    w->max_tokens_spin);
    ADD_ROW ("Temperature:",   w->temperature_spin);
#undef ADD_ROW

    /* Env var hint spans full width */
    gtk_widget_set_halign (w->env_var_label, GTK_ALIGN_END);
    gtk_grid_attach (GTK_GRID (grid), w->env_var_label, 0, row, 2, 1); row++;

    /* Test row */
    GtkWidget *test_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start (GTK_BOX (test_box), w->test_btn,   FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (test_box), w->test_label, TRUE, TRUE, 0);
    gtk_grid_attach (GTK_GRID (grid), test_box, 0, row, 2, 1);

    gtk_box_pack_start (GTK_BOX (content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all (dialog);

    gint result = gtk_dialog_run (GTK_DIALOG (dialog));
    if (result == GTK_RESPONSE_ACCEPT) {
        gint active = gtk_combo_box_get_active (GTK_COMBO_BOX (w->provider_combo));
        g_settings.provider = (AiProviderType) CLAMP (active, 0, AI_PROVIDER_COUNT - 1);

        g_free (g_settings.api_key);
        g_settings.api_key = g_strdup (
            gtk_entry_get_text (GTK_ENTRY (w->api_key_entry)));

        g_free (g_settings.base_url);
        g_settings.base_url = g_strdup (
            gtk_entry_get_text (GTK_ENTRY (w->base_url_entry)));

        g_free (g_settings.model_name);
        gchar *mid = gtk_combo_box_get_active_id (GTK_COMBO_BOX (w->model_combo));
        g_settings.model_name = mid ? g_strdup (mid) :
            g_strdup (gtk_combo_box_text_get_active_text (
                          GTK_COMBO_BOX_TEXT (w->model_combo)));

        g_settings.max_tokens  = (gint) gtk_spin_button_get_value (
                                      GTK_SPIN_BUTTON (w->max_tokens_spin));
        g_settings.temperature = gtk_spin_button_get_value (
                                      GTK_SPIN_BUTTON (w->temperature_spin));

        ai_settings_save_internal ();
    }

    g_free (w);
    gtk_widget_destroy (dialog);
}
