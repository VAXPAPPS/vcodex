/*
 * ai_agent.c — AI Agent orchestrator
 *
 * Manages multi-turn conversations with:
 *   - Automatic tool-use loop (tool calls → execute → re-send)
 *   - Streaming token delivery to the UI
 *   - Provider abstraction via ai_provider.c
 *   - Cancellation support
 *   - Context-window management (prune old tool messages when approaching limit)
 */

#include "ai_agent.h"
#include "ai_http.h"
#include "ai_provider.h"
#include "ai_settings.h"
#include "ai_tools.h"
#include "ai_context.h"
#include <json-glib/json-glib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Global state                                                         */
/* ------------------------------------------------------------------ */

static AetherIdeWindow *g_agent_window = NULL;

/* ------------------------------------------------------------------ */
/* Session structure                                                    */
/* ------------------------------------------------------------------ */

struct _AiAgentSession {
    gchar        *title;
    GPtrArray    *history;       /* AiMessage* */
    GCancellable *cancel;
    gboolean      is_busy;

    /* Current turn's callbacks */
    AiAgentTokenCallback on_token;
    AiAgentDoneCallback  on_done;
    gpointer             user_data;

    /* Streaming accumulator (collects tokens for tool-call detection) */
    GString *stream_buf;

    /* Tool definitions node (cached) */
    JsonNode *tools_node;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static void session_send_history (AiAgentSession *session);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

void
ai_agent_init (AetherIdeWindow *window)
{
    g_agent_window = window;
    ai_tools_init (window);
}

AiAgentSession *
ai_agent_session_new (const gchar *title)
{
    AiAgentSession *s = g_new0 (AiAgentSession, 1);
    s->title      = g_strdup (title ? title : "New Chat");
    s->history    = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_message_free);
    s->cancel     = g_cancellable_new ();
    s->stream_buf = g_string_new ("");

    /* Build system prompt from IDE context */
    AiContext *ctx = g_agent_window ?
                     ai_context_capture (g_agent_window) : NULL;
    gchar *sys_prompt = ai_context_build_system_prompt (ctx);
    if (ctx) ai_context_free (ctx);

    g_ptr_array_add (s->history,
        ai_message_new (AI_ROLE_SYSTEM, sys_prompt, NULL, NULL, NULL));
    g_free (sys_prompt);

    s->tools_node = ai_tools_get_definitions ();
    return s;
}

void
ai_agent_session_free (AiAgentSession *session)
{
    if (!session) return;
    g_cancellable_cancel (session->cancel);
    g_object_unref (session->cancel);
    g_ptr_array_free (session->history, TRUE);
    g_string_free (session->stream_buf, TRUE);
    if (session->tools_node) json_node_free (session->tools_node);
    g_free (session->title);
    g_free (session);
}

void
ai_agent_session_reset (AiAgentSession *session)
{
    g_return_if_fail (session != NULL);
    ai_agent_session_cancel (session);
    g_ptr_array_set_size (session->history, 0);
    g_string_truncate (session->stream_buf, 0);

    /* Re-add system prompt */
    AiContext *ctx = g_agent_window ?
                     ai_context_capture (g_agent_window) : NULL;
    gchar *sys = ai_context_build_system_prompt (ctx);
    if (ctx) ai_context_free (ctx);
    g_ptr_array_add (session->history,
        ai_message_new (AI_ROLE_SYSTEM, sys, NULL, NULL, NULL));
    g_free (sys);
}

void
ai_agent_session_cancel (AiAgentSession *session)
{
    g_return_if_fail (session != NULL);
    if (session->is_busy) {
        g_cancellable_cancel (session->cancel);
        g_object_unref (session->cancel);
        session->cancel   = g_cancellable_new ();
        session->is_busy  = FALSE;
    }
}

const gchar *
ai_agent_session_get_title (AiAgentSession *session)
{
    return session ? session->title : NULL;
}

void
ai_agent_session_set_title (AiAgentSession *session, const gchar *title)
{
    if (!session) return;
    g_free (session->title);
    session->title = g_strdup (title);
}

/* ------------------------------------------------------------------ */
/* Context window management                                            */
/* ------------------------------------------------------------------ */

/* Remove old tool messages when history grows large.
 * We keep: [0]=system, last N user/assistant/tool turns */
static void
prune_history_if_needed (AiAgentSession *session)
{
    /* Simple heuristic: if history exceeds 40 messages, remove pairs 1-4
     * (messages[1]..messages[8]) but keep the system prompt at [0]. */
    if (session->history->len > 40) {
        for (gint i = 0; i < 8 && session->history->len > 10; i++) {
            AiMessage *m = (AiMessage *) g_ptr_array_remove_index (session->history, 1);
            ai_message_free (m);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Turn callback data                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    AiAgentSession *session;
    gboolean        stream_mode; /* TRUE if provider supports streaming */
} TurnData;

/* ------------------------------------------------------------------ */
/* Tool-use loop                                                        */
/* ------------------------------------------------------------------ */

static void
execute_tool_calls_and_continue (AiAgentSession *session,
                                 JsonArray      *tool_calls)
{
    /* Execute each tool call and append result to history */
    for (guint i = 0; i < json_array_get_length (tool_calls); i++) {
        JsonObject *tc   = json_array_get_object_element (tool_calls, i);
        const gchar *id  = json_object_get_string_member (tc, "id");
        JsonObject  *fn  = json_object_get_object_member (tc, "function");
        const gchar *name     = json_object_get_string_member (fn, "name");
        const gchar *args_str = json_object_get_string_member (fn, "arguments");

        /* Notify user via token stream that a tool is running */
        if (session->on_token) {
            gchar *notice = g_strdup_printf (
                "\n\n⚙️  *Running tool: `%s`…*\n", name);
            session->on_token (notice, session->user_data);
            g_free (notice);
        }

        gchar *result = ai_tools_execute (name, args_str);

        g_ptr_array_add (session->history,
            ai_message_new (AI_ROLE_TOOL, result, NULL, id, name));
        g_free (result);
    }

    /* Re-send full history to continue the turn */
    session_send_history (session);
}

/* ------------------------------------------------------------------ */
/* Streaming response handlers                                          */
/* ------------------------------------------------------------------ */

static void
on_stream_token (const gchar *raw_chunk, gpointer user_data)
{
    TurnData       *td      = (TurnData *) user_data;
    AiAgentSession *session = td->session;

    const AiSettings        *settings = ai_settings_get ();
    const AiProviderAdapter *provider = ai_provider_get (settings->provider);

    gchar *token = provider->parse_stream_chunk (provider, raw_chunk);

    if (token == AI_PROVIDER_STREAM_DONE) {
        /* Stream ended — handled in on_stream_done */
        return;
    }

    if (token) {
        g_string_append (session->stream_buf, token);
        if (session->on_token)
            session->on_token (token, session->user_data);
        g_free (token);
    }
}

static void
on_stream_done (const gchar *error_msg, gpointer user_data)
{
    TurnData       *td      = (TurnData *) user_data;
    AiAgentSession *session = td->session;
    g_free (td);

    if (error_msg) {
        session->is_busy = FALSE;
        if (session->on_done)
            session->on_done (error_msg, session->user_data);
        return;
    }

    /* For streaming, the full text was accumulated in stream_buf.
     * Add the assistant message to history. */
    if (session->stream_buf->len > 0) {
        g_ptr_array_add (session->history,
            ai_message_new (AI_ROLE_ASSISTANT,
                            session->stream_buf->str, NULL, NULL, NULL));
        g_string_truncate (session->stream_buf, 0);
    }

    session->is_busy = FALSE;
    if (session->on_done)
        session->on_done (NULL, session->user_data);
}

/* ------------------------------------------------------------------ */
/* Non-streaming response handlers                                      */
/* ------------------------------------------------------------------ */

static void
on_full_body (const gchar *body, gpointer user_data)
{
    TurnData       *td      = (TurnData *) user_data;
    AiAgentSession *session = td->session;

    const AiSettings        *settings = ai_settings_get ();
    const AiProviderAdapter *provider = ai_provider_get (settings->provider);

    gchar     *text       = NULL;
    JsonArray *tool_calls = NULL;

    if (!provider->parse_full_response (provider, body, &text, &tool_calls)) {
        /* parse_full_response may have set text to an error message */
        if (text) {
            if (session->on_done) session->on_done (text, session->user_data);
            g_free (text);
        } else {
            if (session->on_done)
                session->on_done ("Failed to parse response.", session->user_data);
        }
        if (tool_calls) json_array_unref (tool_calls);
        g_free (td);
        session->is_busy = FALSE;
        return;
    }

    if (tool_calls) {
        /* Append assistant message (with tool_calls) to history */
        g_ptr_array_add (session->history,
            ai_message_new (AI_ROLE_ASSISTANT, text, tool_calls, NULL, NULL));
        g_free (text);
        json_array_unref (tool_calls);

        /* Execute tools and loop */
        g_free (td);
        /* Get the tool_calls from the last message we just added */
        AiMessage *last = (AiMessage *) g_ptr_array_index (
            session->history, session->history->len - 1);
        execute_tool_calls_and_continue (session, last->tool_calls);
        return;
    }

    /* Plain text response */
    if (text) {
        g_ptr_array_add (session->history,
            ai_message_new (AI_ROLE_ASSISTANT, text, NULL, NULL, NULL));
        if (session->on_token)
            session->on_token (text, session->user_data);
        g_free (text);
    }

    g_free (td);
}

static void
on_full_done (const gchar *error_msg, gpointer user_data)
{
    TurnData       *td      = (TurnData *) user_data;
    AiAgentSession *session = td->session;
    /* td already freed in on_full_body on success, but may still exist on error */

    session->is_busy = FALSE;
    if (error_msg) {
        if (session->on_done)
            session->on_done (error_msg, session->user_data);
        g_free (td);
    } else {
        if (session->on_done)
            session->on_done (NULL, session->user_data);
        /* td may already be freed; only free if on_full_body didn't */
    }
}

/* ------------------------------------------------------------------ */
/* Core send                                                            */
/* ------------------------------------------------------------------ */

static void
session_send_history (AiAgentSession *session)
{
    const AiSettings        *settings = ai_settings_get ();
    const AiProviderAdapter *provider = ai_provider_get (settings->provider);

    gchar *api_key  = ai_settings_get_effective_key ();
    gchar *body     = provider->build_request_body (
        provider,
        session->history,
        settings->model_name,
        session->tools_node,
        TRUE,  /* stream — attempt streaming; provider handles fallback */
        settings->max_tokens,
        settings->temperature);

    GHashTable *headers = ai_http_make_headers ();
    provider->set_auth_headers (provider, headers, api_key, settings->base_url);

    gchar *url = provider->get_endpoint_url (
        provider, settings->base_url, settings->model_name, api_key);
    g_free (api_key);

    TurnData *td      = g_new0 (TurnData, 1);
    td->session       = session;
    td->stream_mode   = TRUE;

    AiHttpRequest *req = ai_http_request_new ();
    req->url        = url;
    req->headers    = headers;
    req->body_json  = body;
    req->stream     = TRUE;
    req->cancel     = session->cancel;
    req->max_retries = 2;

    /* For streaming, on_chunk gets raw SSE lines, on_done signals completion */
    req->on_chunk   = on_stream_token;
    req->on_done    = on_stream_done;
    req->user_data  = td;

    /* For Gemini (non-SSE) or providers that don't support streaming well,
     * fall back to non-streaming if needed. For now we use streaming for all. */

    ai_http_send (req);

    /* Cleanup — ai_http_send copies what it needs */
    g_hash_table_destroy (headers);
    g_free (body);
    g_free (url);
    ai_http_request_free (req);
}

/* ------------------------------------------------------------------ */
/* Public send                                                          */
/* ------------------------------------------------------------------ */

void
ai_agent_session_send (AiAgentSession       *session,
                       const gchar          *user_text,
                       AiAgentTokenCallback  on_token,
                       AiAgentDoneCallback   on_done,
                       gpointer              user_data)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (user_text != NULL);

    if (session->is_busy) {
        if (on_done) on_done ("Agent is busy. Cancel the current request first.", user_data);
        return;
    }

    session->is_busy   = TRUE;
    session->on_token  = on_token;
    session->on_done   = on_done;
    session->user_data = user_data;

    /* Update context in system prompt if we can */
    if (g_agent_window && session->history->len > 0) {
        AiMessage *sys = (AiMessage *) g_ptr_array_index (session->history, 0);
        if (sys->role == AI_ROLE_SYSTEM) {
            AiContext *ctx = ai_context_capture (g_agent_window);
            g_free (sys->content);
            sys->content = ai_context_build_system_prompt (ctx);
            ai_context_free (ctx);
        }
    }

    prune_history_if_needed (session);

    g_ptr_array_add (session->history,
        ai_message_new (AI_ROLE_USER, user_text, NULL, NULL, NULL));

    session_send_history (session);
}

/* ------------------------------------------------------------------ */
/* Global default session (backwards compat)                           */
/* ------------------------------------------------------------------ */

static AiAgentSession *g_default_session = NULL;

void
ai_agent_send_prompt (const gchar          *prompt,
                      AiAgentTokenCallback  on_token,
                      AiAgentDoneCallback   on_done,
                      gpointer              user_data)
{
    if (!g_default_session)
        g_default_session = ai_agent_session_new ("Default");

    ai_agent_session_send (g_default_session, prompt, on_token, on_done, user_data);
}

/* ------------------------------------------------------------------ */
/* History access — for conversation persistence                        */
/* ------------------------------------------------------------------ */

gint
ai_agent_session_get_message_count (AiAgentSession *session)
{
    g_return_val_if_fail (session != NULL, 0);
    return (gint) session->history->len;
}

const AiMessage *
ai_agent_session_get_message_at (AiAgentSession *session, gint index)
{
    g_return_val_if_fail (session != NULL, NULL);
    if (index < 0 || index >= (gint) session->history->len) return NULL;
    return (const AiMessage *) g_ptr_array_index (session->history, (guint) index);
}

void
ai_agent_session_load_history (AiAgentSession *session, GPtrArray *messages)
{
    g_return_if_fail (session != NULL);
    g_return_if_fail (messages != NULL);

    for (guint i = 0; i < messages->len; i++) {
        const AiMessage *src = (const AiMessage *) g_ptr_array_index (messages, i);
        /* Deep-copy each message */
        g_ptr_array_add (session->history,
            ai_message_new (src->role, src->content, src->tool_calls,
                            src->tool_call_id, src->tool_name));
    }
}
