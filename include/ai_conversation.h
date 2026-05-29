#ifndef AI_CONVERSATION_H
#define AI_CONVERSATION_H

/*
 * ai_conversation.h — Conversation persistence layer
 *
 * Handles durable storage of all AI conversations to
 *   ~/.local/share/vcodex/conversations/{id}.jsonl
 *
 * File format (one JSON object per line):
 *   Line 1: {"type":"meta","id":"...","title":"...","created":N,"modified":N,"turns":N}
 *   Line 2+: {"type":"msg","role":"user|assistant|tool|system",
 *              "content":"...","tool_call_id":"...","tool_name":"...","ts":N}
 *
 * Design principles:
 *   - Append-only writes (fast, safe against partial-write corruption)
 *   - Metadata updated by rewriting only the first line via in-place seek
 *   - Full conversation loaded on demand only (not kept in memory)
 *   - Thread-safe: all operations run on the GLib main thread
 */

#include <glib.h>
#include "ai_provider.h"   /* AiMessage, AiMessageRole */

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Conversation metadata                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar  *id;           /* UUID-like identifier                 */
    gchar  *title;        /* Human-readable title (auto-generated) */
    gint64  created_at;   /* Unix timestamp (seconds)             */
    gint64  modified_at;  /* Unix timestamp (seconds)             */
    gint    turn_count;   /* Number of user↔assistant turns       */
    gchar  *file_path;    /* Absolute path to the .jsonl file     */
} AiConversationMeta;

/* ------------------------------------------------------------------ */
/* Core API                                                             */
/* ------------------------------------------------------------------ */

/**
 * ai_conversation_new_id:
 * Generate a fresh conversation ID and create its backing file.
 * Returns a newly-allocated string (e.g. "conv-1748500000-a3f9").
 * Caller must g_free().
 */
gchar *ai_conversation_new_id (void);

/**
 * ai_conversation_append_message:
 * Atomically append one message to the conversation's JSONL file.
 * Call once per message (user, assistant, or tool).
 *
 * @conv_id:       Conversation identifier.
 * @role:          Message role.
 * @content:       Text content (may be NULL for pure tool-call messages).
 * @tool_call_id:  For tool result messages; NULL otherwise.
 * @tool_name:     For tool result messages; NULL otherwise.
 */
void ai_conversation_append_message (const gchar    *conv_id,
                                     AiMessageRole   role,
                                     const gchar    *content,
                                     const gchar    *tool_call_id,
                                     const gchar    *tool_name);

/**
 * ai_conversation_increment_turn:
 * Increment the turn counter in the conversation metadata.
 * Call once after each complete user↔assistant exchange.
 */
void ai_conversation_increment_turn (const gchar *conv_id);

/**
 * ai_conversation_set_title:
 * Update the conversation title. The JSONL metadata line is rewritten.
 */
void ai_conversation_set_title (const gchar *conv_id, const gchar *title);

/**
 * ai_conversation_load_messages:
 * Read all messages from a conversation file.
 * Returns a GPtrArray<AiMessage*> (skips the metadata line).
 * Caller must g_ptr_array_free (result, TRUE).
 * Returns NULL on error.
 */
GPtrArray *ai_conversation_load_messages (const gchar *conv_id);

/**
 * ai_conversation_list_all:
 * Scan the conversations directory and return all conversations,
 * sorted by modified_at descending (most recent first).
 * Returns GPtrArray<AiConversationMeta*>.
 * Caller must g_ptr_array_free (result, TRUE).
 */
GPtrArray *ai_conversation_list_all (void);

/**
 * ai_conversation_delete:
 * Permanently delete a conversation file.
 */
void ai_conversation_delete (const gchar *conv_id);

/**
 * ai_conversation_meta_free:
 * Free an AiConversationMeta and all its string fields.
 */
void ai_conversation_meta_free (AiConversationMeta *meta);

/**
 * ai_conversation_get_path:
 * Returns the absolute path to the conversation file.
 * Caller must g_free().
 */
gchar *ai_conversation_get_path (const gchar *conv_id);

/**
 * ai_conversation_auto_title:
 * Generate a clean title from the first user message text.
 * Truncates at word boundary ≤ 48 chars, appends "…" if truncated.
 * Returns a newly-allocated string. Caller must g_free().
 */
gchar *ai_conversation_auto_title (const gchar *first_user_message);

G_END_DECLS

#endif /* AI_CONVERSATION_H */
