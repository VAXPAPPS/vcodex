/* ai_agent.c — Network engine for AI Agent */

/* ai_agent.c — Network engine for AI Agent */

#include "ai_agent.h"
#include "ai_settings.h"
#include "ai_tools.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

static SoupSession *session = NULL;
static JsonArray *chat_history = NULL;

typedef struct {
    AiAgentResponseCallback callback;
    gpointer user_data;
    SoupMessage *msg;
} AgentTaskData;

// Forward declaration
static void send_current_history (AgentTaskData *task);

static void
append_message_to_history (const gchar *role, const gchar *content, JsonArray *tool_calls, const gchar *tool_call_id, const gchar *name)
{
    if (!chat_history) chat_history = json_array_new ();

    JsonObject *msg = json_object_new ();
    json_object_set_string_member (msg, "role", role);
    
    if (content) {
        json_object_set_string_member (msg, "content", content);
    }

    if (tool_calls) {
        json_object_set_array_member (msg, "tool_calls", json_array_ref (tool_calls));
    }

    if (tool_call_id) {
        json_object_set_string_member (msg, "tool_call_id", tool_call_id);
    }

    if (name) {
        json_object_set_string_member (msg, "name", name);
    }

    json_array_add_object_element (chat_history, msg);
}

static void
on_response_read (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    SoupSession *session_obj = SOUP_SESSION (source_object);
    AgentTaskData *task = (AgentTaskData *) user_data;
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish (session_obj, res, &error);

    if (error) {
        task->callback (NULL, error->message, task->user_data);
        g_error_free (error);
        g_object_unref (task->msg);
        g_free (task);
        return;
    }

    guint status = soup_message_get_status (task->msg);
    if (status >= 200 && status < 300) {
        const gchar *data = g_bytes_get_data (body, NULL);
        JsonParser *parser = json_parser_new ();
        
        if (json_parser_load_from_data (parser, data, -1, &error)) {
            JsonNode *root = json_parser_get_root (parser);
            JsonObject *obj = json_node_get_object (root);
            
            if (json_object_has_member (obj, "choices")) {
                JsonArray *choices = json_object_get_array_member (obj, "choices");
                if (json_array_get_length (choices) > 0) {
                    JsonObject *choice = json_array_get_object_element (choices, 0);
                    JsonObject *message = json_object_get_object_member (choice, "message");
                    
                    const gchar *content = NULL;
                    if (json_object_has_member (message, "content") && !JSON_NODE_HOLDS_NULL (json_object_get_member (message, "content"))) {
                        content = json_object_get_string_member (message, "content");
                    }
                    
                    // Check for tool calls
                    if (json_object_has_member (message, "tool_calls")) {
                        JsonArray *tool_calls = json_object_get_array_member (message, "tool_calls");
                        
                        // Append the assistant's tool call to history
                        append_message_to_history ("assistant", content, tool_calls, NULL, NULL);

                        // Execute each tool call
                        for (guint i = 0; i < json_array_get_length (tool_calls); i++) {
                            JsonObject *tc = json_array_get_object_element (tool_calls, i);
                            const gchar *id = json_object_get_string_member (tc, "id");
                            JsonObject *func = json_object_get_object_member (tc, "function");
                            const gchar *name = json_object_get_string_member (func, "name");
                            const gchar *args_str = json_object_get_string_member (func, "arguments");

                            // Execute natively
                            gchar *result = ai_tools_execute (name, args_str);
                            
                            // Append tool response to history
                            append_message_to_history ("tool", result, NULL, id, name);
                            g_free (result);
                        }

                        // Recursively send the updated history
                        g_object_unref (parser);
                        g_bytes_unref (body);
                        g_object_unref (task->msg);
                        send_current_history (task);
                        return;

                    } else if (content) {
                        // Standard text response
                        append_message_to_history ("assistant", content, NULL, NULL, NULL);
                        task->callback (content, NULL, task->user_data);
                    } else {
                        task->callback (NULL, "Empty response from agent.", task->user_data);
                    }
                }
            } else {
                task->callback (NULL, "Invalid response format.", task->user_data);
            }
        } else {
            task->callback (NULL, error->message, task->user_data);
            g_error_free (error);
        }
        g_object_unref (parser);
    } else {
        gchar *err_msg = g_strdup_printf ("HTTP Error %d: %s", status, soup_message_get_reason_phrase (task->msg));
        task->callback (NULL, err_msg, task->user_data);
        g_free (err_msg);
    }

    g_bytes_unref (body);
    g_object_unref (task->msg);
    g_free (task);
}

static void
send_current_history (AgentTaskData *task)
{
    const AiSettings *settings = ai_settings_get ();
    gchar *url = g_strdup_printf ("%s/chat/completions", settings->base_url);

    JsonBuilder *builder = json_builder_new ();
    json_builder_begin_object (builder);
    
    json_builder_set_member_name (builder, "model");
    json_builder_add_string_value (builder, settings->model_name);

    // Messages array
    json_builder_set_member_name (builder, "messages");
    JsonNode *history_node = json_node_new (JSON_NODE_ARRAY);
    json_node_set_array (history_node, json_array_ref (chat_history));
    json_builder_add_value (builder, history_node);

    // Tools
    json_builder_set_member_name (builder, "tools");
    json_builder_add_value (builder, ai_tools_get_definitions ());

    json_builder_set_member_name (builder, "stream");
    json_builder_add_boolean_value (builder, FALSE);

    json_builder_end_object (builder);

    JsonGenerator *gen = json_generator_new ();
    JsonNode *root = json_builder_get_root (builder);
    json_generator_set_root (gen, root);
    gchar *json_str = json_generator_to_data (gen, NULL);

    SoupMessage *msg = soup_message_new ("POST", url);
    GBytes *body_bytes = g_bytes_new (json_str, strlen (json_str));
    soup_message_set_request_body_from_bytes (msg, "application/json", body_bytes);
    g_bytes_unref (body_bytes);

    if (settings->api_key && strlen (settings->api_key) > 0) {
        gchar *auth = g_strdup_printf ("Bearer %s", settings->api_key);
        soup_message_headers_append (soup_message_get_request_headers (msg), "Authorization", auth);
        g_free (auth);
    }

    task->msg = g_object_ref (msg);
    soup_session_send_and_read_async (session, msg, G_PRIORITY_DEFAULT, NULL, on_response_read, task);

    g_free (url);
    g_free (json_str);
    json_node_free (root);
    g_object_unref (builder);
    g_object_unref (gen);
    g_object_unref (msg);
}

void
ai_agent_send_prompt (const gchar *prompt, AiAgentResponseCallback callback, gpointer user_data)
{
    if (!session) {
        session = soup_session_new ();
        
        // Setup initial system prompt
        append_message_to_history ("system", "You are an intelligent IDE Assistant. You have access to tools that allow you to read and write files in the user's workspace. Always use these tools when asked to inspect or modify the code.", NULL, NULL, NULL);
    }

    // Append user prompt to history
    append_message_to_history ("user", prompt, NULL, NULL, NULL);

    AgentTaskData *task = g_new0 (AgentTaskData, 1);
    task->callback = callback;
    task->user_data = user_data;
    task->msg = NULL; // will be set in send_current_history

    send_current_history (task);
}
