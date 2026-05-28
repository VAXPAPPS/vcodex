/*
 * ai_http.c — Professional async HTTP transport layer
 *
 * Implements SSE streaming via libsoup-3.0, automatic retry with
 * exponential back-off, and GCancellable-based request cancellation.
 *
 * Threading model: all callbacks fire on the GLib main loop thread.
 */

#include "ai_http.h"
#include <libsoup/soup.h>
#include <string.h>
#include <stdlib.h>

/* Shared session — created once, reused for all requests */
static SoupSession *g_session = NULL;

/* Ensure the shared session exists */
static SoupSession *
get_session (void)
{
    if (!g_session) {
        g_session = soup_session_new ();
        g_object_set (g_session,
                      "timeout", (guint) 120,
                      "max-conns-per-host", (guint) 8,
                      NULL);
    }
    return g_session;
}

/* ------------------------------------------------------------------ */
/* Internal task context                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Copied from AiHttpRequest */
    gchar              *url;
    GHashTable         *headers;   /* borrowed ref — caller keeps alive */
    gchar              *body_json;
    gboolean            stream;
    GCancellable       *cancel;    /* ref'd */
    AiHttpChunkCallback on_chunk;
    AiHttpDoneCallback  on_done;
    gpointer            user_data;
    guint               max_retries;
    guint               timeout_secs;

    /* State */
    guint               attempt;
    SoupMessage        *msg;

    /* Accumulator for non-streaming mode */
    GString            *full_body;
} HttpTask;

static void do_send (HttpTask *task);  /* forward declaration */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void
task_free (HttpTask *task)
{
    if (!task) return;
    g_free (task->url);
    g_free (task->body_json);
    if (task->cancel)
        g_object_unref (task->cancel);
    if (task->msg)
        g_object_unref (task->msg);
    if (task->full_body)
        g_string_free (task->full_body, TRUE);
    g_free (task);
}

/* Translate HTTP status to a human-readable error string */
static gchar *
http_error_string (guint status, const gchar *reason)
{
    switch (status) {
    case 401:
        return g_strdup ("Authentication failed: Invalid API key (401 Unauthorized)");
    case 403:
        return g_strdup ("Access denied: Check your API key permissions (403 Forbidden)");
    case 429:
        return g_strdup ("Rate limit exceeded: Too many requests. Please wait and retry (429)");
    case 500:
        return g_strdup ("AI provider internal error (500). Try again in a moment.");
    case 502:
    case 503:
        return g_strdup ("AI provider is temporarily unavailable (502/503). Try again later.");
    case 0:
        return g_strdup ("Network error: Cannot reach the AI provider. Check your connection.");
    default:
        return g_strdup_printf ("HTTP error %u: %s", status, reason ? reason : "Unknown");
    }
}

/* Schedule retry with exponential back-off */
typedef struct { HttpTask *task; } RetrySource;

static gboolean
on_retry_timeout (gpointer user_data)
{
    HttpTask *task = (HttpTask *) user_data;
    do_send (task);
    return G_SOURCE_REMOVE;
}

static void
schedule_retry (HttpTask *task)
{
    task->attempt++;
    /* Exponential back-off: 1s, 2s, 4s */
    guint delay_ms = (1u << (task->attempt - 1)) * 1000u;
    g_timeout_add (delay_ms, on_retry_timeout, task);
}

/* ------------------------------------------------------------------ */
/* SSE line parser                                                     */
/* ------------------------------------------------------------------ */

/*
 * Process one SSE line.  Lines starting with "data: " are forwarded
 * to on_chunk.  "data: [DONE]" signals end-of-stream.
 * Returns TRUE if the stream is complete.
 */
static gboolean
process_sse_line (HttpTask *task, const gchar *line)
{
    if (g_str_has_prefix (line, "data: ")) {
        const gchar *payload = line + 6; /* skip "data: " */
        if (g_strcmp0 (payload, "[DONE]") == 0)
            return TRUE;  /* stream finished */
        task->on_chunk (payload, task->user_data);
    }
    /* Lines starting with "event:", "id:", ":" (comment) are ignored */
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Streaming response handler                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    HttpTask       *task;
    GInputStream   *stream;
    GDataInputStream *data_stream;
} StreamCtx;

static void on_stream_line (GObject *src, GAsyncResult *res, gpointer ud);

static void
read_next_line (StreamCtx *ctx)
{
    g_data_input_stream_read_line_async (
        ctx->data_stream,
        G_PRIORITY_DEFAULT,
        ctx->task->cancel,
        on_stream_line,
        ctx);
}

static void
on_stream_line (GObject *src, GAsyncResult *res, gpointer ud)
{
    StreamCtx  *ctx  = (StreamCtx *) ud;
    HttpTask   *task = ctx->task;
    GError     *err  = NULL;
    gsize       len;

    gchar *line = g_data_input_stream_read_line_finish (
                      G_DATA_INPUT_STREAM (src), res, &len, &err);

    if (err) {
        if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            task->on_done (err->message, task->user_data);
        g_error_free (err);
        g_object_unref (ctx->data_stream);
        g_object_unref (ctx->stream);
        g_free (ctx);
        task_free (task);
        return;
    }

    if (line == NULL) {
        /* EOF */
        task->on_done (NULL, task->user_data);
        g_object_unref (ctx->data_stream);
        g_object_unref (ctx->stream);
        g_free (ctx);
        task_free (task);
        return;
    }

    /* Strip trailing \r if any */
    if (len > 0 && line[len - 1] == '\r')
        line[len - 1] = '\0';

    gboolean done = process_sse_line (task, line);
    g_free (line);

    if (done) {
        task->on_done (NULL, task->user_data);
        g_object_unref (ctx->data_stream);
        g_object_unref (ctx->stream);
        g_free (ctx);
        task_free (task);
        return;
    }

    read_next_line (ctx);
}

static void
on_send_async (GObject *src, GAsyncResult *res, gpointer ud)
{
    HttpTask   *task = (HttpTask *) ud;
    GError     *err  = NULL;
    GInputStream *body = soup_session_send_finish (
                             SOUP_SESSION (src), res, &err);

    if (err) {
        if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_error_free (err);
            task_free (task);
            return;
        }
        /* Network-level error → maybe retry */
        g_error_free (err);
        if (task->attempt < task->max_retries) {
            if (task->msg) { g_object_unref (task->msg); task->msg = NULL; }
            schedule_retry (task);
            return;
        }
        task->on_done ("Network error: Unable to reach the AI provider.", task->user_data);
        task_free (task);
        return;
    }

    guint status = soup_message_get_status (task->msg);

    /* 5xx → retry if attempts left */
    if (status >= 500 && task->attempt < task->max_retries) {
        g_object_unref (body);
        if (task->msg) { g_object_unref (task->msg); task->msg = NULL; }
        schedule_retry (task);
        return;
    }

    if (status < 200 || status >= 300) {
        gchar *emsg = http_error_string (status,
                          soup_message_get_reason_phrase (task->msg));
        task->on_done (emsg, task->user_data);
        g_free (emsg);
        g_object_unref (body);
        task_free (task);
        return;
    }

    if (task->stream) {
        /* Kick off line-by-line SSE reading */
        StreamCtx *ctx       = g_new0 (StreamCtx, 1);
        ctx->task            = task;
        ctx->stream          = body; /* takes ref */
        ctx->data_stream     = g_data_input_stream_new (body);
        g_data_input_stream_set_newline_type (ctx->data_stream,
                                              G_DATA_STREAM_NEWLINE_TYPE_ANY);
        read_next_line (ctx);
    } else {
        /* Non-streaming: read entire body then call on_chunk once */
        task->full_body = g_string_new ("");
        /* We'll use read_bytes_async in a loop */
        /* For simplicity, use a helper that reads everything */
        GError *rerr = NULL;
        guchar buf[4096];
        gssize n;
        while ((n = g_input_stream_read (body, buf, sizeof(buf), NULL, &rerr)) > 0) {
            g_string_append_len (task->full_body, (gchar*)buf, n);
        }
        g_object_unref (body);
        if (rerr) {
            task->on_done (rerr->message, task->user_data);
            g_error_free (rerr);
        } else {
            task->on_chunk (task->full_body->str, task->user_data);
            task->on_done (NULL, task->user_data);
        }
        task_free (task);
    }
}

/* ------------------------------------------------------------------ */
/* Request builder & sender                                            */
/* ------------------------------------------------------------------ */

static void
do_send (HttpTask *task)
{
    SoupMessage *msg = soup_message_new ("POST", task->url);

    /* Set body */
    GBytes *body_bytes = g_bytes_new_static (task->body_json,
                                             strlen (task->body_json));
    soup_message_set_request_body_from_bytes (msg, "application/json", body_bytes);
    g_bytes_unref (body_bytes);

    /* Apply headers */
    if (task->headers) {
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init (&it, task->headers);
        while (g_hash_table_iter_next (&it, &k, &v))
            soup_message_headers_replace (soup_message_get_request_headers (msg),
                                          (gchar*)k, (gchar*)v);
    }

    /* Accept SSE for streaming */
    if (task->stream)
        soup_message_headers_replace (soup_message_get_request_headers (msg),
                                      "Accept", "text/event-stream");

    if (task->msg)
        g_object_unref (task->msg);
    task->msg = g_object_ref (msg);

    soup_session_send_async (get_session (), msg,
                             G_PRIORITY_DEFAULT,
                             task->cancel,
                             on_send_async,
                             task);
    g_object_unref (msg);
}

/* ------------------------------------------------------------------ */
/* Public API implementation                                           */
/* ------------------------------------------------------------------ */

AiHttpRequest *
ai_http_request_new (void)
{
    AiHttpRequest *r = g_new0 (AiHttpRequest, 1);
    r->max_retries   = 2;
    r->timeout_secs  = 120;
    return r;
}

void
ai_http_request_free (AiHttpRequest *req)
{
    g_free (req);
}

GHashTable *
ai_http_make_headers (void)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

void
ai_http_send (const AiHttpRequest *req)
{
    g_return_if_fail (req != NULL);
    g_return_if_fail (req->url != NULL);
    g_return_if_fail (req->body_json != NULL);
    g_return_if_fail (req->on_chunk != NULL);
    g_return_if_fail (req->on_done != NULL);

    HttpTask *task       = g_new0 (HttpTask, 1);
    task->url            = g_strdup (req->url);
    task->headers        = req->headers;  /* caller keeps alive during send */
    task->body_json      = g_strdup (req->body_json);
    task->stream         = req->stream;
    task->cancel         = req->cancel ? g_object_ref (req->cancel) : NULL;
    task->on_chunk       = req->on_chunk;
    task->on_done        = req->on_done;
    task->user_data      = req->user_data;
    task->max_retries    = req->max_retries;
    task->timeout_secs   = req->timeout_secs;
    task->attempt        = 0;

    do_send (task);
}
