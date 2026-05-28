#ifndef AI_HTTP_H
#define AI_HTTP_H

/*
 * ai_http.h — Professional async HTTP transport for the AI Agent
 *
 * Provides SSE (Server-Sent Events) streaming, automatic retry with
 * exponential back-off, GCancellable-based cancellation, and unified
 * HTTP error handling for all AI provider requests.
 */

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Callback types                                                       */
/* ------------------------------------------------------------------ */

/**
 * AiHttpChunkCallback:
 * @chunk: A raw SSE "data:" line value (NOT NULL-terminated safe — copy it).
 * @user_data: Caller-supplied pointer.
 *
 * Invoked on the GLib main thread for every SSE chunk received.
 * The chunk string is valid only for the duration of this callback.
 */
typedef void (*AiHttpChunkCallback) (const gchar *chunk,
                                     gpointer     user_data);

/**
 * AiHttpDoneCallback:
 * @error_msg: NULL on success, human-readable error string on failure.
 * @user_data: Caller-supplied pointer.
 *
 * Invoked on the GLib main thread when the request completes (success or
 * failure). After this callback, the AiHttpRequest must not be touched.
 */
typedef void (*AiHttpDoneCallback)  (const gchar *error_msg,
                                     gpointer     user_data);

/* ------------------------------------------------------------------ */
/* Request descriptor                                                   */
/* ------------------------------------------------------------------ */

/**
 * AiHttpRequest:
 *
 * Describes a single HTTP request. Fill all fields then call ai_http_send().
 * The struct is managed by the caller; ai_http_send() makes internal copies
 * of strings so the caller may free them after the call returns.
 *
 * Set @stream = TRUE to use SSE chunked mode (on_chunk is called repeatedly).
 * Set @stream = FALSE for a single full response (on_chunk is called once
 * with the complete body, then on_done is called).
 */
typedef struct {
    /* Target */
    const gchar        *url;

    /* Headers: key → value (both gchar*, owned by caller) */
    GHashTable         *headers;

    /* JSON request body */
    const gchar        *body_json;

    /* If TRUE, parse Server-Sent Events and call on_chunk per token */
    gboolean            stream;

    /* Cancellation token — caller owns this; may be NULL */
    GCancellable       *cancel;

    /* Callbacks — both must be non-NULL */
    AiHttpChunkCallback on_chunk;
    AiHttpDoneCallback  on_done;
    gpointer            user_data;

    /* Retry policy (0 = no retries) */
    guint               max_retries;    /* default: 2 */
    guint               timeout_secs;   /* default: 120 */
} AiHttpRequest;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * ai_http_request_new:
 *
 * Allocates a zeroed AiHttpRequest with sensible defaults.
 * Free with ai_http_request_free() when done (before calling ai_http_send
 * is also fine — send() copies what it needs).
 */
AiHttpRequest *ai_http_request_new (void);

/**
 * ai_http_request_free:
 *
 * Frees an AiHttpRequest allocated by ai_http_request_new().
 * Does NOT free the headers GHashTable — caller owns that.
 */
void ai_http_request_free (AiHttpRequest *req);

/**
 * ai_http_send:
 * @req: Fully populated request descriptor.
 *
 * Initiates the async HTTP request. Returns immediately.
 * @req->on_chunk and @req->on_done are called on the GLib main loop.
 */
void ai_http_send (const AiHttpRequest *req);

/**
 * ai_http_make_headers:
 *
 * Convenience: creates a new GHashTable<gchar*,gchar*> suitable for
 * AiHttpRequest.headers. Destroy with g_hash_table_destroy().
 */
GHashTable *ai_http_make_headers (void);

G_END_DECLS

#endif /* AI_HTTP_H */
