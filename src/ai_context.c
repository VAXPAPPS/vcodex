/*
 * ai_context.c — IDE context capture implementation
 */

#include "ai_context.h"
#include "vcodex_window_private.h"
#include <gtksourceview/gtksource.h>
#include <string.h>
#include <sys/stat.h>

/* Maximum content size to inject into the prompt (to avoid token overflow) */
#define MAX_CONTEXT_CONTENT_BYTES  (32 * 1024)

/* ------------------------------------------------------------------ */
/* Active source view helper                                            */
/* ------------------------------------------------------------------ */

GtkWidget *
ai_context_get_active_source_view (AetherIdeWindow *window)
{
    GtkWidget *notebook = aether_ide_window_get_notebook (window);
    if (!notebook) return NULL;

    gint page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
    if (page < 0) return NULL;

    GtkWidget *scroll = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), page);
    if (!scroll || !GTK_IS_BIN (scroll)) return NULL;

    GtkWidget *child = gtk_bin_get_child (GTK_BIN (scroll));
    if (!child || !GTK_IS_TEXT_VIEW (child)) return NULL;

    /* Verify it's actually a GtkSourceView */
    if (!GTK_SOURCE_IS_VIEW (child)) return NULL;

    return child;
}

/* ------------------------------------------------------------------ */
/* Context capture                                                      */
/* ------------------------------------------------------------------ */

AiContext *
ai_context_capture (AetherIdeWindow *window)
{
    AiContext *ctx = g_new0 (AiContext, 1);

    /* Workspace */
    const gchar *ws = aether_ide_window_get_workspace_dir (window);
    ctx->workspace_path = ws ? g_strdup (ws) : NULL;

    /* Active editor */
    GtkWidget *sv = ai_context_get_active_source_view (window);
    if (!sv) return ctx;

    /* File path */
    const gchar *filepath = g_object_get_data (G_OBJECT (sv), "filepath");
    ctx->active_file_path = filepath ? g_strdup (filepath) : NULL;

    /* Language */
    GtkSourceBuffer *buf = GTK_SOURCE_BUFFER (
        gtk_text_view_get_buffer (GTK_TEXT_VIEW (sv)));
    GtkSourceLanguage *lang = gtk_source_buffer_get_language (buf);
    if (lang)
        ctx->language_id = g_strdup (gtk_source_language_get_id (lang));

    /* Cursor position */
    GtkTextIter cursor_iter;
    GtkTextMark *insert_mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (buf));
    gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (buf), &cursor_iter, insert_mark);
    ctx->cursor_line = gtk_text_iter_get_line (&cursor_iter) + 1;      /* 1-based */
    ctx->cursor_col  = gtk_text_iter_get_line_offset (&cursor_iter) + 1;

    /* Selected text */
    GtkTextIter sel_start, sel_end;
    if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (buf),
                                              &sel_start, &sel_end)) {
        gchar *sel = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buf),
                                               &sel_start, &sel_end, FALSE);
        /* Limit selection to avoid prompt overflow */
        if (sel && strlen (sel) < 8192)
            ctx->selected_text = sel;
        else {
            g_free (sel);
        }
    }

    /* File content (capped for large files) */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buf), &start, &end);
    gchar *content = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (buf),
                                               &start, &end, FALSE);
    if (content) {
        gsize len = strlen (content);
        if (len <= MAX_CONTEXT_CONTENT_BYTES) {
            ctx->active_file_content = content;
        } else {
            /* Truncate symmetrically: first 16K + last 16K */
            gsize half = MAX_CONTEXT_CONTENT_BYTES / 2;
            gchar *truncated = g_strdup_printf (
                "%.*s\n\n[... %zu bytes truncated ...]\n\n%s",
                (gint) half, content,
                len - MAX_CONTEXT_CONTENT_BYTES,
                content + (len - half));
            g_free (content);
            ctx->active_file_content = truncated;
        }
    }

    return ctx;
}

/* ------------------------------------------------------------------ */
/* Free                                                                 */
/* ------------------------------------------------------------------ */

void
ai_context_free (AiContext *ctx)
{
    if (!ctx) return;
    g_free (ctx->workspace_path);
    g_free (ctx->active_file_path);
    g_free (ctx->active_file_content);
    g_free (ctx->language_id);
    g_free (ctx->selected_text);
    g_free (ctx);
}

/* ------------------------------------------------------------------ */
/* System prompt builder                                                */
/* ------------------------------------------------------------------ */

gchar *
ai_context_build_system_prompt (const AiContext *ctx)
{
    GString *prompt = g_string_new (
        "You are AetherAI, an expert software engineering assistant integrated "
        "into AetherIDE — a professional code editor running on Linux.\n\n"
        "## Your Capabilities\n"
        "You have access to tools that let you:\n"
        "- Read, create, edit, and delete files in the workspace\n"
        "- Search code with grep/ripgrep-style queries\n"
        "- List directory contents (with type/size info)\n"
        "- Get/apply surgical diffs instead of full file rewrites\n"
        "- Run shell commands in the terminal (with user confirmation)\n"
        "- Open files in the editor, insert code at the cursor\n"
        "- Get the currently active file and the user's selection\n\n"
        "## Guidelines\n"
        "- Prefer `replace_range` or `apply_diff` over `edit_file` for existing files\n"
        "- Ask for confirmation before running commands or deleting files\n"
        "- Keep responses concise and action-oriented\n"
        "- When writing code, match the style of the existing codebase\n"
        "- If a task is ambiguous, ask ONE clarifying question before proceeding\n\n"
        "## Current IDE Context\n"
    );

    if (ctx) {
        if (ctx->workspace_path)
            g_string_append_printf (prompt, "- **Workspace**: `%s`\n",
                                    ctx->workspace_path);
        else
            g_string_append (prompt, "- **Workspace**: (no folder opened)\n");

        if (ctx->active_file_path) {
            g_string_append_printf (prompt, "- **Active file**: `%s`\n",
                                    ctx->active_file_path);
            if (ctx->language_id)
                g_string_append_printf (prompt, "- **Language**: %s\n",
                                        ctx->language_id);
            if (ctx->cursor_line > 0)
                g_string_append_printf (prompt,
                                        "- **Cursor**: Line %d, Column %d\n",
                                        ctx->cursor_line, ctx->cursor_col);
        } else {
            g_string_append (prompt, "- **Active file**: (none)\n");
        }

        if (ctx->selected_text && *ctx->selected_text) {
            g_string_append (prompt, "\n**User's current selection:**\n```\n");
            g_string_append (prompt, ctx->selected_text);
            g_string_append (prompt, "\n```\n");
        }
    }

    g_string_append (prompt,
        "\nAlways use tools proactively. When you need to inspect code, "
        "use `view_file` or `get_active_file` rather than asking the user to paste it.");

    return g_string_free (prompt, FALSE);
}
