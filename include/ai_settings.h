#ifndef AI_SETTINGS_H
#define AI_SETTINGS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
    AI_PROVIDER_OPENAI = 0,
    AI_PROVIDER_ANTHROPIC,
    AI_PROVIDER_GEMINI,
    AI_PROVIDER_LOCAL
} AiProvider;

typedef struct {
    AiProvider provider;
    gchar *api_key;
    gchar *base_url;
    gchar *model_name;
} AiSettings;

/* 
 * Initialize the settings module, load settings from settings.ini
 */
void ai_settings_init (void);

/*
 * Get the current loaded settings. The returned pointer should not be freed.
 */
const AiSettings* ai_settings_get (void);

/*
 * Show the Settings Dialog to allow the user to change provider and API keys.
 * parent: The parent window (AetherIdeWindow)
 */
void ai_settings_show_dialog (GtkWindow *parent);

G_END_DECLS

#endif /* AI_SETTINGS_H */
