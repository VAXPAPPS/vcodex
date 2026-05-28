#ifndef AI_SETTINGS_H
#define AI_SETTINGS_H

/*
 * ai_settings.h — Extended AI provider settings
 *
 * Manages per-provider configuration including API keys (loaded from
 * GNOME Keyring via libsecret when available, or from an encrypted-
 * permission config file as fallback), base URLs, active model, and
 * generation parameters.
 *
 * Provides the settings dialog with:
 *   - Dynamic provider switching
 *   - Per-provider model dropdown (populated from ai_provider catalogue)
 *   - "Test Connection" button
 *   - Environment variable auto-detection
 */

#include <gtk/gtk.h>
#include "ai_provider.h"

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Settings structure                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    AiProviderType  provider;
    gchar          *api_key;      /* Never NULL (may be empty string) */
    gchar          *base_url;     /* Never NULL */
    gchar          *model_name;   /* Never NULL */
    gint            max_tokens;   /* 0 = provider default */
    gdouble         temperature;  /* 0.0 – 2.0, default 0.7 */
} AiSettings;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * ai_settings_init:
 * Load settings from disk. Must be called once at application startup.
 */
void ai_settings_init (void);

/**
 * ai_settings_get:
 * Returns pointer to the current settings. Returned pointer must NOT be freed.
 */
const AiSettings *ai_settings_get (void);

/**
 * ai_settings_show_dialog:
 * Display the modal settings dialog.
 */
void ai_settings_show_dialog (GtkWindow *parent);

/**
 * ai_settings_get_effective_key:
 * Returns the effective API key: keyring/config key if set,
 * otherwise the value of the provider's environment variable.
 * Returns a newly-allocated string; caller must g_free().
 * Returns NULL if no key is found at all.
 */
gchar *ai_settings_get_effective_key (void);

G_END_DECLS

#endif /* AI_SETTINGS_H */
