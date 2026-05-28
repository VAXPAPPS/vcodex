#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

/*
 * ai_provider.h — Multi-provider adapter layer
 *
 * Defines a unified adapter interface for all supported AI providers.
 * Each provider translates between the internal canonical message format
 * and the provider-specific HTTP request/response wire format.
 *
 * Supported providers (13 total):
 *   OpenAI, Anthropic, Google Gemini, Mistral AI, Groq,
 *   Cohere, DeepSeek, xAI (Grok), Azure OpenAI,
 *   OpenRouter, Together AI, Ollama (local), Custom (OpenAI-compat.)
 */

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Provider enumeration                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    AI_PROVIDER_OPENAI      = 0,
    AI_PROVIDER_ANTHROPIC   = 1,
    AI_PROVIDER_GEMINI      = 2,
    AI_PROVIDER_MISTRAL     = 3,
    AI_PROVIDER_GROQ        = 4,
    AI_PROVIDER_COHERE      = 5,
    AI_PROVIDER_DEEPSEEK    = 6,
    AI_PROVIDER_XAI         = 7,
    AI_PROVIDER_AZURE       = 8,
    AI_PROVIDER_OPENROUTER  = 9,
    AI_PROVIDER_TOGETHER    = 10,
    AI_PROVIDER_OLLAMA      = 11,
    AI_PROVIDER_CUSTOM      = 12,
    AI_PROVIDER_COUNT       = 13
} AiProviderType;

/* ------------------------------------------------------------------ */
/* Known model catalogue per provider                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    const gchar *id;           /* API model identifier */
    const gchar *display_name; /* Human-readable label */
    gboolean     supports_vision;
    gboolean     supports_tools;
} AiModelInfo;

/* ------------------------------------------------------------------ */
/* Canonical message representation                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    AI_ROLE_SYSTEM    = 0,
    AI_ROLE_USER      = 1,
    AI_ROLE_ASSISTANT = 2,
    AI_ROLE_TOOL      = 3
} AiMessageRole;

typedef struct {
    AiMessageRole  role;
    gchar         *content;        /* Text content (may be NULL if tool_calls set) */
    JsonArray     *tool_calls;     /* For assistant messages with tool use */
    gchar         *tool_call_id;   /* For tool result messages */
    gchar         *tool_name;      /* For tool result messages */
} AiMessage;

/* ------------------------------------------------------------------ */
/* Provider adapter interface                                           */
/* ------------------------------------------------------------------ */

typedef struct _AiProviderAdapter AiProviderAdapter;

struct _AiProviderAdapter {
    /* Identity */
    AiProviderType  type;
    const gchar    *name;             /* "OpenAI", "Anthropic", … */
    const gchar    *default_base_url;
    const gchar    *env_key_name;     /* e.g. "OPENAI_API_KEY" */

    /* Known models for this provider (NULL-terminated) */
    const AiModelInfo *models;
    guint              models_count;

    /**
     * build_request_body:
     * Converts the message list + settings into a JSON string ready to POST.
     * Caller must g_free() the result.
     */
    gchar* (*build_request_body) (const AiProviderAdapter *adapter,
                                  GPtrArray               *messages, /* AiMessage* */
                                  const gchar             *model,
                                  JsonNode                *tools,    /* may be NULL */
                                  gboolean                 stream,
                                  gint                     max_tokens,
                                  gdouble                  temperature);

    /**
     * set_auth_headers:
     * Populates @headers (GHashTable<gchar*,gchar*>) with the provider-
     * specific authentication and version headers.
     */
    void (*set_auth_headers) (const AiProviderAdapter *adapter,
                              GHashTable              *headers,
                              const gchar             *api_key,
                              const gchar             *base_url);  /* used by Azure */

    /**
     * get_endpoint_url:
     * Returns the full POST URL for the given base_url and model.
     * Caller must g_free() the result.
     */
    gchar* (*get_endpoint_url) (const AiProviderAdapter *adapter,
                                const gchar             *base_url,
                                const gchar             *model,
                                const gchar             *api_key); /* Gemini uses key in URL */

    /**
     * parse_stream_chunk:
     * Given one SSE "data:" payload, extract the text token.
     * Returns newly allocated string (token) or NULL (non-text chunk).
     * Returns special sentinel AI_PROVIDER_STREAM_DONE when stream ends.
     */
    gchar* (*parse_stream_chunk) (const AiProviderAdapter *adapter,
                                  const gchar             *raw_chunk);

    /**
     * parse_full_response:
     * Given the complete response body (non-streaming), extract:
     *   - text content → *text_out (caller g_free())
     *   - tool calls   → *tool_calls_out (caller json_array_unref())
     * Returns TRUE on success, FALSE on parse error.
     */
    gboolean (*parse_full_response) (const AiProviderAdapter  *adapter,
                                     const gchar              *body,
                                     gchar                   **text_out,
                                     JsonArray               **tool_calls_out);
};

/* Sentinel returned by parse_stream_chunk to signal end-of-stream */
#define AI_PROVIDER_STREAM_DONE  ((gchar*)(gpointer)0x1)

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * ai_provider_get:
 * Returns the adapter for the given provider type. The returned pointer
 * is static — do NOT free it.
 */
const AiProviderAdapter *ai_provider_get (AiProviderType type);

/**
 * ai_provider_get_name:
 * Returns the human-readable provider name.
 */
const gchar *ai_provider_get_name (AiProviderType type);

/**
 * ai_provider_get_default_url:
 */
const gchar *ai_provider_get_default_url (AiProviderType type);

/**
 * ai_provider_get_env_key:
 * Returns the environment variable name for this provider's API key.
 */
const gchar *ai_provider_get_env_key (AiProviderType type);

/**
 * ai_provider_get_models:
 * @count: (out): number of models returned.
 * Returns the static model catalogue for this provider.
 */
const AiModelInfo *ai_provider_get_models (AiProviderType type, guint *count);

/**
 * ai_message_new:
 * Allocates a new AiMessage. Caller must call ai_message_free().
 */
AiMessage *ai_message_new (AiMessageRole  role,
                            const gchar   *content,
                            JsonArray     *tool_calls,
                            const gchar   *tool_call_id,
                            const gchar   *tool_name);

/**
 * ai_message_free:
 */
void ai_message_free (AiMessage *msg);

G_END_DECLS

#endif /* AI_PROVIDER_H */
