#ifndef AI_AGENT_H
#define AI_AGENT_H

/*
 * ai_agent.h — AI Agent orchestrator
 *
 * The orchestrator manages multi-turn conversations, tool-use loops,
 * streaming token delivery, and session lifecycle.
 *
 * Streaming model:
 *   - on_token is called for each text token as it arrives (may be many times)
 *   - on_done  is called once when the full turn is complete (no more tokens)
 *   Both callbacks run on the GLib main thread.
 */

#include <glib.h>
#include <gio/gio.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Callback signatures                                                  */
/* ------------------------------------------------------------------ */

/**
 * AiAgentTokenCallback:
 * @token:     A single text token (NOT null-terminated after the given length;
 *             treat as a regular C string — it IS null-terminated).
 * @user_data: Caller-supplied context.
 *
 * Invoked on the main thread for each streaming token. Append to your buffer.
 */
typedef void (*AiAgentTokenCallback) (const gchar *token,
                                      gpointer     user_data);

/**
 * AiAgentDoneCallback:
 * @error_msg: NULL on success; non-NULL human-readable error on failure.
 * @user_data: Caller-supplied context.
 *
 * Invoked exactly once on the main thread when the agent's turn is complete.
 */
typedef void (*AiAgentDoneCallback)  (const gchar *error_msg,
                                      gpointer     user_data);

/* ------------------------------------------------------------------ */
/* Session handle                                                       */
/* ------------------------------------------------------------------ */

/**
 * AiAgentSession:
 * Opaque handle representing one conversation thread.
 * Create with ai_agent_session_new(), free with ai_agent_session_free().
 */
typedef struct _AiAgentSession AiAgentSession;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * ai_agent_init:
 * Initialize the agent subsystem. Must be called once with the IDE window.
 */
void ai_agent_init (AetherIdeWindow *window);

/**
 * ai_agent_session_new:
 * Create a new isolated conversation session.
 * @title: Human-readable name for this conversation (may be NULL).
 */
AiAgentSession *ai_agent_session_new  (const gchar *title);

/**
 * ai_agent_session_free:
 * Destroy a session, freeing all associated memory.
 * Any in-flight requests are cancelled.
 */
void            ai_agent_session_free (AiAgentSession *session);

/**
 * ai_agent_session_reset:
 * Clear the conversation history, keeping the session handle alive.
 */
void            ai_agent_session_reset (AiAgentSession *session);

/**
 * ai_agent_session_send:
 * @session:    The conversation session.
 * @user_text:  The user's message.
 * @on_token:   Called for each streaming text token.
 * @on_done:    Called when the turn completes.
 * @user_data:  Passed to both callbacks.
 *
 * Appends @user_text to the session history and sends the full conversation
 * to the configured AI provider. Handles streaming and the tool-use loop.
 */
void ai_agent_session_send (AiAgentSession        *session,
                            const gchar           *user_text,
                            AiAgentTokenCallback   on_token,
                            AiAgentDoneCallback    on_done,
                            gpointer               user_data);

/**
 * ai_agent_session_cancel:
 * Cancel the current in-flight request (if any).
 * on_done will NOT be called after cancellation.
 */
void ai_agent_session_cancel (AiAgentSession *session);

/**
 * ai_agent_session_get_title:
 */
const gchar *ai_agent_session_get_title (AiAgentSession *session);

/**
 * ai_agent_session_set_title:
 */
void ai_agent_session_set_title (AiAgentSession *session, const gchar *title);

/* Convenience: global default session (backwards-compatible) */
void ai_agent_send_prompt (const gchar *prompt,
                           AiAgentTokenCallback on_token,
                           AiAgentDoneCallback  on_done,
                           gpointer user_data);

G_END_DECLS

#endif /* AI_AGENT_H */
