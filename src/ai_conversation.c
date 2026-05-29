/*
 * ai_conversation.c — Conversation persistence layer
 *
 * Storage layout:
 *   ~/.local/share/vcodex/conversations/
 *     ├── conv-1748500000-a3f9.jsonl
 *     ├── conv-1748501234-b7c2.jsonl
 *     └── ...
 *
 * JSONL format per file:
 *   Line 1 (metadata, fixed-width padded to 512 bytes for in-place rewrite):
 *     {"type":"meta","id":"conv-...","title":"...","created":N,"modified":N,"turns":N}
 *     ← padded with spaces to exactly META_LINE_SIZE bytes + '\n' →
 *
 *   Lines 2+ (messages):
 *     {"type":"msg","role":"user","content":"...","ts":N}
 *     {"type":"msg","role":"assistant","content":"...","ts":N}
 *     {"type":"msg","role":"tool","content":"...","tool_call_id":"...","tool_name":"...","ts":N}
 *
 * The fixed-width metadata line allows O(1) in-place updates of title/turns
 * without rewriting the entire file.
 */

#include "ai_conversation.h"
#include <json-glib/json-glib.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

/* The first line of every conversation file is padded to this many bytes
 * (excluding the newline), allowing in-place rewrite without shifting data. */
#define META_LINE_SIZE  511   /* + '\n' = 512 bytes total */

/* ------------------------------------------------------------------ */
/* Directory helpers                                                    */
/* ------------------------------------------------------------------ */

/* Forward declarations for functions defined later */
static gboolean write_meta_line (FILE *fp, const gchar *id, const gchar *title,
                                  gint64 created_at, gint64 modified_at, gint turns);

static gchar *
get_conv_dir (void)
{
    const gchar *data_dir = g_get_user_data_dir ();
    gchar *dir = g_build_filename (data_dir, "vcodex", "conversations", NULL);
    if (g_mkdir_with_parents (dir, 0700) != 0 && errno != EEXIST)
        g_warning ("Failed to create conversations directory: %s", g_strerror (errno));
    return dir;
}

gchar *
ai_conversation_get_path (const gchar *conv_id)
{
    gchar *dir  = get_conv_dir ();
    gchar *filename = g_strdup_printf ("%s.jsonl", conv_id);
    gchar *path = g_build_filename (dir, filename, NULL);
    g_free (dir);
    g_free (filename);
    return path;
}

/* ------------------------------------------------------------------ */
/* ID generation                                                        */
/* ------------------------------------------------------------------ */

/* Generate a raw ID string without creating a file. Internal use only. */
static gchar *
generate_id_string (void)
{
    gint64  ts   = g_get_real_time () / 1000000;   /* seconds */
    guint32 rand = g_random_int () & 0xFFFF;        /* 4 hex digits */
    return g_strdup_printf ("conv-%"G_GINT64_FORMAT"-%04x", ts, rand);
}

/**
 * ai_conversation_new_id:
 * Generates a new conversation ID AND creates the backing JSONL file.
 */
gchar *
ai_conversation_new_id (void)
{
    gchar *id   = generate_id_string ();
    gchar *path = ai_conversation_get_path (id);
    gint64 now  = g_get_real_time () / 1000000;

    FILE *fp = fopen (path, "wb");
    if (!fp) {
        g_warning ("Cannot create conversation file '%s': %s",
                   path, g_strerror (errno));
    } else {
        write_meta_line (fp, id, "New Chat", now, now, 0);
        fclose (fp);
    }

    g_free (path);
    return id;  /* Caller owns the string */
}


/* Build the metadata JSON string (unpadded) */
static gchar *
build_meta_json (const gchar *id,
                 const gchar *title,
                 gint64       created_at,
                 gint64       modified_at,
                 gint         turns)
{
    JsonObject *obj = json_object_new ();
    json_object_set_string_member (obj, "type",     "meta");
    json_object_set_string_member (obj, "id",       id);
    json_object_set_string_member (obj, "title",    title ? title : "New Chat");
    json_object_set_int_member    (obj, "created",  created_at);
    json_object_set_int_member    (obj, "modified", modified_at);
    json_object_set_int_member    (obj, "turns",    turns);

    JsonNode *node = json_node_new (JSON_NODE_OBJECT);
    json_node_set_object (node, obj);
    json_object_unref (obj);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_root (gen, node);
    json_generator_set_pretty (gen, FALSE);
    gchar *str = json_generator_to_data (gen, NULL);
    g_object_unref (gen);
    json_node_free (node);
    return str;
}

/* Write a padded metadata line to an open FILE* at position 0 */
static gboolean
write_meta_line (FILE        *fp,
                 const gchar *id,
                 const gchar *title,
                 gint64       created_at,
                 gint64       modified_at,
                 gint         turns)
{
    gchar *json = build_meta_json (id, title, created_at, modified_at, turns);
    gsize  len  = strlen (json);

    if (len > META_LINE_SIZE) {
        g_warning ("Metadata JSON too long (%zu bytes), truncating title", len);
        /* Shorten title and rebuild */
        gchar *short_title = g_strndup (title ? title : "Chat", 20);
        g_free (json);
        json = build_meta_json (id, short_title, created_at, modified_at, turns);
        g_free (short_title);
        len = strlen (json);
    }

    /* Pad with spaces to META_LINE_SIZE, then '\n' */
    fseek (fp, 0, SEEK_SET);
    fwrite (json, 1, len, fp);
    for (gsize i = len; i < META_LINE_SIZE; i++)
        fputc (' ', fp);
    fputc ('\n', fp);
    fflush (fp);

    g_free (json);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Metadata serialisation                                               */
/* ------------------------------------------------------------------ */

static const gchar *
role_to_string (AiMessageRole role)
{
    switch (role) {
    case AI_ROLE_SYSTEM:    return "system";
    case AI_ROLE_USER:      return "user";
    case AI_ROLE_ASSISTANT: return "assistant";
    case AI_ROLE_TOOL:      return "tool";
    default:                return "user";
    }
}

void
ai_conversation_append_message (const gchar   *conv_id,
                                AiMessageRole  role,
                                const gchar   *content,
                                const gchar   *tool_call_id,
                                const gchar   *tool_name)
{
    if (!conv_id || !*conv_id) return;

    gchar *path = ai_conversation_get_path (conv_id);
    gint64 now  = g_get_real_time () / 1000000;

    /* Build message JSON */
    JsonObject *obj = json_object_new ();
    json_object_set_string_member (obj, "type",    "msg");
    json_object_set_string_member (obj, "role",    role_to_string (role));
    json_object_set_int_member    (obj, "ts",      now);

    if (content)
        json_object_set_string_member (obj, "content", content);
    if (tool_call_id)
        json_object_set_string_member (obj, "tool_call_id", tool_call_id);
    if (tool_name)
        json_object_set_string_member (obj, "tool_name", tool_name);

    JsonNode *node = json_node_new (JSON_NODE_OBJECT);
    json_node_set_object (node, obj);
    json_object_unref (obj);

    JsonGenerator *gen = json_generator_new ();
    json_generator_set_root (gen, node);
    json_generator_set_pretty (gen, FALSE);
    gchar *line = json_generator_to_data (gen, NULL);
    g_object_unref (gen);
    json_node_free (node);

    /* Append to file */
    FILE *fp = fopen (path, "ab");
    if (fp) {
        /* Ensure file exists and has metadata (create if brand new) */
        long size = 0;
        fseek (fp, 0, SEEK_END);
        size = ftell (fp);
        if (size == 0) {
            /* Brand new file — write metadata first */
            fclose (fp);
            fp = fopen (path, "wb");
            if (fp) {
                write_meta_line (fp, conv_id, "New Chat", now, now, 0);
                fclose (fp);
            }
            fp = fopen (path, "ab");
        }

        if (fp) {
            fprintf (fp, "%s\n", line);
            fclose (fp);
        }
    } else {
        g_warning ("Cannot open conversation file for append: %s", path);
    }

    g_free (line);
    g_free (path);
}

/* ------------------------------------------------------------------ */
/* Increment turn counter + update modified timestamp                   */
/* ------------------------------------------------------------------ */

void
ai_conversation_increment_turn (const gchar *conv_id)
{
    if (!conv_id || !*conv_id) return;

    gchar *path = ai_conversation_get_path (conv_id);
    FILE  *fp   = fopen (path, "r+b");
    if (!fp) { g_free (path); return; }

    /* Read the first META_LINE_SIZE bytes (the metadata line) */
    gchar meta_buf[META_LINE_SIZE + 2];
    gsize n = fread (meta_buf, 1, META_LINE_SIZE + 1, fp);
    meta_buf[n] = '\0';

    /* Strip trailing spaces/newline for JSON parsing */
    gsize len = strlen (meta_buf);
    while (len > 0 && (meta_buf[len-1] == ' ' || meta_buf[len-1] == '\n' || meta_buf[len-1] == '\r'))
        meta_buf[--len] = '\0';

    /* Parse existing metadata */
    GError *err = NULL;
    JsonParser *parser = json_parser_new ();
    gchar *id    = NULL;
    gchar *title = NULL;
    gint64 created = g_get_real_time () / 1000000;
    gint   turns   = 0;

    if (json_parser_load_from_data (parser, meta_buf, -1, &err)) {
        JsonObject *meta = json_node_get_object (json_parser_get_root (parser));
        if (meta) {
            id      = g_strdup (json_object_get_string_member_with_default (meta, "id",      conv_id));
            title   = g_strdup (json_object_get_string_member_with_default (meta, "title",   "New Chat"));
            created = json_object_has_member (meta, "created") ?
                      json_object_get_int_member (meta, "created") : created;
            turns   = (gint)(json_object_has_member (meta, "turns") ?
                      json_object_get_int_member (meta, "turns") : 0);
        }
    } else {
        g_clear_error (&err);
        id    = g_strdup (conv_id);
        title = g_strdup ("New Chat");
    }
    g_object_unref (parser);

    gint64 now = g_get_real_time () / 1000000;
    write_meta_line (fp, id ? id : conv_id, title ? title : "New Chat",
                     created, now, turns + 1);

    g_free (id);
    g_free (title);
    fclose (fp);
    g_free (path);
}

/* ------------------------------------------------------------------ */
/* Set title                                                            */
/* ------------------------------------------------------------------ */

void
ai_conversation_set_title (const gchar *conv_id, const gchar *title)
{
    if (!conv_id || !*conv_id || !title) return;

    gchar *path = ai_conversation_get_path (conv_id);
    FILE  *fp   = fopen (path, "r+b");
    if (!fp) { g_free (path); return; }

    gchar meta_buf[META_LINE_SIZE + 2];
    gsize n = fread (meta_buf, 1, META_LINE_SIZE + 1, fp);
    meta_buf[n] = '\0';

    gsize len = strlen (meta_buf);
    while (len > 0 && (meta_buf[len-1] == ' ' || meta_buf[len-1] == '\n' || meta_buf[len-1] == '\r'))
        meta_buf[--len] = '\0';

    GError *err = NULL;
    JsonParser *parser = json_parser_new ();

    gchar *id       = NULL;
    gint64 created  = g_get_real_time () / 1000000;
    gint64 modified = created;
    gint   turns    = 0;

    if (json_parser_load_from_data (parser, meta_buf, -1, &err)) {
        JsonObject *meta = json_node_get_object (json_parser_get_root (parser));
        if (meta) {
            id       = g_strdup (json_object_get_string_member_with_default (meta, "id", conv_id));
            created  = json_object_has_member (meta, "created")  ? json_object_get_int_member (meta, "created")  : created;
            modified = json_object_has_member (meta, "modified") ? json_object_get_int_member (meta, "modified") : modified;
            turns    = (gint)(json_object_has_member (meta, "turns") ? json_object_get_int_member (meta, "turns") : 0);
        }
    } else {
        g_clear_error (&err);
        id = g_strdup (conv_id);
    }
    g_object_unref (parser);

    write_meta_line (fp, id ? id : conv_id, title, created, modified, turns);

    g_free (id);
    fclose (fp);
    g_free (path);
}

/* ------------------------------------------------------------------ */
/* Load messages                                                         */
/* ------------------------------------------------------------------ */

static AiMessageRole
string_to_role (const gchar *s)
{
    if (!s)                        return AI_ROLE_USER;
    if (g_strcmp0 (s, "assistant") == 0) return AI_ROLE_ASSISTANT;
    if (g_strcmp0 (s, "system")    == 0) return AI_ROLE_SYSTEM;
    if (g_strcmp0 (s, "tool")      == 0) return AI_ROLE_TOOL;
    return AI_ROLE_USER;
}

GPtrArray *
ai_conversation_load_messages (const gchar *conv_id)
{
    if (!conv_id || !*conv_id) return NULL;

    gchar *path = ai_conversation_get_path (conv_id);
    GPtrArray *messages = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_message_free);

    GError *file_err = NULL;
    gchar  *content  = NULL;
    if (!g_file_get_contents (path, &content, NULL, &file_err)) {
        g_warning ("Cannot read conversation '%s': %s", path, file_err->message);
        g_error_free (file_err);
        g_free (path);
        return messages;  /* Return empty array */
    }
    g_free (path);

    gchar **lines = g_strsplit (content, "\n", -1);
    g_free (content);

    GError *parse_err = NULL;
    for (gint i = 0; lines[i]; i++) {
        const gchar *line = g_strstrip ((gchar *)lines[i]);
        if (!*line || *line != '{') continue;

        JsonParser *parser = json_parser_new ();
        if (!json_parser_load_from_data (parser, line, -1, &parse_err)) {
            g_clear_error (&parse_err);
            g_object_unref (parser);
            continue;
        }

        JsonObject *obj = json_node_get_object (json_parser_get_root (parser));
        if (!obj) { g_object_unref (parser); continue; }

        const gchar *type = json_object_get_string_member_with_default (obj, "type", "");
        if (g_strcmp0 (type, "msg") != 0) { g_object_unref (parser); continue; }

        const gchar *role_str     = json_object_get_string_member_with_default (obj, "role",          "user");
        const gchar *msg_content  = json_object_get_string_member_with_default (obj, "content",       "");
        const gchar *tc_id        = json_object_has_member (obj, "tool_call_id") ?
                                    json_object_get_string_member (obj, "tool_call_id") : NULL;
        const gchar *t_name       = json_object_has_member (obj, "tool_name") ?
                                    json_object_get_string_member (obj, "tool_name") : NULL;

        AiMessageRole role = string_to_role (role_str);
        /* Skip system messages — they're auto-regenerated from IDE context */
        if (role != AI_ROLE_SYSTEM) {
            g_ptr_array_add (messages,
                ai_message_new (role, msg_content, NULL, tc_id, t_name));
        }

        g_object_unref (parser);
    }

    g_strfreev (lines);
    return messages;
}

/* ------------------------------------------------------------------ */
/* List all conversations                                               */
/* ------------------------------------------------------------------ */

void
ai_conversation_meta_free (AiConversationMeta *meta)
{
    if (!meta) return;
    g_free (meta->id);
    g_free (meta->title);
    g_free (meta->file_path);
    g_free (meta);
}

static AiConversationMeta *
read_meta_from_file (const gchar *file_path)
{
    FILE *fp = fopen (file_path, "rb");
    if (!fp) return NULL;

    gchar meta_buf[META_LINE_SIZE + 2];
    gsize n = fread (meta_buf, 1, META_LINE_SIZE + 1, fp);
    fclose (fp);
    meta_buf[n] = '\0';

    gsize len = strlen (meta_buf);
    while (len > 0 && (meta_buf[len-1] == ' ' || meta_buf[len-1] == '\n' || meta_buf[len-1] == '\r'))
        meta_buf[--len] = '\0';

    if (!*meta_buf) return NULL;

    GError *err = NULL;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, meta_buf, -1, &err)) {
        g_clear_error (&err);
        g_object_unref (parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object (json_parser_get_root (parser));
    if (!obj) { g_object_unref (parser); return NULL; }

    const gchar *type = json_object_get_string_member_with_default (obj, "type", "");
    if (g_strcmp0 (type, "meta") != 0) { g_object_unref (parser); return NULL; }

    AiConversationMeta *meta = g_new0 (AiConversationMeta, 1);
    meta->id          = g_strdup (json_object_get_string_member_with_default (obj, "id",    ""));
    meta->title       = g_strdup (json_object_get_string_member_with_default (obj, "title", "Untitled"));
    meta->created_at  = json_object_has_member (obj, "created")  ? json_object_get_int_member (obj, "created")  : 0;
    meta->modified_at = json_object_has_member (obj, "modified") ? json_object_get_int_member (obj, "modified") : 0;
    meta->turn_count  = (gint)(json_object_has_member (obj, "turns") ? json_object_get_int_member (obj, "turns") : 0);
    meta->file_path   = g_strdup (file_path);

    g_object_unref (parser);
    return meta;
}

static gint
compare_meta_by_modified (gconstpointer a, gconstpointer b)
{
    const AiConversationMeta *ma = *(const AiConversationMeta *const *)a;
    const AiConversationMeta *mb = *(const AiConversationMeta *const *)b;
    /* Descending order (most recent first) */
    if (ma->modified_at > mb->modified_at) return -1;
    if (ma->modified_at < mb->modified_at) return  1;
    return 0;
}

GPtrArray *
ai_conversation_list_all (void)
{
    GPtrArray *list = g_ptr_array_new_with_free_func ((GDestroyNotify)ai_conversation_meta_free);

    gchar *dir = get_conv_dir ();
    GDir  *d   = g_dir_open (dir, 0, NULL);
    if (!d) { g_free (dir); return list; }

    const gchar *filename;
    while ((filename = g_dir_read_name (d)) != NULL) {
        if (!g_str_has_suffix (filename, ".jsonl")) continue;

        gchar *fpath = g_build_filename (dir, filename, NULL);
        AiConversationMeta *meta = read_meta_from_file (fpath);
        g_free (fpath);

        if (meta && meta->id && *meta->id)
            g_ptr_array_add (list, meta);
        else if (meta)
            ai_conversation_meta_free (meta);
    }

    g_dir_close (d);
    g_free (dir);

    g_ptr_array_sort (list, compare_meta_by_modified);
    return list;
}

/* ------------------------------------------------------------------ */
/* Delete                                                               */
/* ------------------------------------------------------------------ */

void
ai_conversation_delete (const gchar *conv_id)
{
    if (!conv_id || !*conv_id) return;
    gchar *path = ai_conversation_get_path (conv_id);
    if (g_unlink (path) != 0)
        g_warning ("Failed to delete conversation '%s': %s", path, g_strerror (errno));
    g_free (path);
}

/* ------------------------------------------------------------------ */
/* Auto-title generation                                               */
/* ------------------------------------------------------------------ */

gchar *
ai_conversation_auto_title (const gchar *first_user_message)
{
    if (!first_user_message || !*first_user_message)
        return g_strdup ("New Chat");

    /* Strip leading markdown context prefix if present (e.g. "**Active file:**...") */
    const gchar *text = first_user_message;
    if (g_str_has_prefix (text, "**Active file:**")) {
        /* Find the actual user text after "\n\n" */
        const gchar *sep = strstr (text, "\n\n");
        if (sep) text = sep + 2;
    }

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\n' || *text == '\r') text++;

    /* Strip leading markdown syntax characters */
    while (*text == '#' || *text == '*' || *text == '`' || *text == '-' || *text == '>') text++;
    while (*text == ' ') text++;

    if (!*text) return g_strdup ("New Chat");

    /* Take up to 48 characters, breaking at word boundary */
    gsize max_len = 48;
    gsize len     = strlen (text);

    if (len <= max_len)
        return g_strdup (text);

    /* Find last space within limit */
    gsize cut = max_len;
    while (cut > 20 && text[cut] != ' ') cut--;
    if (cut <= 20) cut = max_len;

    gchar *title = g_strndup (text, cut);
    gchar *result = g_strdup_printf ("%s…", title);
    g_free (title);
    return result;
}

/* ------------------------------------------------------------------ */
/* Public new_id — aliases the file-creating version                   */
/* ------------------------------------------------------------------ */

/* Note: ai_conversation_new_id() is defined earlier in this file.
 * The function automatically creates the backing JSONL file. */

