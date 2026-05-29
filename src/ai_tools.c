/*
 * ai_tools.c — Native tool implementations for the AI Agent
 *
 * 17 tools organised by category:
 *   File I/O:    view_file, edit_file, replace_range, create_file,
 *                delete_file, get_active_file, open_file_in_editor
 *   Navigation:  list_dir, find_files, get_project_structure
 *   Code search: search_in_files
 *   Selection:   get_selection, insert_at_cursor
 *   Shell:       run_command
 *   Diff:        apply_diff
 *   IDE:         get_diagnostics
 *
 * Sandboxing: all path operations are confined to the workspace directory.
 * Dangerous tools (delete_file, run_command) require user confirmation.
 */

#include "ai_tools.h"
#include "ai_context.h"
#include "vcodex_window_private.h"
#include "editor_tab.h"
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

/* ------------------------------------------------------------------ */
/* Global tool context                                                  */
/* ------------------------------------------------------------------ */

static AiToolContext g_tool_ctx = { NULL, NULL };

/* Permission overrides: tool_name → AiToolPermission */
static GHashTable *g_permissions = NULL;

/* Default permissions */
typedef struct { const gchar *name; AiToolPermission perm; } ToolDefault;
static const ToolDefault g_defaults[] = {
    { "view_file",           TOOL_PERM_AUTO    },
    { "edit_file",           TOOL_PERM_AUTO    },
    { "replace_range",       TOOL_PERM_AUTO    },
    { "create_file",         TOOL_PERM_AUTO    },
    { "delete_file",         TOOL_PERM_CONFIRM },
    { "get_active_file",     TOOL_PERM_AUTO    },
    { "open_file_in_editor", TOOL_PERM_AUTO    },
    { "list_dir",            TOOL_PERM_AUTO    },
    { "find_files",          TOOL_PERM_AUTO    },
    { "get_project_structure", TOOL_PERM_AUTO  },
    { "search_in_files",     TOOL_PERM_AUTO    },
    { "get_selection",       TOOL_PERM_AUTO    },
    { "insert_at_cursor",    TOOL_PERM_AUTO    },
    { "run_command",         TOOL_PERM_CONFIRM },
    { "apply_diff",          TOOL_PERM_AUTO    },
    { "get_diagnostics",     TOOL_PERM_AUTO    },
    { "web_fetch",           TOOL_PERM_AUTO    },
    { "git_status",          TOOL_PERM_AUTO    },
    { "git_diff",            TOOL_PERM_AUTO    },
    { "git_stage",           TOOL_PERM_AUTO    },
    { "git_unstage",         TOOL_PERM_AUTO    },
    { "git_commit",          TOOL_PERM_CONFIRM },
    { "git_push",            TOOL_PERM_CONFIRM },
    { "git_pull",            TOOL_PERM_CONFIRM },
    { NULL, 0 }
};

void
ai_tools_init (AetherIdeWindow *window)
{
    g_tool_ctx.window = window;
    const gchar *ws = aether_ide_window_get_workspace_dir (window);
    g_tool_ctx.workspace_path = ws;

    if (!g_permissions) {
        g_permissions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, NULL);
        for (gint i = 0; g_defaults[i].name; i++) {
            g_hash_table_insert (g_permissions,
                                 g_strdup (g_defaults[i].name),
                                 GINT_TO_POINTER (g_defaults[i].perm));
        }
    }
}

AiToolPermission
ai_tools_get_permission (const gchar *tool_name)
{
    if (!g_permissions) return TOOL_PERM_AUTO;
    gpointer p = g_hash_table_lookup (g_permissions, tool_name);
    return (AiToolPermission) GPOINTER_TO_INT (p);
}

void
ai_tools_set_permission (const gchar *tool_name, AiToolPermission perm)
{
    if (!g_permissions) return;
    g_hash_table_insert (g_permissions, g_strdup (tool_name),
                         GINT_TO_POINTER (perm));
}

/* ================================================================== */
/* Schema builder helpers                                               */
/* ================================================================== */

static JsonObject *
make_string_prop (const gchar *desc)
{
    JsonObject *p = json_object_new ();
    json_object_set_string_member (p, "type", "string");
    json_object_set_string_member (p, "description", desc);
    return p;
}

static JsonObject *
make_int_prop (const gchar *desc)
{
    JsonObject *p = json_object_new ();
    json_object_set_string_member (p, "type", "integer");
    json_object_set_string_member (p, "description", desc);
    return p;
}

static JsonObject *
make_bool_prop (const gchar *desc)
{
    JsonObject *p = json_object_new ();
    json_object_set_string_member (p, "type", "boolean");
    json_object_set_string_member (p, "description", desc);
    return p;
}

static void
add_tool (JsonArray *arr, const gchar *name, const gchar *desc,
          JsonObject *props, const gchar **required_fields)
{
    JsonObject *parameters = json_object_new ();
    json_object_set_string_member (parameters, "type", "object");
    json_object_set_object_member (parameters, "properties", props);

    JsonArray *req = json_array_new ();
    if (required_fields) {
        for (gint i = 0; required_fields[i]; i++)
            json_array_add_string_element (req, required_fields[i]);
    }
    json_object_set_array_member (parameters, "required", req);

    JsonObject *fn = json_object_new ();
    json_object_set_string_member (fn, "name", name);
    json_object_set_string_member (fn, "description", desc);
    json_object_set_object_member (fn, "parameters", parameters);

    JsonObject *tool = json_object_new ();
    json_object_set_string_member (tool, "type", "function");
    json_object_set_object_member (tool, "function", fn);

    json_array_add_object_element (arr, tool);
}

JsonNode *
ai_tools_get_definitions (void)
{
    JsonArray *arr = json_array_new ();

    /* ---- view_file ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Absolute or workspace-relative path to the file."));
        json_object_set_object_member (p, "start_line",
            make_int_prop ("Optional: first line to read (1-based). 0 means beginning."));
        json_object_set_object_member (p, "end_line",
            make_int_prop ("Optional: last line to read (1-based). 0 means end of file."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "view_file",
            "Read the contents of a file. Can optionally read a specific line range.",
            p, req);
    }

    /* ---- edit_file ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path", make_string_prop ("Path to the file."));
        json_object_set_object_member (p, "content",
            make_string_prop ("The complete new content to write to the file."));
        const gchar *req[] = { "path", "content", NULL };
        add_tool (arr, "edit_file",
            "Overwrite a file with entirely new content. Prefer replace_range for targeted edits.",
            p, req);
    }

    /* ---- replace_range ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",    make_string_prop ("Path to the file."));
        json_object_set_object_member (p, "start_line", make_int_prop ("First line to replace (1-based, inclusive)."));
        json_object_set_object_member (p, "end_line",   make_int_prop ("Last line to replace (1-based, inclusive)."));
        json_object_set_object_member (p, "new_content",make_string_prop ("Replacement text (may span multiple lines)."));
        const gchar *req[] = { "path", "start_line", "end_line", "new_content", NULL };
        add_tool (arr, "replace_range",
            "Replace a specific range of lines in a file. More surgical than edit_file.",
            p, req);
    }

    /* ---- create_file ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",    make_string_prop ("Path for the new file. Parent dirs are created automatically."));
        json_object_set_object_member (p, "content", make_string_prop ("Initial file content."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "create_file",
            "Create a new file (and any missing parent directories).",
            p, req);
    }

    /* ---- delete_file ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path", make_string_prop ("Path to the file or empty directory to delete."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "delete_file",
            "Delete a file. Requires user confirmation.",
            p, req);
    }

    /* ---- get_active_file ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "include_content",
            make_bool_prop ("If true, include the file content in the response."));
        const gchar *req[] = { NULL };
        add_tool (arr, "get_active_file",
            "Get information about the file currently open in the editor.",
            p, req);
    }

    /* ---- open_file_in_editor ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Path to the file to open in a new editor tab."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "open_file_in_editor",
            "Open a file in a new editor tab.",
            p, req);
    }

    /* ---- list_dir ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Directory path. Use '.' for the workspace root."));
        json_object_set_object_member (p, "recursive",
            make_bool_prop ("If true, list recursively. Max depth: 5."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "list_dir",
            "List the contents of a directory with file sizes and types.",
            p, req);
    }

    /* ---- find_files ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "pattern",
            make_string_prop ("Glob pattern, e.g. '*.c', '**/test_*.py'."));
        const gchar *req[] = { "pattern", NULL };
        add_tool (arr, "find_files",
            "Find files matching a glob pattern in the workspace.",
            p, req);
    }

    /* ---- get_project_structure ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "max_depth",
            make_int_prop ("Maximum directory depth to show. Default: 4."));
        const gchar *req[] = { NULL };
        add_tool (arr, "get_project_structure",
            "Get a compact tree view of the workspace file structure.",
            p, req);
    }

    /* ---- search_in_files ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "query",
            make_string_prop ("Search string or regex pattern."));
        json_object_set_object_member (p, "is_regex",
            make_bool_prop ("Whether the query is a regex. Default: false."));
        json_object_set_object_member (p, "file_glob",
            make_string_prop ("Optional: limit search to files matching this glob (e.g. '*.c')."));
        json_object_set_object_member (p, "case_sensitive",
            make_bool_prop ("Whether the search is case-sensitive. Default: false."));
        const gchar *req[] = { "query", NULL };
        add_tool (arr, "search_in_files",
            "Search for a string or regex pattern across all files in the workspace.",
            p, req);
    }

    /* ---- get_selection ---- */
    {
        JsonObject *p = json_object_new ();
        const gchar *req[] = { NULL };
        add_tool (arr, "get_selection",
            "Get the text currently selected in the active editor.",
            p, req);
    }

    /* ---- insert_at_cursor ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "text",
            make_string_prop ("Text to insert at the current cursor position."));
        const gchar *req[] = { "text", NULL };
        add_tool (arr, "insert_at_cursor",
            "Insert text at the current cursor position in the active editor.",
            p, req);
    }

    /* ---- run_command ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "command",
            make_string_prop ("Shell command to execute in the workspace directory."));
        json_object_set_object_member (p, "timeout_seconds",
            make_int_prop ("Max execution time in seconds. Default: 30."));
        const gchar *req[] = { "command", NULL };
        add_tool (arr, "run_command",
            "Execute a shell command in the workspace. Requires user confirmation.",
            p, req);
    }

    /* ---- apply_diff ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Path to the file to patch."));
        json_object_set_object_member (p, "diff",
            make_string_prop ("Unified diff (patch) to apply to the file."));
        const gchar *req[] = { "path", "diff", NULL };
        add_tool (arr, "apply_diff",
            "Apply a unified diff (patch) to a file. Preferred for surgical code modifications.",
            p, req);
    }

    /* ---- get_diagnostics ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Optional: path to get diagnostics for. Defaults to active file."));
        const gchar *req[] = { NULL };
        add_tool (arr, "get_diagnostics",
            "Get compiler/linter errors and warnings for a file.",
            p, req);
    }

    /* ---- web_fetch ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "url",
            make_string_prop ("URL to fetch (HTTP/HTTPS)."));
        json_object_set_object_member (p, "extract_text",
            make_bool_prop ("If true, strip HTML tags and return plain text."));
        const gchar *req[] = { "url", NULL };
        add_tool (arr, "web_fetch",
            "Fetch the content of a URL (for reading documentation or APIs).",
            p, req);
    }

    /* ---- git_status ---- */
    {
        JsonObject *p = json_object_new ();
        add_tool (arr, "git_status",
            "Get the current git status (staged and unstaged files).",
            p, NULL);
    }

    /* ---- git_diff ---- */
    {
        JsonObject *p = json_object_new ();
        add_tool (arr, "git_diff",
            "Get the git diff for unstaged changes.",
            p, NULL);
    }

    /* ---- git_stage ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Path to stage. Use '.' for all."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "git_stage",
            "Stage a file for commit.",
            p, req);
    }

    /* ---- git_unstage ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "path",
            make_string_prop ("Path to unstage."));
        const gchar *req[] = { "path", NULL };
        add_tool (arr, "git_unstage",
            "Unstage a file.",
            p, req);
    }

    /* ---- git_commit ---- */
    {
        JsonObject *p = json_object_new ();
        json_object_set_object_member (p, "message",
            make_string_prop ("Commit message."));
        const gchar *req[] = { "message", NULL };
        add_tool (arr, "git_commit",
            "Commit staged changes.",
            p, req);
    }

    /* ---- git_push ---- */
    {
        JsonObject *p = json_object_new ();
        add_tool (arr, "git_push",
            "Push commits to remote.",
            p, NULL);
    }

    /* ---- git_pull ---- */
    {
        JsonObject *p = json_object_new ();
        add_tool (arr, "git_pull",
            "Pull commits from remote.",
            p, NULL);
    }

    JsonNode *node = json_node_new (JSON_NODE_ARRAY);
    json_node_set_array (node, arr);
    return node;
}

/* ================================================================== */
/* Path sandboxing                                                      */
/* ================================================================== */

/* Resolve path relative to workspace, ensure it doesn't escape */
static gchar *
resolve_safe_path (const gchar *input_path)
{
    if (!input_path || !*input_path) return NULL;

    gchar *resolved;
    if (g_path_is_absolute (input_path)) {
        resolved = g_strdup (input_path);
    } else {
        /* Relative: resolve against workspace */
        const gchar *ws = g_tool_ctx.workspace_path;
        if (!ws) ws = g_get_home_dir ();
        resolved = g_build_filename (ws, input_path, NULL);
    }

    /* Canonicalize to remove ".." etc */
    GFile *gf = g_file_new_for_path (resolved);
    gchar *canon = g_file_get_path (gf);
    g_object_unref (gf);
    g_free (resolved);

    /* Check workspace confinement */
    const gchar *ws = g_tool_ctx.workspace_path;
    if (ws && !g_str_has_prefix (canon ? canon : "", ws)) {
        /* Path escapes workspace — block it */
        g_free (canon);
        return NULL;
    }

    return canon ? canon : resolved;
}

/* ================================================================== */
/* User confirmation dialog                                             */
/* ================================================================== */

static gboolean
ask_user_confirmation (const gchar *title, const gchar *message)
{
    GtkWindow *parent = g_tool_ctx.window ?
                        GTK_WINDOW (g_tool_ctx.window) : NULL;

    GtkWidget *dialog = gtk_message_dialog_new (
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "%s", message);
    gtk_window_set_title (GTK_WINDOW (dialog), title);

    gint response = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return (response == GTK_RESPONSE_YES);
}

/* ================================================================== */
/* Tool implementations                                                 */
/* ================================================================== */

static gchar *
tool_view_file (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : NULL;
    if (!path) return g_strdup ("Error: 'path' is required");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    gchar *content = NULL;
    GError *err = NULL;
    if (!g_file_get_contents (safe, &content, NULL, &err)) {
        gchar *msg = g_strdup_printf ("Error reading file: %s", err->message);
        g_error_free (err);
        g_free (safe);
        return msg;
    }
    g_free (safe);

    /* Handle optional line range */
    gint start_line = 0, end_line = 0;
    if (json_object_has_member (args, "start_line"))
        start_line = (gint) json_object_get_int_member (args, "start_line");
    if (json_object_has_member (args, "end_line"))
        end_line = (gint) json_object_get_int_member (args, "end_line");

    if (start_line <= 0 && end_line <= 0) {
        return content; /* Return full file */
    }

    /* Extract lines */
    gchar **lines = g_strsplit (content, "\n", -1);
    g_free (content);
    gint total = (gint) g_strv_length (lines);
    gint s = MAX (0, start_line - 1);
    gint e = (end_line > 0) ? MIN (end_line, total) : total;

    GString *result = g_string_new ("");
    for (gint i = s; i < e; i++) {
        g_string_append_printf (result, "%4d: %s\n", i + 1, lines[i]);
    }
    g_strfreev (lines);
    return g_string_free (result, FALSE);
}

static gchar *
tool_edit_file (JsonObject *args)
{
    const gchar *path    = json_object_has_member (args, "path")    ? json_object_get_string_member (args, "path")    : NULL;
    const gchar *content = json_object_has_member (args, "content") ? json_object_get_string_member (args, "content") : "";

    if (!path) return g_strdup ("Error: 'path' is required");
    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    GError *err = NULL;
    if (!g_file_set_contents (safe, content, -1, &err)) {
        gchar *msg = g_strdup_printf ("Error writing file: %s", err->message);
        g_error_free (err); g_free (safe);
        return msg;
    }
    gchar *res = g_strdup_printf ("Successfully wrote %s", safe);
    g_free (safe);
    return res;
}

static gchar *
tool_replace_range (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : NULL;
    if (!path) return g_strdup ("Error: 'path' is required");

    gint start_line = json_object_has_member (args, "start_line") ?
                      (gint) json_object_get_int_member (args, "start_line") : -1;
    gint end_line   = json_object_has_member (args, "end_line") ?
                      (gint) json_object_get_int_member (args, "end_line") : -1;
    const gchar *new_content = json_object_has_member (args, "new_content") ?
                               json_object_get_string_member (args, "new_content") : "";

    if (start_line < 1 || end_line < 1)
        return g_strdup ("Error: 'start_line' and 'end_line' must be >= 1");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    gchar *old_content = NULL;
    GError *err = NULL;
    if (!g_file_get_contents (safe, &old_content, NULL, &err)) {
        gchar *msg = g_strdup_printf ("Error reading file: %s", err->message);
        g_error_free (err); g_free (safe);
        return msg;
    }

    gchar **lines = g_strsplit (old_content, "\n", -1);
    g_free (old_content);
    gint total = (gint) g_strv_length (lines);

    if (start_line > total || end_line > total) {
        g_strfreev (lines); g_free (safe);
        return g_strdup_printf ("Error: line range %d-%d out of bounds (file has %d lines)",
                                start_line, end_line, total);
    }

    GString *result = g_string_new ("");
    /* Lines before the range */
    for (gint i = 0; i < start_line - 1; i++)
        g_string_append_printf (result, "%s\n", lines[i]);
    /* Replacement */
    g_string_append (result, new_content);
    if (*new_content && new_content[strlen(new_content)-1] != '\n')
        g_string_append_c (result, '\n');
    /* Lines after the range */
    for (gint i = end_line; i < total; i++)
        g_string_append_printf (result, "%s\n", lines[i]);
    g_strfreev (lines);

    if (!g_file_set_contents (safe, result->str, -1, &err)) {
        gchar *msg = g_strdup_printf ("Error writing file: %s", err->message);
        g_error_free (err);
        g_string_free (result, TRUE);
        g_free (safe);
        return msg;
    }

    gchar *res = g_strdup_printf ("Replaced lines %d–%d in %s",
                                  start_line, end_line, safe);
    g_string_free (result, TRUE);
    g_free (safe);
    return res;
}

static gchar *
tool_create_file (JsonObject *args)
{
    const gchar *path    = json_object_has_member (args, "path")    ? json_object_get_string_member (args, "path")    : NULL;
    const gchar *content = json_object_has_member (args, "content") ? json_object_get_string_member (args, "content") : "";

    if (!path) return g_strdup ("Error: 'path' is required");
    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    /* Create parent dirs */
    gchar *dir = g_path_get_dirname (safe);
    g_mkdir_with_parents (dir, 0755);
    g_free (dir);

    GError *err = NULL;
    if (!g_file_set_contents (safe, content, -1, &err)) {
        gchar *msg = g_strdup_printf ("Error creating file: %s", err->message);
        g_error_free (err); g_free (safe);
        return msg;
    }
    gchar *res = g_strdup_printf ("Created %s", safe);
    g_free (safe);
    return res;
}

static gchar *
tool_delete_file (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : NULL;
    if (!path) return g_strdup ("Error: 'path' is required");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    gchar *msg = g_strdup_printf ("Delete file:\n%s\n\nAre you sure?", safe);
    gboolean confirmed = ask_user_confirmation ("Confirm Delete", msg);
    g_free (msg);

    if (!confirmed) { g_free (safe); return g_strdup ("Cancelled by user."); }

    GFile  *gf  = g_file_new_for_path (safe);
    GError *err = NULL;
    if (!g_file_delete (gf, NULL, &err)) {
        gchar *res = g_strdup_printf ("Error deleting: %s", err->message);
        g_error_free (err); g_object_unref (gf); g_free (safe);
        return res;
    }
    gchar *res = g_strdup_printf ("Deleted %s", safe);
    g_object_unref (gf); g_free (safe);
    return res;
}

static gchar *
tool_get_active_file (JsonObject *args)
{
    gboolean include_content = TRUE;
    if (json_object_has_member (args, "include_content"))
        include_content = json_object_get_boolean_member (args, "include_content");

    if (!g_tool_ctx.window) return g_strdup ("Error: no IDE window");

    AiContext *ctx = ai_context_capture (g_tool_ctx.window);
    if (!ctx->active_file_path) {
        ai_context_free (ctx);
        return g_strdup ("No file currently open in the editor.");
    }

    GString *result = g_string_new ("");
    g_string_append_printf (result, "Path: %s\n", ctx->active_file_path);
    if (ctx->language_id)
        g_string_append_printf (result, "Language: %s\n", ctx->language_id);
    g_string_append_printf (result, "Cursor: Line %d, Col %d\n",
                            ctx->cursor_line, ctx->cursor_col);

    if (include_content && ctx->active_file_content) {
        g_string_append (result, "\nContent:\n```\n");
        g_string_append (result, ctx->active_file_content);
        g_string_append (result, "\n```");
    }

    ai_context_free (ctx);
    return g_string_free (result, FALSE);
}

static gchar *
tool_open_file_in_editor (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : NULL;
    if (!path) return g_strdup ("Error: 'path' is required");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    gchar *content = NULL;
    GError *err = NULL;
    if (!g_file_get_contents (safe, &content, NULL, &err)) {
        gchar *msg = g_strdup_printf ("Error reading file: %s", err->message);
        g_error_free (err); g_free (safe);
        return msg;
    }

    gchar *basename = g_path_get_basename (safe);
    create_editor_tab (g_tool_ctx.window, basename, safe, content);
    g_free (basename);
    g_free (content);

    gchar *res = g_strdup_printf ("Opened %s in editor.", safe);
    g_free (safe);
    return res;
}

static void
list_dir_recursive (GString *out, const gchar *path, gint depth, gint max_depth,
                    const gchar *prefix)
{
    if (depth > max_depth) return;

    GDir   *dir = g_dir_open (path, 0, NULL);
    if (!dir) return;

    const gchar *name;
    /* Collect entries */
    GPtrArray *entries = g_ptr_array_new_with_free_func (g_free);
    while ((name = g_dir_read_name (dir)) != NULL)
        g_ptr_array_add (entries, g_strdup (name));
    g_dir_close (dir);
    g_ptr_array_sort (entries, (GCompareFunc) g_strcmp0);

    for (guint i = 0; i < entries->len; i++) {
        const gchar *entry = (const gchar *)g_ptr_array_index (entries, i);
        gchar *full = g_build_filename (path, entry, NULL);
        gboolean is_last = (i == entries->len - 1);
        const gchar *branch = is_last ? "└── " : "├── ";
        const gchar *child_prefix = is_last ? "    " : "│   ";

        if (g_file_test (full, G_FILE_TEST_IS_DIR)) {
            g_string_append_printf (out, "%s%s%s/\n", prefix, branch, entry);
            if (depth < max_depth) {
                gchar *new_prefix = g_strconcat (prefix, child_prefix, NULL);
                list_dir_recursive (out, full, depth + 1, max_depth, new_prefix);
                g_free (new_prefix);
            }
        } else {
            gchar *fpath = g_build_filename (path, entry, NULL);
            struct stat st;
            gsize sz = 0;
            if (stat (fpath, &st) == 0) sz = (gsize) st.st_size;
            g_free (fpath);
            if (sz < 1024)
                g_string_append_printf (out, "%s%s%s  (%zuB)\n", prefix, branch, entry, sz);
            else if (sz < 1024*1024)
                g_string_append_printf (out, "%s%s%s  (%.1fKB)\n", prefix, branch, entry, sz/1024.0);
            else
                g_string_append_printf (out, "%s%s%s  (%.1fMB)\n", prefix, branch, entry, sz/1048576.0);
        }
        g_free (full);
    }
    g_ptr_array_free (entries, TRUE);
}

static gchar *
tool_list_dir (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : ".";
    gboolean recursive = json_object_has_member (args, "recursive") &&
                         json_object_get_boolean_member (args, "recursive");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    if (!g_file_test (safe, G_FILE_TEST_IS_DIR)) {
        g_free (safe);
        return g_strdup ("Error: not a directory");
    }

    GString *out = g_string_new ("");
    g_string_append_printf (out, "%s/\n", safe);
    list_dir_recursive (out, safe, 0, recursive ? 5 : 1, "");
    g_free (safe);
    return g_string_free (out, FALSE);
}

static gchar *
tool_get_project_structure (JsonObject *args)
{
    gint max_depth = 4;
    if (json_object_has_member (args, "max_depth"))
        max_depth = (gint) json_object_get_int_member (args, "max_depth");
    max_depth = CLAMP (max_depth, 1, 8);

    const gchar *ws = g_tool_ctx.workspace_path;
    if (!ws) return g_strdup ("No workspace open.");

    GString *out = g_string_new ("");
    g_string_append_printf (out, "%s/\n", ws);
    list_dir_recursive (out, ws, 0, max_depth, "");
    return g_string_free (out, FALSE);
}

static gchar *
tool_find_files (JsonObject *args)
{
    const gchar *pattern = json_object_has_member (args, "pattern") ?
                           json_object_get_string_member (args, "pattern") : "*";
    const gchar *ws = g_tool_ctx.workspace_path;
    if (!ws) return g_strdup ("No workspace open.");

    /* Use 'find' command for robustness */
    gchar *cmd = g_strdup_printf (
        "find %s -maxdepth 10 -name '%s' 2>/dev/null | head -100", ws, pattern);
    gchar *output = NULL;
    GError *err = NULL;
    g_spawn_command_line_sync (cmd, &output, NULL, NULL, &err);
    g_free (cmd);

    if (err) {
        gchar *msg = g_strdup_printf ("Error: %s", err->message);
        g_error_free (err);
        return msg;
    }
    return output ? output : g_strdup ("No files found.");
}

static gchar *
tool_search_in_files (JsonObject *args)
{
    const gchar *query = json_object_has_member (args, "query") ?
                         json_object_get_string_member (args, "query") : NULL;
    if (!query) return g_strdup ("Error: 'query' is required");

    gboolean is_regex      = json_object_has_member (args, "is_regex") &&
                             json_object_get_boolean_member (args, "is_regex");
    gboolean case_sensitive = json_object_has_member (args, "case_sensitive") &&
                              json_object_get_boolean_member (args, "case_sensitive");
    const gchar *file_glob = json_object_has_member (args, "file_glob") ?
                             json_object_get_string_member (args, "file_glob") : NULL;

    const gchar *ws = g_tool_ctx.workspace_path;
    if (!ws) return g_strdup ("No workspace open.");

    /* Build grep command (use ripgrep if available, else grep) */
    GString *cmd = g_string_new ("");
    if (g_find_program_in_path ("rg")) {
        g_string_append (cmd, "rg --line-number --with-filename --color=never");
        if (!case_sensitive) g_string_append (cmd, " --ignore-case");
        if (!is_regex)       g_string_append (cmd, " --fixed-strings");
        if (file_glob)       g_string_append_printf (cmd, " --glob '%s'", file_glob);
        g_string_append_printf (cmd, " -- %s %s 2>/dev/null | head -200",
                                g_shell_quote (query), ws);
    } else {
        g_string_append (cmd, "grep -rn --include='*'");
        if (!case_sensitive) g_string_append (cmd, " -i");
        if (!is_regex)       g_string_append (cmd, " -F");
        if (file_glob)       g_string_append_printf (cmd, " --include='%s'", file_glob);
        g_string_append_printf (cmd, " -- %s %s 2>/dev/null | head -200",
                                g_shell_quote (query), ws);
    }

    gchar *output = NULL, *err_out = NULL;
    GError *err = NULL;
    g_spawn_command_line_sync (cmd->str, &output, &err_out, NULL, &err);
    g_string_free (cmd, TRUE);

    if (err) {
        gchar *msg = g_strdup_printf ("Search error: %s", err->message);
        g_error_free (err); g_free (output); g_free (err_out);
        return msg;
    }
    if (!output || !*output) {
        g_free (output); g_free (err_out);
        return g_strdup ("No matches found.");
    }
    g_free (err_out);
    return output;
}

static gchar *
tool_get_selection (JsonObject *args)
{
    (void) args;
    if (!g_tool_ctx.window) return g_strdup ("No IDE window available.");

    AiContext *ctx = ai_context_capture (g_tool_ctx.window);
    gchar *result;
    if (ctx->selected_text && *ctx->selected_text)
        result = g_strdup (ctx->selected_text);
    else
        result = g_strdup ("(no text selected)");
    ai_context_free (ctx);
    return result;
}

static gchar *
tool_insert_at_cursor (JsonObject *args)
{
    const gchar *text = json_object_has_member (args, "text") ?
                        json_object_get_string_member (args, "text") : NULL;
    if (!text) return g_strdup ("Error: 'text' is required");
    if (!g_tool_ctx.window) return g_strdup ("No IDE window available.");

    GtkWidget *sv = ai_context_get_active_source_view (g_tool_ctx.window);
    if (!sv) return g_strdup ("No active editor.");

    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (sv));
    gtk_text_buffer_begin_user_action (buf);
    gtk_text_buffer_insert_at_cursor (buf, text, -1);
    gtk_text_buffer_end_user_action (buf);

    return g_strdup ("Text inserted at cursor.");
}

static gchar *
tool_run_command (JsonObject *args)
{
    const gchar *cmd = json_object_has_member (args, "command") ?
                       json_object_get_string_member (args, "command") : NULL;
    if (!cmd) return g_strdup ("Error: 'command' is required");

    gint timeout_secs = 30;
    if (json_object_has_member (args, "timeout_seconds"))
        timeout_secs = (gint) json_object_get_int_member (args, "timeout_seconds");
    timeout_secs = CLAMP (timeout_secs, 1, 120);

    gchar *confirm_msg = g_strdup_printf (
        "The AI wants to run the following command:\n\n%s\n\nAllow?", cmd);
    gboolean confirmed = ask_user_confirmation ("Run Command?", confirm_msg);
    g_free (confirm_msg);

    if (!confirmed) return g_strdup ("Command cancelled by user.");

    const gchar *ws = g_tool_ctx.workspace_path;
    gchar *full_cmd = ws ?
        g_strdup_printf ("cd %s && timeout %d %s 2>&1",
                         g_shell_quote (ws), timeout_secs, cmd) :
        g_strdup_printf ("timeout %d %s 2>&1", timeout_secs, cmd);

    gchar *output = NULL;
    gint exit_status = 0;
    GError *err = NULL;
    g_spawn_command_line_sync (full_cmd, &output, NULL, &exit_status, &err);
    g_free (full_cmd);

    if (err) {
        gchar *msg = g_strdup_printf ("Failed to run command: %s", err->message);
        g_error_free (err);
        return msg;
    }

    GString *result = g_string_new ("");
    g_string_append_printf (result, "Exit code: %d\n", exit_status);
    if (output && *output) {
        g_string_append (result, "Output:\n");
        g_string_append (result, output);
    } else {
        g_string_append (result, "(no output)");
    }
    g_free (output);
    return g_string_free (result, FALSE);
}

static gchar *
tool_apply_diff (JsonObject *args)
{
    const gchar *path = json_object_has_member (args, "path") ?
                        json_object_get_string_member (args, "path") : NULL;
    const gchar *diff = json_object_has_member (args, "diff") ?
                        json_object_get_string_member (args, "diff") : NULL;
    if (!path) return g_strdup ("Error: 'path' is required");
    if (!diff)  return g_strdup ("Error: 'diff' is required");

    gchar *safe = resolve_safe_path (path);
    if (!safe) return g_strdup ("Error: path is outside the workspace");

    /* Write diff to temp file and apply with 'patch' */
    gchar *tmpfile = NULL;
    gint fd = g_file_open_tmp ("aether-XXXXXX.patch", &tmpfile, NULL);
    if (fd < 0) { g_free (safe); return g_strdup ("Error: could not create temp file"); }

    GIOChannel *ch = g_io_channel_unix_new (fd);
    g_io_channel_write_chars (ch, diff, -1, NULL, NULL);
    g_io_channel_unref (ch);
    close (fd);

    gchar *cmd = g_strdup_printf ("patch -u %s %s 2>&1",
                                  g_shell_quote (safe), g_shell_quote (tmpfile));
    gchar *output = NULL;
    gint exit_status = 0;
    GError *err = NULL;
    g_spawn_command_line_sync (cmd, &output, NULL, &exit_status, &err);
    g_free (cmd);
    unlink (tmpfile);
    g_free (tmpfile);
    g_free (safe);

    if (err) {
        g_free (output);
        gchar *msg = g_strdup_printf ("patch failed: %s", err->message);
        g_error_free (err);
        return msg;
    }
    gchar *result = g_strdup_printf ("patch exit=%d\n%s", exit_status,
                                     output ? output : "");
    g_free (output);
    return result;
}

static gchar *
tool_get_diagnostics (JsonObject *args)
{
    (void) args;
    /* TODO: integrate with LSP client when fully implemented */
    return g_strdup (
        "Diagnostics integration is pending full LSP implementation.\n"
        "Use the Problems panel in the IDE to view current errors.");
}

static gchar *
tool_web_fetch (JsonObject *args)
{
    const gchar *url = json_object_has_member (args, "url") ?
                       json_object_get_string_member (args, "url") : NULL;
    if (!url) return g_strdup ("Error: 'url' is required");

    /* Validate URL scheme */
    if (!g_str_has_prefix (url, "http://") && !g_str_has_prefix (url, "https://"))
        return g_strdup ("Error: only HTTP/HTTPS URLs are supported");

    gchar *cmd = g_strdup_printf (
        "curl -sL --max-time 15 --max-filesize 524288 "
        "-H 'User-Agent: AetherIDE/1.0' "
        "'%s' 2>&1 | head -c 32768", url);
    gchar *output = NULL;
    GError *err = NULL;
    g_spawn_command_line_sync (cmd, &output, NULL, NULL, &err);
    g_free (cmd);

    if (err) {
        gchar *msg = g_strdup_printf ("Fetch error: %s", err->message);
        g_error_free (err); g_free (output);
        return msg;
    }
    return output ? output : g_strdup ("(empty response)");
}

/* ================================================================== */
/* Git Tools                                                            */
/* ================================================================== */

#include "git_manager.h"

static gchar *
tool_git_status (JsonObject *args)
{
    (void) args;
    GPtrArray *st = git_manager_get_status();
    if (!st || st->len == 0) {
        if (st) g_ptr_array_free(st, TRUE);
        return g_strdup("No changes.");
    }
    GString *out = g_string_new("");
    for (guint i = 0; i < st->len; i++) {
        GitFileEntry *e = g_ptr_array_index(st, i);
        g_string_append_printf(out, "%s %s\n", (e->status & GIT_FILE_STATUS_STAGED) ? "S" : "U", e->path);
    }
    g_ptr_array_free(st, TRUE);
    return g_string_free(out, FALSE);
}

static gchar *
tool_git_diff (JsonObject *args)
{
    (void) args;
    /* Basic unstaged diff via command line for now as manager diff API is TODO */
    const gchar *ws = g_tool_ctx.workspace_path;
    if (!ws) return g_strdup("No workspace.");
    gchar *cmd = g_strdup_printf("git -C %s diff", g_shell_quote(ws));
    gchar *out = NULL;
    g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL);
    g_free(cmd);
    return out ? out : g_strdup("");
}

static gchar *
tool_git_stage (JsonObject *args)
{
    const gchar *path = json_object_has_member(args, "path") ? json_object_get_string_member(args, "path") : NULL;
    if (!path) return g_strdup("Error: path required.");
    if (g_strcmp0(path, ".") == 0) {
        if (git_manager_stage_all()) return g_strdup("Staged all changes.");
        return g_strdup("Failed to stage all.");
    }
    if (git_manager_stage_file(path)) return g_strdup_printf("Staged: %s", path);
    return g_strdup_printf("Failed to stage: %s", path);
}

static gchar *
tool_git_unstage (JsonObject *args)
{
    const gchar *path = json_object_has_member(args, "path") ? json_object_get_string_member(args, "path") : NULL;
    if (!path) return g_strdup("Error: path required.");
    if (git_manager_unstage_file(path)) return g_strdup_printf("Unstaged: %s", path);
    return g_strdup_printf("Failed to unstage: %s", path);
}

static gchar *
tool_git_commit (JsonObject *args)
{
    const gchar *msg = json_object_has_member(args, "message") ? json_object_get_string_member(args, "message") : NULL;
    if (!msg) return g_strdup("Error: message required.");
    if (git_manager_commit(msg)) return g_strdup("Committed successfully.");
    return g_strdup("Failed to commit.");
}

static gchar *
tool_git_push (JsonObject *args)
{
    (void) args;
    if (git_manager_push()) return g_strdup("Pushed successfully.");
    return g_strdup("Failed to push.");
}

static gchar *
tool_git_pull (JsonObject *args)
{
    (void) args;
    if (git_manager_pull()) return g_strdup("Pulled successfully.");
    return g_strdup("Failed to pull.");
}


/* ================================================================== */
/* Dispatcher                                                           */
/* ================================================================== */

gchar *
ai_tools_execute (const gchar *tool_name, const gchar *args_json)
{
    /* Check permission */
    AiToolPermission perm = ai_tools_get_permission (tool_name);
    if (perm == TOOL_PERM_DENY)
        return g_strdup_printf ("Tool '%s' is disabled.", tool_name);

    /* Parse args */
    GError *err = NULL;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, args_json, -1, &err)) {
        gchar *msg = g_strdup_printf ("Invalid tool arguments: %s", err->message);
        g_error_free (err); g_object_unref (parser);
        return msg;
    }
    JsonNode *root = json_parser_get_root (parser);
    JsonObject *args = JSON_NODE_HOLDS_OBJECT (root) ?
                       json_node_get_object (root) : json_object_new ();

    /* CONFIRM tools: show dialog (handled inside individual tools or here) */
    /* (delete_file and run_command handle confirmation internally) */

    gchar *result = NULL;
    if      (g_strcmp0 (tool_name, "view_file")           == 0) result = tool_view_file (args);
    else if (g_strcmp0 (tool_name, "edit_file")           == 0) result = tool_edit_file (args);
    else if (g_strcmp0 (tool_name, "replace_range")       == 0) result = tool_replace_range (args);
    else if (g_strcmp0 (tool_name, "create_file")         == 0) result = tool_create_file (args);
    else if (g_strcmp0 (tool_name, "delete_file")         == 0) result = tool_delete_file (args);
    else if (g_strcmp0 (tool_name, "get_active_file")     == 0) result = tool_get_active_file (args);
    else if (g_strcmp0 (tool_name, "open_file_in_editor") == 0) result = tool_open_file_in_editor (args);
    else if (g_strcmp0 (tool_name, "list_dir")            == 0) result = tool_list_dir (args);
    else if (g_strcmp0 (tool_name, "find_files")          == 0) result = tool_find_files (args);
    else if (g_strcmp0 (tool_name, "get_project_structure")== 0) result = tool_get_project_structure (args);
    else if (g_strcmp0 (tool_name, "search_in_files")     == 0) result = tool_search_in_files (args);
    else if (g_strcmp0 (tool_name, "get_selection")       == 0) result = tool_get_selection (args);
    else if (g_strcmp0 (tool_name, "insert_at_cursor")    == 0) result = tool_insert_at_cursor (args);
    else if (g_strcmp0 (tool_name, "run_command")         == 0) result = tool_run_command (args);
    else if (g_strcmp0 (tool_name, "apply_diff")          == 0) result = tool_apply_diff (args);
    else if (g_strcmp0 (tool_name, "get_diagnostics")     == 0) result = tool_get_diagnostics (args);
    else if (g_strcmp0 (tool_name, "web_fetch")           == 0) result = tool_web_fetch (args);
    else if (g_strcmp0 (tool_name, "git_status")          == 0) result = tool_git_status (args);
    else if (g_strcmp0 (tool_name, "git_diff")            == 0) result = tool_git_diff (args);
    else if (g_strcmp0 (tool_name, "git_stage")           == 0) result = tool_git_stage (args);
    else if (g_strcmp0 (tool_name, "git_unstage")         == 0) result = tool_git_unstage (args);
    else if (g_strcmp0 (tool_name, "git_commit")          == 0) result = tool_git_commit (args);
    else if (g_strcmp0 (tool_name, "git_push")            == 0) result = tool_git_push (args);
    else if (g_strcmp0 (tool_name, "git_pull")            == 0) result = tool_git_pull (args);
    else result = g_strdup_printf ("Unknown tool: '%s'", tool_name);

    g_object_unref (parser);
    return result;
}
