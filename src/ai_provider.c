/*
 * ai_provider.c — All AI provider adapter implementations
 *
 * Three protocol families:
 *   1. OpenAI-compatible  (OpenAI, Groq, DeepSeek, xAI, OpenRouter,
 *                          Together AI, Mistral, Ollama, Azure, Custom)
 *   2. Anthropic Messages API
 *   3. Google Gemini REST API
 *
 * Cohere v2 is also OpenAI-compatible at /v2/chat, so it uses family 1.
 */

#include "ai_provider.h"
#include <json-glib/json-glib.h>
#include <string.h>

/* ================================================================== */
/* Helper utilities                                                     */
/* ================================================================== */

/* Build a JSON string from a JsonBuilder (frees builder+root) */
static gchar *
builder_to_string (JsonBuilder *b)
{
    JsonNode      *root = json_builder_get_root (b);
    JsonGenerator *gen  = json_generator_new ();
    json_generator_set_root (gen, root);
    gchar *s = json_generator_to_data (gen, NULL);
    json_node_free (root);
    g_object_unref (gen);
    g_object_unref (b);
    return s;
}

/* Add a canonical message array to a JsonBuilder that is inside an object */
static void
builder_add_openai_messages (JsonBuilder *b, GPtrArray *messages)
{
    json_builder_set_member_name (b, "messages");
    json_builder_begin_array (b);
    for (guint i = 0; i < messages->len; i++) {
        AiMessage *m = (AiMessage *)g_ptr_array_index (messages, i);
        json_builder_begin_object (b);

        const gchar *role_str;
        switch (m->role) {
        case AI_ROLE_SYSTEM:    role_str = "system";    break;
        case AI_ROLE_USER:      role_str = "user";      break;
        case AI_ROLE_ASSISTANT: role_str = "assistant"; break;
        case AI_ROLE_TOOL:      role_str = "tool";      break;
        default:                role_str = "user";      break;
        }
        json_builder_set_member_name (b, "role");
        json_builder_add_string_value (b, role_str);

        if (m->content) {
            json_builder_set_member_name (b, "content");
            json_builder_add_string_value (b, m->content);
        }
        if (m->tool_calls) {
            json_builder_set_member_name (b, "tool_calls");
            JsonNode *tc_node = json_node_new (JSON_NODE_ARRAY);
            json_node_set_array (tc_node, m->tool_calls);
            json_builder_add_value (b, tc_node);
        }
        if (m->tool_call_id) {
            json_builder_set_member_name (b, "tool_call_id");
            json_builder_add_string_value (b, m->tool_call_id);
        }
        if (m->tool_name) {
            json_builder_set_member_name (b, "name");
            json_builder_add_string_value (b, m->tool_name);
        }
        json_builder_end_object (b);
    }
    json_builder_end_array (b);
}

/* Parse a JSON string safely, return parsed object or NULL */
static JsonObject *
parse_json_object (const gchar *json_str)
{
    GError     *err    = NULL;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json_str, -1, &err)) {
        g_error_free (err);
        g_object_unref (parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root (parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT (root)) {
        g_object_unref (parser);
        return NULL;
    }
    JsonObject *obj = json_object_ref (json_node_get_object (root));
    g_object_unref (parser);
    return obj;
}

/* ================================================================== */
/* FAMILY 1: OpenAI-compatible                                         */
/* ================================================================== */

static gchar *
openai_build_request (const AiProviderAdapter *a,
                      GPtrArray *messages, const gchar *model,
                      JsonNode *tools, gboolean stream,
                      gint max_tokens, gdouble temperature)
{
    (void) a;
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);

    json_builder_set_member_name (b, "model");
    json_builder_add_string_value (b, model);

    builder_add_openai_messages (b, messages);

    if (tools) {
        json_builder_set_member_name (b, "tools");
        json_builder_add_value (b, json_node_copy (tools));
        json_builder_set_member_name (b, "tool_choice");
        json_builder_add_string_value (b, "auto");
    }

    json_builder_set_member_name (b, "stream");
    json_builder_add_boolean_value (b, stream);

    if (max_tokens > 0) {
        json_builder_set_member_name (b, "max_tokens");
        json_builder_add_int_value (b, max_tokens);
    }
    json_builder_set_member_name (b, "temperature");
    json_builder_add_double_value (b, temperature);

    json_builder_end_object (b);
    return builder_to_string (b);
}

static void
openai_set_auth (const AiProviderAdapter *a, GHashTable *headers,
                 const gchar *api_key, const gchar *base_url)
{
    (void) a; (void) base_url;
    if (api_key && *api_key) {
        gchar *auth = g_strdup_printf ("Bearer %s", api_key);
        g_hash_table_insert (headers, g_strdup ("Authorization"), auth);
    }
    g_hash_table_insert (headers, g_strdup ("Content-Type"),
                         g_strdup ("application/json"));
}

static gchar *
openai_get_url (const AiProviderAdapter *a, const gchar *base_url,
                const gchar *model, const gchar *api_key)
{
    (void) a; (void) model; (void) api_key;
    return g_strdup_printf ("%s/chat/completions", base_url);
}

static gchar *
openai_parse_chunk (const AiProviderAdapter *a, const gchar *raw)
{
    (void) a;
    if (!raw || g_strcmp0 (raw, "[DONE]") == 0)
        return AI_PROVIDER_STREAM_DONE;

    JsonObject *obj = parse_json_object (raw);
    if (!obj) return NULL;

    gchar *token = NULL;
    if (json_object_has_member (obj, "choices")) {
        JsonArray *choices = json_object_get_array_member (obj, "choices");
        if (json_array_get_length (choices) > 0) {
            JsonObject *choice = json_array_get_object_element (choices, 0);
            if (json_object_has_member (choice, "delta")) {
                JsonObject *delta = json_object_get_object_member (choice, "delta");
                if (json_object_has_member (delta, "content") &&
                    !JSON_NODE_HOLDS_NULL (json_object_get_member (delta, "content"))) {
                    token = g_strdup (json_object_get_string_member (delta, "content"));
                }
            }
            /* Check finish_reason */
            if (json_object_has_member (choice, "finish_reason") &&
                !JSON_NODE_HOLDS_NULL (json_object_get_member (choice, "finish_reason"))) {
                const gchar *fr = json_object_get_string_member (choice, "finish_reason");
                if (g_strcmp0 (fr, "stop") == 0 || g_strcmp0 (fr, "end_turn") == 0) {
                    json_object_unref (obj);
                    if (token) { g_free (token); }
                    return AI_PROVIDER_STREAM_DONE;
                }
            }
        }
    }
    json_object_unref (obj);
    return token; /* may be NULL for non-content chunks */
}

static gboolean
openai_parse_full (const AiProviderAdapter *a, const gchar *body,
                   gchar **text_out, JsonArray **tc_out)
{
    (void) a;
    *text_out = NULL;
    *tc_out   = NULL;

    JsonObject *obj = parse_json_object (body);
    if (!obj) return FALSE;

    if (!json_object_has_member (obj, "choices")) {
        /* Try error object */
        if (json_object_has_member (obj, "error")) {
            JsonObject *err = json_object_get_object_member (obj, "error");
            *text_out = g_strdup_printf ("API Error: %s",
                json_object_get_string_member (err, "message"));
        }
        json_object_unref (obj);
        return FALSE;
    }

    JsonArray  *choices = json_object_get_array_member (obj, "choices");
    JsonObject *choice  = json_array_get_object_element (choices, 0);
    JsonObject *message = json_object_get_object_member (choice, "message");

    if (json_object_has_member (message, "content") &&
        !JSON_NODE_HOLDS_NULL (json_object_get_member (message, "content"))) {
        *text_out = g_strdup (json_object_get_string_member (message, "content"));
    }
    if (json_object_has_member (message, "tool_calls")) {
        *tc_out = json_array_ref (
            json_object_get_array_member (message, "tool_calls"));
    }

    json_object_unref (obj);
    return TRUE;
}

/* ================================================================== */
/* FAMILY 2: Anthropic Messages API                                    */
/* ================================================================== */

static gchar *
anthropic_build_request (const AiProviderAdapter *a,
                         GPtrArray *messages, const gchar *model,
                         JsonNode *tools, gboolean stream,
                         gint max_tokens, gdouble temperature)
{
    (void) a;
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);

    json_builder_set_member_name (b, "model");
    json_builder_add_string_value (b, model);

    json_builder_set_member_name (b, "max_tokens");
    json_builder_add_int_value (b, max_tokens > 0 ? max_tokens : 8192);

    /* Anthropic separates "system" from "messages" */
    const gchar *system_text = NULL;
    json_builder_set_member_name (b, "messages");
    json_builder_begin_array (b);
    for (guint i = 0; i < messages->len; i++) {
        AiMessage *m = (AiMessage *)g_ptr_array_index (messages, i);
        if (m->role == AI_ROLE_SYSTEM) {
            system_text = m->content;
            continue;
        }
        json_builder_begin_object (b);
        const gchar *role_str = (m->role == AI_ROLE_USER) ? "user" : "assistant";
        json_builder_set_member_name (b, "role");
        json_builder_add_string_value (b, role_str);

        if (m->tool_calls) {
            /* assistant message with tool use */
            json_builder_set_member_name (b, "content");
            json_builder_begin_array (b);
            for (guint j = 0; j < json_array_get_length (m->tool_calls); j++) {
                JsonObject *tc = json_array_get_object_element (m->tool_calls, j);
                JsonObject *fn = json_object_get_object_member (tc, "function");
                json_builder_begin_object (b);
                json_builder_set_member_name (b, "type");
                json_builder_add_string_value (b, "tool_use");
                json_builder_set_member_name (b, "id");
                json_builder_add_string_value (b, json_object_get_string_member (tc, "id"));
                json_builder_set_member_name (b, "name");
                json_builder_add_string_value (b, json_object_get_string_member (fn, "name"));
                json_builder_set_member_name (b, "input");
                /* Parse the arguments string into a JSON object */
                const gchar *args_str = json_object_get_string_member (fn, "arguments");
                JsonParser *ap = json_parser_new ();
                if (json_parser_load_from_data (ap, args_str, -1, NULL)) {
                    json_builder_add_value (b, json_node_copy (json_parser_get_root (ap)));
                } else {
                    json_builder_begin_object (b); json_builder_end_object (b);
                }
                g_object_unref (ap);
                json_builder_end_object (b);
            }
            json_builder_end_array (b);
        } else if (m->role == AI_ROLE_TOOL) {
            /* tool result */
            json_builder_set_member_name (b, "role");
            /* overwrite role to "user" for tool results in Anthropic */
            json_builder_end_object (b);
            json_builder_begin_object (b);
            json_builder_set_member_name (b, "role");
            json_builder_add_string_value (b, "user");
            json_builder_set_member_name (b, "content");
            json_builder_begin_array (b);
            json_builder_begin_object (b);
            json_builder_set_member_name (b, "type");
            json_builder_add_string_value (b, "tool_result");
            json_builder_set_member_name (b, "tool_use_id");
            json_builder_add_string_value (b, m->tool_call_id ? m->tool_call_id : "");
            json_builder_set_member_name (b, "content");
            json_builder_add_string_value (b, m->content ? m->content : "");
            json_builder_end_object (b);
            json_builder_end_array (b);
        } else {
            json_builder_set_member_name (b, "content");
            json_builder_add_string_value (b, m->content ? m->content : "");
        }
        json_builder_end_object (b);
    }
    json_builder_end_array (b);

    if (system_text) {
        json_builder_set_member_name (b, "system");
        json_builder_add_string_value (b, system_text);
    }

    if (tools) {
        /* Convert OpenAI tools format to Anthropic format */
        json_builder_set_member_name (b, "tools");
        json_builder_begin_array (b);
        JsonArray *tools_arr = json_node_get_array (tools);
        for (guint i = 0; i < json_array_get_length (tools_arr); i++) {
            JsonObject *tool = json_array_get_object_element (tools_arr, i);
            JsonObject *fn   = json_object_get_object_member (tool, "function");
            json_builder_begin_object (b);
            json_builder_set_member_name (b, "name");
            json_builder_add_string_value (b, json_object_get_string_member (fn, "name"));
            json_builder_set_member_name (b, "description");
            json_builder_add_string_value (b, json_object_get_string_member (fn, "description"));
            json_builder_set_member_name (b, "input_schema");
            json_builder_add_value (b, json_node_copy (
                json_object_get_member (fn, "parameters")));
            json_builder_end_object (b);
        }
        json_builder_end_array (b);
    }

    json_builder_set_member_name (b, "stream");
    json_builder_add_boolean_value (b, stream);

    json_builder_end_object (b);
    return builder_to_string (b);
}

static void
anthropic_set_auth (const AiProviderAdapter *a, GHashTable *headers,
                    const gchar *api_key, const gchar *base_url)
{
    (void) a; (void) base_url;
    if (api_key && *api_key)
        g_hash_table_insert (headers, g_strdup ("x-api-key"), g_strdup (api_key));
    g_hash_table_insert (headers, g_strdup ("anthropic-version"),
                         g_strdup ("2023-06-01"));
    g_hash_table_insert (headers, g_strdup ("Content-Type"),
                         g_strdup ("application/json"));
}

static gchar *
anthropic_get_url (const AiProviderAdapter *a, const gchar *base_url,
                   const gchar *model, const gchar *api_key)
{
    (void) a; (void) model; (void) api_key;
    return g_strdup_printf ("%s/messages", base_url);
}

static gchar *
anthropic_parse_chunk (const AiProviderAdapter *a, const gchar *raw)
{
    (void) a;
    if (!raw) return NULL;
    JsonObject *obj = parse_json_object (raw);
    if (!obj) return NULL;

    gchar *token = NULL;
    if (json_object_has_member (obj, "type")) {
        const gchar *type = json_object_get_string_member (obj, "type");
        if (g_strcmp0 (type, "content_block_delta") == 0) {
            JsonObject *delta = json_object_get_object_member (obj, "delta");
            if (delta && json_object_has_member (delta, "text"))
                token = g_strdup (json_object_get_string_member (delta, "text"));
        } else if (g_strcmp0 (type, "message_stop") == 0 ||
                   g_strcmp0 (type, "message_delta") == 0) {
            /* Check stop_reason in message_delta */
            if (json_object_has_member (obj, "delta")) {
                JsonObject *delta = json_object_get_object_member (obj, "delta");
                if (json_object_has_member (delta, "stop_reason")) {
                    json_object_unref (obj);
                    return AI_PROVIDER_STREAM_DONE;
                }
            }
        }
    }
    json_object_unref (obj);
    return token;
}

static gboolean
anthropic_parse_full (const AiProviderAdapter *a, const gchar *body,
                      gchar **text_out, JsonArray **tc_out)
{
    (void) a;
    *text_out = NULL;
    *tc_out   = NULL;

    JsonObject *obj = parse_json_object (body);
    if (!obj) return FALSE;

    if (!json_object_has_member (obj, "content")) {
        json_object_unref (obj);
        return FALSE;
    }

    GString   *text      = g_string_new ("");
    JsonArray *tc_array  = NULL;
    JsonArray *content   = json_object_get_array_member (obj, "content");

    for (guint i = 0; i < json_array_get_length (content); i++) {
        JsonObject *block = json_array_get_object_element (content, i);
        const gchar *type = json_object_get_string_member (block, "type");

        if (g_strcmp0 (type, "text") == 0) {
            g_string_append (text, json_object_get_string_member (block, "text"));
        } else if (g_strcmp0 (type, "tool_use") == 0) {
            /* Convert Anthropic tool_use to OpenAI tool_calls format */
            if (!tc_array) tc_array = json_array_new ();

            JsonObject *tc  = json_object_new ();
            JsonObject *fn  = json_object_new ();

            json_object_set_string_member (tc, "id",
                json_object_get_string_member (block, "id"));
            json_object_set_string_member (tc, "type", "function");
            json_object_set_string_member (fn, "name",
                json_object_get_string_member (block, "name"));

            /* Serialize input back to string */
            JsonGenerator *gen = json_generator_new ();
            JsonNode *inp_node = json_node_copy (
                json_object_get_member (block, "input"));
            json_generator_set_root (gen, inp_node);
            gchar *args_str = json_generator_to_data (gen, NULL);
            json_node_free (inp_node);
            g_object_unref (gen);

            json_object_set_string_member (fn, "arguments", args_str);
            g_free (args_str);
            json_object_set_object_member (tc, "function", fn);
            json_array_add_object_element (tc_array, tc);
        }
    }

    if (text->len > 0)
        *text_out = g_string_free (text, FALSE);
    else
        g_string_free (text, TRUE);

    *tc_out = tc_array;
    json_object_unref (obj);
    return TRUE;
}

/* ================================================================== */
/* FAMILY 3: Google Gemini                                             */
/* ================================================================== */

static gchar *
gemini_build_request (const AiProviderAdapter *a,
                      GPtrArray *messages, const gchar *model,
                      JsonNode *tools, gboolean stream,
                      gint max_tokens, gdouble temperature)
{
    (void) a; (void) stream;
    JsonBuilder *b = json_builder_new ();
    json_builder_begin_object (b);

    /* Contents array */
    json_builder_set_member_name (b, "contents");
    json_builder_begin_array (b);
    for (guint i = 0; i < messages->len; i++) {
        AiMessage *m = (AiMessage *)g_ptr_array_index (messages, i);
        if (m->role == AI_ROLE_SYSTEM) continue; /* handled separately */

        json_builder_begin_object (b);
        json_builder_set_member_name (b, "role");
        json_builder_add_string_value (b,
            m->role == AI_ROLE_USER ? "user" : "model");
        json_builder_set_member_name (b, "parts");
        json_builder_begin_array (b);
        json_builder_begin_object (b);
        json_builder_set_member_name (b, "text");
        json_builder_add_string_value (b, m->content ? m->content : "");
        json_builder_end_object (b);
        json_builder_end_array (b);
        json_builder_end_object (b);
    }
    json_builder_end_array (b);

    /* System instruction */
    for (guint i = 0; i < messages->len; i++) {
        AiMessage *m = (AiMessage *)g_ptr_array_index (messages, i);
        if (m->role == AI_ROLE_SYSTEM && m->content) {
            json_builder_set_member_name (b, "systemInstruction");
            json_builder_begin_object (b);
            json_builder_set_member_name (b, "parts");
            json_builder_begin_array (b);
            json_builder_begin_object (b);
            json_builder_set_member_name (b, "text");
            json_builder_add_string_value (b, m->content);
            json_builder_end_object (b);
            json_builder_end_array (b);
            json_builder_end_object (b);
            break;
        }
    }

    /* Generation config */
    json_builder_set_member_name (b, "generationConfig");
    json_builder_begin_object (b);
    if (max_tokens > 0) {
        json_builder_set_member_name (b, "maxOutputTokens");
        json_builder_add_int_value (b, max_tokens);
    }
    json_builder_set_member_name (b, "temperature");
    json_builder_add_double_value (b, temperature);
    json_builder_end_object (b);

    json_builder_end_object (b);
    return builder_to_string (b);
}

static void
gemini_set_auth (const AiProviderAdapter *a, GHashTable *headers,
                 const gchar *api_key, const gchar *base_url)
{
    (void) a; (void) api_key; (void) base_url;
    /* Gemini uses the key as a URL query param, not a header */
    g_hash_table_insert (headers, g_strdup ("Content-Type"),
                         g_strdup ("application/json"));
}

static gchar *
gemini_get_url (const AiProviderAdapter *a, const gchar *base_url,
                const gchar *model, const gchar *api_key)
{
    (void) a;
    /* streaming: :streamGenerateContent, non-streaming: :generateContent */
    return g_strdup_printf ("%s/models/%s:generateContent?key=%s",
                            base_url, model, api_key ? api_key : "");
}

static gchar *
gemini_parse_chunk (const AiProviderAdapter *a, const gchar *raw)
{
    (void) a;
    /* Gemini streaming returns JSON objects separated by newlines (not SSE) */
    JsonObject *obj = parse_json_object (raw);
    if (!obj) return NULL;

    gchar *token = NULL;
    if (json_object_has_member (obj, "candidates")) {
        JsonArray  *cands = json_object_get_array_member (obj, "candidates");
        JsonObject *cand  = json_array_get_object_element (cands, 0);
        if (json_object_has_member (cand, "content")) {
            JsonObject *content = json_object_get_object_member (cand, "content");
            JsonArray  *parts   = json_object_get_array_member (content, "parts");
            JsonObject *part    = json_array_get_object_element (parts, 0);
            if (json_object_has_member (part, "text"))
                token = g_strdup (json_object_get_string_member (part, "text"));
        }
        if (json_object_has_member (cand, "finishReason")) {
            const gchar *fr = json_object_get_string_member (cand, "finishReason");
            if (g_strcmp0 (fr, "STOP") == 0 || g_strcmp0 (fr, "MAX_TOKENS") == 0) {
                json_object_unref (obj);
                if (token) g_free (token);
                return AI_PROVIDER_STREAM_DONE;
            }
        }
    }
    json_object_unref (obj);
    return token;
}

static gboolean
gemini_parse_full (const AiProviderAdapter *a, const gchar *body,
                   gchar **text_out, JsonArray **tc_out)
{
    (void) a;
    *text_out = NULL;
    *tc_out   = NULL;

    JsonObject *obj = parse_json_object (body);
    if (!obj) return FALSE;

    if (!json_object_has_member (obj, "candidates")) {
        json_object_unref (obj);
        return FALSE;
    }

    JsonArray  *cands   = json_object_get_array_member (obj, "candidates");
    JsonObject *cand    = json_array_get_object_element (cands, 0);
    JsonObject *content = json_object_get_object_member (cand, "content");
    JsonArray  *parts   = json_object_get_array_member (content, "parts");

    GString *text = g_string_new ("");
    for (guint i = 0; i < json_array_get_length (parts); i++) {
        JsonObject *part = json_array_get_object_element (parts, i);
        if (json_object_has_member (part, "text"))
            g_string_append (text, json_object_get_string_member (part, "text"));
    }

    *text_out = g_string_free (text, FALSE);
    json_object_unref (obj);
    return TRUE;
}

/* Azure uses same protocol as OpenAI but different URL structure */
static gchar *
azure_get_url (const AiProviderAdapter *a, const gchar *base_url,
               const gchar *model, const gchar *api_key)
{
    (void) a; (void) api_key;
    /* Expected base_url format:
     * https://{resource}.openai.azure.com/openai/deployments/{deployment}
     * OR the user sets base_url = https://resource.openai.azure.com
     * and model = deployment name. */
    return g_strdup_printf ("%s/openai/deployments/%s/chat/completions"
                            "?api-version=2024-08-01-preview",
                            base_url, model);
}

static void
azure_set_auth (const AiProviderAdapter *a, GHashTable *headers,
                const gchar *api_key, const gchar *base_url)
{
    (void) a; (void) base_url;
    if (api_key && *api_key)
        g_hash_table_insert (headers, g_strdup ("api-key"), g_strdup (api_key));
    g_hash_table_insert (headers, g_strdup ("Content-Type"),
                         g_strdup ("application/json"));
}

/* OpenRouter adds extra courtesy headers */
static void
openrouter_set_auth (const AiProviderAdapter *a, GHashTable *headers,
                     const gchar *api_key, const gchar *base_url)
{
    openai_set_auth (a, headers, api_key, base_url);
    g_hash_table_insert (headers, g_strdup ("HTTP-Referer"),
                         g_strdup ("https://github.com/vcodex/aether-ide"));
    g_hash_table_insert (headers, g_strdup ("X-Title"),
                         g_strdup ("AetherIDE"));
}

/* ================================================================== */
/* Model catalogues                                                     */
/* ================================================================== */

static const AiModelInfo openai_models[] = {
    { "gpt-4o",                "GPT-4o",                 TRUE, TRUE  },
    { "gpt-4o-mini",           "GPT-4o mini",            TRUE, TRUE  },
    { "o3-mini",               "o3 mini",                FALSE, TRUE  },
    { "o1",                    "o1",                     FALSE, TRUE  },
    { "gpt-4-turbo",           "GPT-4 Turbo",            TRUE, TRUE  },
    { "gpt-3.5-turbo",         "GPT-3.5 Turbo",          FALSE, TRUE },
};
static const AiModelInfo anthropic_models[] = {
    { "claude-opus-4-5",        "Claude Opus 4.5",        TRUE, TRUE },
    { "claude-sonnet-4-5",      "Claude Sonnet 4.5",      TRUE, TRUE },
    { "claude-haiku-3-5",       "Claude Haiku 3.5",       TRUE, TRUE },
    { "claude-3-opus-20240229",  "Claude 3 Opus",         TRUE, TRUE },
};
static const AiModelInfo gemini_models[] = {
    { "gemini-2.0-flash",         "Gemini 2.0 Flash",        TRUE, TRUE  },
    { "gemini-2.0-flash-thinking","Gemini 2.0 Flash Thinking",TRUE, TRUE },
    { "gemini-1.5-pro",           "Gemini 1.5 Pro",          TRUE, TRUE  },
    { "gemini-1.5-flash",         "Gemini 1.5 Flash",        TRUE, TRUE  },
};
static const AiModelInfo mistral_models[] = {
    { "mistral-large-latest",   "Mistral Large",          FALSE, TRUE },
    { "mistral-small-latest",   "Mistral Small",          FALSE, TRUE },
    { "codestral-latest",       "Codestral (Code)",       FALSE, TRUE },
    { "open-mistral-nemo",      "Mistral Nemo",           FALSE, TRUE },
};
static const AiModelInfo groq_models[] = {
    { "llama-3.3-70b-versatile","Llama 3.3 70B",          FALSE, TRUE },
    { "llama-3.1-8b-instant",   "Llama 3.1 8B (Fast)",    FALSE, TRUE },
    { "mixtral-8x7b-32768",     "Mixtral 8x7B",           FALSE, TRUE },
    { "gemma2-9b-it",           "Gemma 2 9B",             FALSE, TRUE },
};
static const AiModelInfo cohere_models[] = {
    { "command-r-plus-08-2024", "Command R+ (Aug 2024)",  FALSE, TRUE },
    { "command-r-08-2024",      "Command R (Aug 2024)",   FALSE, TRUE },
    { "command-light",          "Command Light",          FALSE, TRUE },
};
static const AiModelInfo deepseek_models[] = {
    { "deepseek-chat",          "DeepSeek Chat (V3)",     FALSE, TRUE },
    { "deepseek-reasoner",      "DeepSeek Reasoner (R1)", FALSE, FALSE},
    { "deepseek-coder",         "DeepSeek Coder",         FALSE, TRUE },
};
static const AiModelInfo xai_models[] = {
    { "grok-3",                 "Grok-3",                 FALSE, TRUE },
    { "grok-3-fast",            "Grok-3 Fast",            FALSE, TRUE },
    { "grok-2-latest",          "Grok-2",                 FALSE, TRUE },
};
static const AiModelInfo azure_models[] = {
    { "gpt-4o",                 "GPT-4o (Azure)",         TRUE, TRUE  },
    { "gpt-4-turbo",            "GPT-4 Turbo (Azure)",    TRUE, TRUE  },
    { "gpt-35-turbo",           "GPT-3.5 Turbo (Azure)",  FALSE, TRUE },
};
static const AiModelInfo openrouter_models[] = {
    { "openai/gpt-4o",                    "GPT-4o",             TRUE, TRUE  },
    { "anthropic/claude-sonnet-4-5",      "Claude Sonnet 4.5",  TRUE, TRUE  },
    { "google/gemini-2.0-flash-001",      "Gemini 2.0 Flash",   TRUE, TRUE  },
    { "deepseek/deepseek-chat",           "DeepSeek V3",        FALSE, TRUE },
    { "meta-llama/llama-3.3-70b-instruct","Llama 3.3 70B",      FALSE, TRUE },
    { "mistralai/mistral-large",          "Mistral Large",      FALSE, TRUE },
    { "x-ai/grok-3",                      "Grok-3",             FALSE, TRUE },
};
static const AiModelInfo together_models[] = {
    { "meta-llama/Llama-3.3-70B-Instruct-Turbo","Llama 3.3 70B Turbo",FALSE, TRUE },
    { "meta-llama/Meta-Llama-3.1-8B-Instruct-Turbo","Llama 3.1 8B",FALSE, TRUE },
    { "mistralai/Mixtral-8x7B-Instruct-v0.1","Mixtral 8x7B",  FALSE, TRUE },
    { "Qwen/Qwen2.5-Coder-32B-Instruct",  "Qwen2.5 Coder 32B",FALSE, TRUE },
};
static const AiModelInfo ollama_models[] = {
    { "llama3.3",               "Llama 3.3",              FALSE, TRUE },
    { "qwen2.5-coder:32b",      "Qwen2.5 Coder 32B",      FALSE, TRUE },
    { "deepseek-r1:32b",        "DeepSeek R1 32B",        FALSE, FALSE },
    { "codellama:34b",          "CodeLlama 34B",          FALSE, TRUE },
    { "mistral",                "Mistral 7B",             FALSE, TRUE },
    { "gemma3:27b",             "Gemma3 27B",             FALSE, TRUE },
};
static const AiModelInfo custom_models[] = {
    { "custom-model",           "Custom Model",           FALSE, TRUE },
};

/* ================================================================== */
/* Static adapter table                                                */
/* ================================================================== */

#define MODELS_COUNT(arr) (G_N_ELEMENTS (arr))

static const AiProviderAdapter g_adapters[AI_PROVIDER_COUNT] = {
    [AI_PROVIDER_OPENAI] = {
        AI_PROVIDER_OPENAI, "OpenAI",
        "https://api.openai.com/v1", "OPENAI_API_KEY",
        openai_models, MODELS_COUNT(openai_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_ANTHROPIC] = {
        AI_PROVIDER_ANTHROPIC, "Anthropic",
        "https://api.anthropic.com/v1", "ANTHROPIC_API_KEY",
        anthropic_models, MODELS_COUNT(anthropic_models),
        anthropic_build_request, anthropic_set_auth, anthropic_get_url,
        anthropic_parse_chunk, anthropic_parse_full
    },
    [AI_PROVIDER_GEMINI] = {
        AI_PROVIDER_GEMINI, "Google Gemini",
        "https://generativelanguage.googleapis.com/v1beta", "GEMINI_API_KEY",
        gemini_models, MODELS_COUNT(gemini_models),
        gemini_build_request, gemini_set_auth, gemini_get_url,
        gemini_parse_chunk, gemini_parse_full
    },
    [AI_PROVIDER_MISTRAL] = {
        AI_PROVIDER_MISTRAL, "Mistral AI",
        "https://api.mistral.ai/v1", "MISTRAL_API_KEY",
        mistral_models, MODELS_COUNT(mistral_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_GROQ] = {
        AI_PROVIDER_GROQ, "Groq",
        "https://api.groq.com/openai/v1", "GROQ_API_KEY",
        groq_models, MODELS_COUNT(groq_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_COHERE] = {
        AI_PROVIDER_COHERE, "Cohere",
        "https://api.cohere.com/compatibility/v1", "COHERE_API_KEY",
        cohere_models, MODELS_COUNT(cohere_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_DEEPSEEK] = {
        AI_PROVIDER_DEEPSEEK, "DeepSeek",
        "https://api.deepseek.com/v1", "DEEPSEEK_API_KEY",
        deepseek_models, MODELS_COUNT(deepseek_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_XAI] = {
        AI_PROVIDER_XAI, "xAI (Grok)",
        "https://api.x.ai/v1", "XAI_API_KEY",
        xai_models, MODELS_COUNT(xai_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_AZURE] = {
        AI_PROVIDER_AZURE, "Azure OpenAI",
        "https://YOUR-RESOURCE.openai.azure.com", "AZURE_OPENAI_API_KEY",
        azure_models, MODELS_COUNT(azure_models),
        openai_build_request, azure_set_auth, azure_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_OPENROUTER] = {
        AI_PROVIDER_OPENROUTER, "OpenRouter",
        "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY",
        openrouter_models, MODELS_COUNT(openrouter_models),
        openai_build_request, openrouter_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_TOGETHER] = {
        AI_PROVIDER_TOGETHER, "Together AI",
        "https://api.together.xyz/v1", "TOGETHER_API_KEY",
        together_models, MODELS_COUNT(together_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_OLLAMA] = {
        AI_PROVIDER_OLLAMA, "Ollama (Local)",
        "http://localhost:11434/v1", NULL,
        ollama_models, MODELS_COUNT(ollama_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
    [AI_PROVIDER_CUSTOM] = {
        AI_PROVIDER_CUSTOM, "Custom (OpenAI-Compatible)",
        "http://localhost:8080/v1", "CUSTOM_API_KEY",
        custom_models, MODELS_COUNT(custom_models),
        openai_build_request, openai_set_auth, openai_get_url,
        openai_parse_chunk, openai_parse_full
    },
};

/* ================================================================== */
/* Public API                                                           */
/* ================================================================== */

const AiProviderAdapter *
ai_provider_get (AiProviderType type)
{
    if (type < 0 || type >= AI_PROVIDER_COUNT) return &g_adapters[0];
    return &g_adapters[type];
}

const gchar *
ai_provider_get_name (AiProviderType type)
{
    return ai_provider_get (type)->name;
}

const gchar *
ai_provider_get_default_url (AiProviderType type)
{
    return ai_provider_get (type)->default_base_url;
}

const gchar *
ai_provider_get_env_key (AiProviderType type)
{
    return ai_provider_get (type)->env_key_name;
}

const AiModelInfo *
ai_provider_get_models (AiProviderType type, guint *count)
{
    const AiProviderAdapter *a = ai_provider_get (type);
    if (count) *count = a->models_count;
    return a->models;
}

/* ------------------------------------------------------------------ */
/* AiMessage lifecycle                                                  */
/* ------------------------------------------------------------------ */

AiMessage *
ai_message_new (AiMessageRole role, const gchar *content,
                JsonArray *tool_calls, const gchar *tool_call_id,
                const gchar *tool_name)
{
    AiMessage *m      = g_new0 (AiMessage, 1);
    m->role           = role;
    m->content        = content ? g_strdup (content) : NULL;
    m->tool_calls     = tool_calls ? json_array_ref (tool_calls) : NULL;
    m->tool_call_id   = tool_call_id ? g_strdup (tool_call_id) : NULL;
    m->tool_name      = tool_name ? g_strdup (tool_name) : NULL;
    return m;
}

void
ai_message_free (AiMessage *msg)
{
    if (!msg) return;
    g_free (msg->content);
    if (msg->tool_calls) json_array_unref (msg->tool_calls);
    g_free (msg->tool_call_id);
    g_free (msg->tool_name);
    g_free (msg);
}
