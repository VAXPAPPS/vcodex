#ifndef AI_AGENT_H
#define AI_AGENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef void (*AiAgentResponseCallback)(const gchar *response_text, const gchar *error_msg, gpointer user_data);

/*
 * Sends a prompt to the configured AI API.
 * The callback is invoked on the main thread when the response arrives.
 */
void ai_agent_send_prompt (const gchar *prompt, AiAgentResponseCallback callback, gpointer user_data);

G_END_DECLS

#endif /* AI_AGENT_H */
