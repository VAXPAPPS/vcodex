/*
 * ai_panel.c — Professional AI Agent chat interface
 *
 * Features:
 *   • Real-time streaming: tokens appear one-by-one as they arrive
 *   • Markdown rendering: bold, italic, code blocks, headers, lists
 *   • GtkSourceView code blocks with syntax highlighting in AI responses
 *   • User/AI chat bubbles with distinct styling
 *   • Typing indicator (spinner + "AI is thinking…")
 *   • Cancel in-flight request button
 *   • "Send with context" — attaches active file info automatically
 *   • Code block action buttons: Copy | Insert at Cursor
 *   • Keyboard shortcut: Ctrl+Enter to send
 *   • Model/provider badge in the header
 *   • Conversation persistence: auto-save to ~/.local/share/vcodex/conversations/
 *   • Multi-conversation switcher with history popover
 */

#include "ai_panel.h"
#include "ai_agent.h"
#include "ai_settings.h"
#include "ai_provider.h"
#include "ai_context.h"
#include "ai_tools.h"
#include "ai_conversation.h"
#include "vcodex_window_private.h"
#include <gtksourceview/gtksource.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================== */
/* Panel data structure                                                 */
/* ================================================================== */

typedef struct {
    AetherIdeWindow *window;
    AiAgentSession  *session;

    /* Layout */
    GtkWidget *chat_list;       /* GtkListBox — chat bubbles */
    GtkWidget *scroll;          /* GtkScrolledWindow wrapping chat_list */
    GtkWidget *input_view;      /* GtkTextView for user input */
    GtkWidget *send_btn;
    GtkWidget *cancel_btn;
    GtkWidget *context_btn;     /* "📎 Send with context" */
    GtkWidget *spinner;
    GtkWidget *thinking_label;
    GtkWidget *model_badge;     /* Shows "Provider / Model" */
    GtkWidget *status_bar;      /* row with spinner + thinking_label */

    /* Current streaming row */
    GtkWidget     *streaming_row;   /* GtkListBoxRow being built */
    GtkTextBuffer *streaming_buf;   /* Buffer in the streaming row */
    GtkTextTag    *streaming_tag;   /* Cursor highlight tag */

    gboolean    is_streaming;
    gboolean    attach_context;     /* If TRUE, next send includes file context */

    /* ---- Conversation persistence ---- */
    gchar      *conv_id;            /* Active conversation UUID */
    gint        presend_history_len;/* History length recorded before last send */
    gboolean    conv_titled;        /* TRUE once auto-title has been set */
    gchar      *pending_user_text;  /* First user message (for auto-titling) */

    /* ---- Conversation switcher UI ---- */
    GtkWidget  *conv_btn;           /* History icon button in header */
    GtkWidget  *conv_popover;       /* GtkPopover containing conv list */
    GtkWidget  *conv_list_box;      /* GtkListBox inside the popover */
    GtkWidget  *conv_title_label;   /* Label showing current conv title */
} AiPanelData;

/* ================================================================== */
/* CSS for chat bubbles                                                 */
/* ================================================================== */

static const gchar *PANEL_CSS =
    ".ai-panel {"
    "  background: transparent;"
    "}"
    ".chat-header {"
    "  border-bottom: 1px solid rgba(255,255,255,0.08);"
    "  padding: 6px 10px;"
    "}"
    ".model-badge {"
    "  background: rgba(99,179,237,0.15);"
    "  color: #63b3ed;"
    "  border-radius: 4px;"
    "  padding: 2px 8px;"
    "  font-size: 11px;"
    "}"
    ".conv-title-label {"
    "  font-size: 11px;"
    "  color: rgba(255,255,255,0.55);"
    "  font-style: italic;"
    "  max-width: 120px;"
    "}"
    /* Conversation popover */
    ".conv-popover {"
    "  padding: 0;"
    "}"
    ".conv-popover-header {"
    "  padding: 8px 12px;"
    "  border-bottom: 1px solid rgba(255,255,255,0.08);"
    "  font-weight: bold;"
    "  font-size: 12px;"
    "}"
    ".conv-row {"
    "  padding: 8px 12px;"
    "  border-bottom: 1px solid rgba(255,255,255,0.04);"
    "  transition: background 120ms ease;"
    "}"
    ".conv-row:hover {"
    "  background: rgba(255,255,255,0.07);"
    "}"
    ".conv-row-active {"
    "  background: rgba(99,179,237,0.12);"
    "  border-left: 3px solid #63b3ed;"
    "}"
    ".conv-row-title {"
    "  font-size: 12px;"
    "  font-weight: 600;"
    "}"
    ".conv-row-meta {"
    "  font-size: 10px;"
    "  color: rgba(255,255,255,0.35);"
    "  margin-top: 2px;"
    "}"
    ".conv-delete-btn {"
    "  padding: 2px 6px;"
    "  border-radius: 4px;"
    "  opacity: 0.5;"
    "}"
    ".conv-delete-btn:hover {"
    "  background: rgba(252,129,74,0.2);"
    "  color: #fc814a;"
    "  opacity: 1.0;"
    "}"
    ".conv-new-btn {"
    "  border-radius: 6px;"
    "  margin: 8px;"
    "  padding: 6px 12px;"
    "  font-size: 11px;"
    "}"
    /* Existing styles */
    ".bubble-user {"
    "  background: rgba(99,179,237,0.18);"
    "  border-radius: 12px 12px 3px 12px;"
    "  margin: 4px 8px 4px 30px;"
    "  padding: 10px 14px;"
    "}"
    ".bubble-ai {"
    "  background: rgba(255,255,255,0.06);"
    "  border-radius: 12px 12px 12px 3px;"
    "  margin: 4px 30px 4px 8px;"
    "  padding: 10px 14px;"
    "  border-left: 2px solid rgba(99,179,237,0.4);"
    "}"
    ".bubble-role {"
    "  font-size: 10px;"
    "  color: rgba(255,255,255,0.4);"
    "  margin-bottom: 4px;"
    "  letter-spacing: 0.5px;"
    "  text-transform: uppercase;"
    "}"
    ".code-block-bar {"
    "  background: rgba(0,0,0,0.3);"
    "  border-radius: 6px 6px 0 0;"
    "  padding: 3px 8px;"
    "}"
    ".code-block-view {"
    "  border-radius: 0 0 6px 6px;"
    "  background: rgba(0,0,0,0.25);"
    "  font-family: monospace;"
    "  font-size: 12px;"
    "  padding: 8px;"
    "}"
    ".input-area {"
    "  border-top: 1px solid rgba(255,255,255,0.08);"
    "  padding: 10px;"
    "}"
    ".send-btn {"
    "  background: rgba(99,179,237,0.8);"
    "  color: white;"
    "  border-radius: 6px;"
    "  border: none;"
    "  padding: 6px 14px;"
    "}"
    ".send-btn:hover {"
    "  background: rgba(99,179,237,1.0);"
    "}"
    ".cancel-btn {"
    "  background: rgba(252,129,74,0.7);"
    "  color: white;"
    "  border-radius: 6px;"
    "  border: none;"
    "  padding: 6px 14px;"
    "}"
    ".context-btn {"
    "  border-radius: 6px;"
    "  padding: 4px 10px;"
    "  font-size: 11px;"
    "}"
    ".context-btn-active {"
    "  background: rgba(72,187,120,0.25);"
    "  color: #48bb78;"
    "}"
    ".thinking-row {"
    "  padding: 6px 12px;"
    "  color: rgba(255,255,255,0.4);"
    "  font-style: italic;"
    "  font-size: 12px;"
    "}";

static GtkCssProvider *g_css_provider = NULL;

static void
ensure_css (void)
{
    if (g_css_provider) return;
    g_css_provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (g_css_provider, PANEL_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen (
        gdk_screen_get_default (),
        GTK_STYLE_PROVIDER (g_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
}

/* ================================================================== */
/* Markdown → GtkTextBuffer renderer                                   */
/* ================================================================== */

/*
 * Lightweight Markdown renderer for GTK TextBuffer.
 * Handles: **bold**, *italic*, `inline code`, ``` code blocks,
 *          # headers, - / * bullet lists, > blockquotes.
 * Does NOT handle tables or nested lists (those get shown as-is).
 */

static void
ensure_text_tags (GtkTextBuffer *buf)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table (buf);
#define ENSURE_TAG(name, ...) \
    if (!gtk_text_tag_table_lookup (table, name)) \
        gtk_text_buffer_create_tag (buf, name, __VA_ARGS__, NULL)

    ENSURE_TAG ("bold",       "weight",     PANGO_WEIGHT_BOLD);
    ENSURE_TAG ("italic",     "style",      PANGO_STYLE_ITALIC);
    ENSURE_TAG ("code-inline","family",     "monospace",
                              "background", "#1a202c",
                              "foreground", "#e2e8f0");
    ENSURE_TAG ("h1",         "weight",     PANGO_WEIGHT_BOLD,
                              "scale",      PANGO_SCALE_LARGE,
                              "foreground", "#90cdf4");
    ENSURE_TAG ("h2",         "weight",     PANGO_WEIGHT_BOLD,
                              "foreground", "#90cdf4");
    ENSURE_TAG ("h3",         "weight",     PANGO_WEIGHT_SEMIBOLD,
                              "foreground", "#a0aec0");
    ENSURE_TAG ("quote",      "foreground", "#718096",
                              "style",      PANGO_STYLE_ITALIC,
                              "left-margin", 16);
    ENSURE_TAG ("bullet",     "left-margin", 16);
    ENSURE_TAG ("tool-notice","foreground", "#68d391",
                              "style",      PANGO_STYLE_ITALIC);
#undef ENSURE_TAG
}

/* Render a single line of markdown text (no block structure) into the buffer */
static void
render_inline (GtkTextBuffer *buf, GtkTextIter *iter, const gchar *text)
{
    const gchar *p = text;
    while (*p) {
        /* Bold: **text** */
        if (p[0] == '*' && p[1] == '*') {
            const gchar *end = strstr (p + 2, "**");
            if (end) {
                GtkTextIter start = *iter;
                gchar *inner = g_strndup (p + 2, end - (p + 2));
                gtk_text_buffer_insert (buf, iter, inner, -1);
                GtkTextIter s = start;
                gtk_text_buffer_apply_tag_by_name (buf, "bold", &s, iter);
                g_free (inner);
                p = end + 2;
                continue;
            }
        }
        /* Italic: *text* (single star) */
        if (p[0] == '*' && p[1] != '*') {
            const gchar *end = strchr (p + 1, '*');
            if (end && end != p + 1) {
                GtkTextIter start = *iter;
                gchar *inner = g_strndup (p + 1, end - (p + 1));
                gtk_text_buffer_insert (buf, iter, inner, -1);
                GtkTextIter s = start;
                gtk_text_buffer_apply_tag_by_name (buf, "italic", &s, iter);
                g_free (inner);
                p = end + 1;
                continue;
            }
        }
        /* Inline code: `text` */
        if (p[0] == '`' && p[1] != '`') {
            const gchar *end = strchr (p + 1, '`');
            if (end) {
                GtkTextIter start = *iter;
                gchar *inner = g_strndup (p + 1, end - (p + 1));
                gtk_text_buffer_insert (buf, iter, inner, -1);
                GtkTextIter s = start;
                gtk_text_buffer_apply_tag_by_name (buf, "code-inline", &s, iter);
                g_free (inner);
                p = end + 1;
                continue;
            }
        }
        /* Plain character */
        gchar ch[2] = { *p, '\0' };
        gtk_text_buffer_insert (buf, iter, ch, 1);
        p++;
    }
}

/* Detect if a line starts a code fence and return language (or NULL) */
static gboolean
is_code_fence (const gchar *line, gchar **lang_out)
{
    if (g_str_has_prefix (line, "```")) {
        const gchar *lang = line + 3;
        while (*lang == ' ') lang++;
        if (lang_out) *lang_out = (*lang && *lang != '\n') ? g_strdup (lang) : NULL;
        return TRUE;
    }
    return FALSE;
}

/* ================================================================== */
/* Code block widget builder                                            */
/* ================================================================== */

static void
on_copy_code_clicked (GtkButton *btn, gpointer user_data)
{
    const gchar *code = (const gchar *) user_data;
    GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text (clip, code, -1);
    /* Briefly change button label */
    gtk_button_set_label (btn, "✓ Copied");
    /* Reset after 1.5s */
}

static void
on_insert_code_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    const gchar *code = (const gchar *) user_data;
    /* Call the insert_at_cursor tool */
    gchar *args = g_strdup_printf ("{\"text\":\"%s\"}", code);
    gchar *result = ai_tools_execute ("insert_at_cursor", args);
    g_free (args);
    g_free (result);
}

/* Create a styled code block widget with language label + action buttons */
static GtkWidget *
build_code_block_widget (const gchar *lang, const gchar *code)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    /* ---- Top bar ---- */
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class (gtk_widget_get_style_context (bar), "code-block-bar");

    GtkWidget *lang_lbl = gtk_label_new (lang && *lang ? lang : "code");
    GtkStyleContext *lctx = gtk_widget_get_style_context (lang_lbl);
    gtk_style_context_add_class (lctx, "dim-label");
    gtk_widget_set_hexpand (lang_lbl, TRUE);
    gtk_widget_set_halign  (lang_lbl, GTK_ALIGN_START);

    /* Copy button */
    GtkWidget *copy_btn = gtk_button_new_with_label ("⎘ Copy");
    gtk_style_context_add_class (gtk_widget_get_style_context (copy_btn), "flat");
    gchar *code_copy = g_strdup (code);
    g_signal_connect_data (copy_btn, "clicked",
                           G_CALLBACK (on_copy_code_clicked),
                           code_copy, (GClosureNotify)g_free, 0);

    /* Insert button */
    GtkWidget *insert_btn = gtk_button_new_with_label ("↳ Insert");
    gtk_style_context_add_class (gtk_widget_get_style_context (insert_btn), "flat");
    gchar *code_ins = g_strdup (code);
    g_signal_connect_data (insert_btn, "clicked",
                           G_CALLBACK (on_insert_code_clicked),
                           code_ins, (GClosureNotify)g_free, 0);

    gtk_box_pack_start (GTK_BOX (bar), lang_lbl,   TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (bar), copy_btn,   FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bar), insert_btn, FALSE, FALSE, 0);

    /* ---- Code view ---- */
    GtkSourceBuffer *src_buf = gtk_source_buffer_new (NULL);

    if (lang && *lang) {
        GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
        GtkSourceLanguage *src_lang =
            gtk_source_language_manager_get_language (lm, lang);
        if (!src_lang) {
            /* Try guessing from extension */
            gchar *fake_filename = g_strdup_printf ("file.%s", lang);
            src_lang = gtk_source_language_manager_guess_language (
                lm, fake_filename, NULL);
            g_free (fake_filename);
        }
        if (src_lang)
            gtk_source_buffer_set_language (src_buf, src_lang);
    }

    gtk_text_buffer_set_text (GTK_TEXT_BUFFER (src_buf), code, -1);

    GtkWidget *sv = gtk_source_view_new_with_buffer (src_buf);
    gtk_source_view_set_show_line_numbers  (GTK_SOURCE_VIEW (sv), TRUE);
    gtk_source_view_set_highlight_current_line (GTK_SOURCE_VIEW (sv), FALSE);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (sv), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (sv), FALSE);
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (sv), GTK_WRAP_NONE);
    gtk_style_context_add_class (gtk_widget_get_style_context (sv), "code-block-view");

    /* Clamp height */
    gchar **lines = g_strsplit (code, "\n", -1);
    gint n_lines = (gint) g_strv_length (lines);
    g_strfreev (lines);
    gint height = CLAMP (n_lines * 18 + 20, 60, 400);
    gtk_widget_set_size_request (sv, -1, height);

    GtkWidget *sv_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sv_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_container_add (GTK_CONTAINER (sv_scroll), sv);

    gtk_box_pack_start (GTK_BOX (vbox), bar,       FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), sv_scroll,  TRUE, TRUE,  0);

    g_object_unref (src_buf);
    return vbox;
}

/* ================================================================== */
/* Full message renderer                                                */
/* ================================================================== */

/*
 * Render a complete markdown string into a GtkBox.
 * Produces a mix of GtkTextViews (for prose) and GtkSourceViews (for code).
 */
static GtkWidget *
render_markdown_to_box (const gchar *markdown)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

    gchar **lines = g_strsplit (markdown, "\n", -1);
    gint n = (gint) g_strv_length (lines);

    GString     *prose_buf = g_string_new ("");
    gboolean     in_code   = FALSE;
    GString     *code_buf  = NULL;
    gchar       *code_lang = NULL;

    for (gint i = 0; i <= n; i++) {
        const gchar *line = (i < n) ? lines[i] : NULL;

        if (!in_code) {
            gchar *lang = NULL;
            if (line && is_code_fence (line, &lang)) {
                /* Flush pending prose */
                if (prose_buf->len > 0) {
                    /* Build a GtkTextView for prose */
                    GtkTextBuffer *buf = gtk_text_buffer_new (NULL);
                    ensure_text_tags (buf);
                    GtkTextIter iter;
                    gtk_text_buffer_get_end_iter (buf, &iter);

                    /* Parse prose lines */
                    gchar **plines = g_strsplit (prose_buf->str, "\n", -1);
                    for (gint pi = 0; plines[pi]; pi++) {
                        const gchar *pl = plines[pi];
                        if (pi > 0) gtk_text_buffer_insert (buf, &iter, "\n", 1);
                        if (g_str_has_prefix (pl, "### ")) {
                            GtkTextIter s = iter;
                            render_inline (buf, &iter, pl + 4);
                            gtk_text_buffer_apply_tag_by_name (buf, "h3", &s, &iter);
                        } else if (g_str_has_prefix (pl, "## ")) {
                            GtkTextIter s = iter;
                            render_inline (buf, &iter, pl + 3);
                            gtk_text_buffer_apply_tag_by_name (buf, "h2", &s, &iter);
                        } else if (g_str_has_prefix (pl, "# ")) {
                            GtkTextIter s = iter;
                            render_inline (buf, &iter, pl + 2);
                            gtk_text_buffer_apply_tag_by_name (buf, "h1", &s, &iter);
                        } else if (g_str_has_prefix (pl, "> ")) {
                            GtkTextIter s = iter;
                            render_inline (buf, &iter, pl + 2);
                            gtk_text_buffer_apply_tag_by_name (buf, "quote", &s, &iter);
                        } else if (g_str_has_prefix (pl, "- ") ||
                                   g_str_has_prefix (pl, "* ")) {
                            GtkTextIter s = iter;
                            gtk_text_buffer_insert (buf, &iter, "• ", -1);
                            render_inline (buf, &iter, pl + 2);
                            gtk_text_buffer_apply_tag_by_name (buf, "bullet", &s, &iter);
                        } else if (g_str_has_prefix (pl, "⚙️")) {
                            GtkTextIter s = iter;
                            render_inline (buf, &iter, pl);
                            gtk_text_buffer_apply_tag_by_name (buf, "tool-notice", &s, &iter);
                        } else {
                            render_inline (buf, &iter, pl);
                        }
                    }
                    g_strfreev (plines);

                    GtkWidget *tv = gtk_text_view_new_with_buffer (buf);
                    gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
                    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (tv), FALSE);
                    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);
                    gtk_widget_set_hexpand (tv, TRUE);
                    gtk_box_pack_start (GTK_BOX (vbox), tv, FALSE, TRUE, 0);
                    g_object_unref (buf);
                    g_string_truncate (prose_buf, 0);
                }

                /* Start code block */
                in_code   = TRUE;
                code_buf  = g_string_new ("");
                code_lang = lang; /* may be NULL */
                continue;
            }

            if (line) {
                if (prose_buf->len > 0) g_string_append_c (prose_buf, '\n');
                g_string_append (prose_buf, line);
            }
        } else {
            /* Inside code block */
            if (!line || is_code_fence (line, NULL)) {
                /* End of code block */
                GtkWidget *cb = build_code_block_widget (
                    code_lang, code_buf->str);
                gtk_box_pack_start (GTK_BOX (vbox), cb, FALSE, TRUE, 0);
                g_string_free (code_buf, TRUE);
                g_free (code_lang);
                code_buf = NULL; code_lang = NULL;
                in_code = FALSE;
            } else {
                if (code_buf->len > 0) g_string_append_c (code_buf, '\n');
                g_string_append (code_buf, line);
            }
        }
    }

    /* Flush any remaining prose */
    if (prose_buf->len > 0) {
        GtkTextBuffer *buf = gtk_text_buffer_new (NULL);
        ensure_text_tags (buf);
        GtkTextIter iter;
        gtk_text_buffer_get_end_iter (buf, &iter);
        gtk_text_buffer_insert (buf, &iter, prose_buf->str, -1);

        GtkWidget *tv = gtk_text_view_new_with_buffer (buf);
        gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
        gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (tv), FALSE);
        gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);
        gtk_widget_set_hexpand (tv, TRUE);
        gtk_box_pack_start (GTK_BOX (vbox), tv, FALSE, TRUE, 0);
        g_object_unref (buf);
    }
    if (code_buf) {
        /* Unclosed code fence */
        GtkWidget *cb = build_code_block_widget (code_lang, code_buf->str);
        gtk_box_pack_start (GTK_BOX (vbox), cb, FALSE, TRUE, 0);
        g_string_free (code_buf, TRUE);
        g_free (code_lang);
    }

    g_string_free (prose_buf, TRUE);
    g_strfreev (lines);
    return vbox;
}

/* ================================================================== */
/* Chat bubble builder                                                  */
/* ================================================================== */

static void
scroll_to_bottom (AiPanelData *data)
{
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (
                             GTK_SCROLLED_WINDOW (data->scroll));
    gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj));
}

/* Add a complete (non-streaming) chat bubble */
static void
add_complete_bubble (AiPanelData *data,
                     const gchar *role_label,
                     const gchar *text,
                     gboolean     is_user)
{
    GtkWidget *row = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
    gtk_list_box_row_set_selectable  (GTK_LIST_BOX_ROW (row), FALSE);

    GtkWidget *bubble = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class (gtk_widget_get_style_context (bubble),
                                 is_user ? "bubble-user" : "bubble-ai");
    gtk_widget_set_halign (bubble, is_user ? GTK_ALIGN_END : GTK_ALIGN_FILL);
    gtk_widget_set_hexpand (bubble, !is_user);

    /* Role label */
    GtkWidget *role_lbl = gtk_label_new (role_label);
    gtk_widget_set_halign (role_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (role_lbl), "bubble-role");
    gtk_box_pack_start (GTK_BOX (bubble), role_lbl, FALSE, FALSE, 0);

    if (is_user) {
        /* User messages: simple selectable label */
        GtkWidget *lbl = gtk_label_new (text);
        gtk_label_set_line_wrap (GTK_LABEL (lbl), TRUE);
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_label_set_selectable (GTK_LABEL (lbl), TRUE);
        gtk_widget_set_halign (lbl, GTK_ALIGN_START);
        gtk_box_pack_start (GTK_BOX (bubble), lbl, FALSE, TRUE, 0);
    } else {
        /* AI messages: full markdown rendering */
        GtkWidget *content = render_markdown_to_box (text);
        gtk_box_pack_start (GTK_BOX (bubble), content, TRUE, TRUE, 0);
    }

    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top    (outer, 6);
    gtk_widget_set_margin_bottom (outer, 6);
    gtk_widget_set_margin_start  (outer, 4);
    gtk_widget_set_margin_end    (outer, 4);
    gtk_box_pack_start (GTK_BOX (outer), bubble, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (row), outer);
    gtk_list_box_insert (GTK_LIST_BOX (data->chat_list), row, -1);
    gtk_widget_show_all (row);

    /* Scroll to bottom (deferred so layout finishes first) */
    g_idle_add_once ((GSourceOnceFunc)scroll_to_bottom, data);
}

/* ================================================================== */
/* Streaming row — appears token by token                              */
/* ================================================================== */

static void
begin_streaming_row (AiPanelData *data)
{
    GtkWidget *row = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
    gtk_list_box_row_set_selectable  (GTK_LIST_BOX_ROW (row), FALSE);

    GtkWidget *bubble = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class (gtk_widget_get_style_context (bubble), "bubble-ai");
    gtk_widget_set_hexpand (bubble, TRUE);

    GtkWidget *role_lbl = gtk_label_new ("AETHER AI");
    gtk_widget_set_halign (role_lbl, GTK_ALIGN_START);
    gtk_style_context_add_class (gtk_widget_get_style_context (role_lbl), "bubble-role");
    gtk_box_pack_start (GTK_BOX (bubble), role_lbl, FALSE, FALSE, 0);

    /* Simple text view for streaming (will be replaced by markdown view on completion) */
    GtkTextBuffer *buf = gtk_text_buffer_new (NULL);
    ensure_text_tags (buf);

    GtkWidget *tv = gtk_text_view_new_with_buffer (buf);
    gtk_text_view_set_editable      (GTK_TEXT_VIEW (tv), FALSE);
    gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (tv), FALSE);
    gtk_text_view_set_wrap_mode     (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_hexpand (tv, TRUE);
    gtk_box_pack_start (GTK_BOX (bubble), tv, FALSE, TRUE, 0);

    /* Store in data for token appending */
    data->streaming_buf = buf;
    data->streaming_tag = gtk_text_buffer_get_tag_table (buf) ?
                          gtk_text_buffer_create_tag (buf, "stream-cursor",
                              "background", "rgba(99,179,237,0.4)", NULL) : NULL;

    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top    (outer, 6);
    gtk_widget_set_margin_bottom (outer, 6);
    gtk_widget_set_margin_start  (outer, 4);
    gtk_widget_set_margin_end    (outer, 4);
    gtk_box_pack_start (GTK_BOX (outer), bubble, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (row), outer);
    gtk_list_box_insert (GTK_LIST_BOX (data->chat_list), row, -1);
    gtk_widget_show_all (row);

    data->streaming_row = row;
    g_object_unref (buf);
}

/* Append a token to the current streaming row */
static void
append_streaming_token (AiPanelData *data, const gchar *token)
{
    if (!data->streaming_buf) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter (data->streaming_buf, &end);
    gtk_text_buffer_insert (data->streaming_buf, &end, token, -1);

    /* Auto-scroll */
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment (
                             GTK_SCROLLED_WINDOW (data->scroll));
    gdouble upper = gtk_adjustment_get_upper (adj);
    gdouble page  = gtk_adjustment_get_page_size (adj);
    gdouble val   = gtk_adjustment_get_value (adj);
    /* Only auto-scroll if user was near the bottom */
    if (val >= upper - page - 80)
        gtk_adjustment_set_value (adj, upper);
}

/* When streaming is done, replace the simple text view with full markdown */
static void
finalize_streaming_row (AiPanelData *data, const gchar *full_text)
{
    if (!data->streaming_row) return;

    /* Get the bubble box (outer → bubble) */
    GtkWidget *outer  = gtk_bin_get_child (GTK_BIN (data->streaming_row));
    GtkWidget *bubble = NULL;
    if (outer) {
        GList *children = gtk_container_get_children (GTK_CONTAINER (outer));
        if (children) {
            bubble = GTK_WIDGET (children->data);
            g_list_free (children);
        }
    }

    if (bubble) {
        /* Remove all children after the role label */
        GList *ch = gtk_container_get_children (GTK_CONTAINER (bubble));
        gboolean first = TRUE;
        for (GList *l = ch; l; l = l->next) {
            if (first) { first = FALSE; continue; }
            gtk_widget_destroy (GTK_WIDGET (l->data));
        }
        g_list_free (ch);

        /* Add the proper markdown-rendered content */
        GtkWidget *content = render_markdown_to_box (full_text);
        gtk_box_pack_start (GTK_BOX (bubble), content, TRUE, TRUE, 0);
        gtk_widget_show_all (bubble);
    }

    data->streaming_row = NULL;
    data->streaming_buf = NULL;

    g_idle_add_once ((GSourceOnceFunc)scroll_to_bottom, data);
}

/* ================================================================== */
/* Agent callbacks                                                      */
/* ================================================================== */

typedef struct {
    AiPanelData *data;
    GString     *accumulated;
} StreamState;

static void
on_agent_token (const gchar *token, gpointer user_data)
{
    StreamState *state = (StreamState *) user_data;

    if (!state->data->streaming_row)
        begin_streaming_row (state->data);

    append_streaming_token (state->data, token);
    g_string_append (state->accumulated, token);
}

/* Forward declaration — defined later in Conversation persistence section */
static void persist_new_messages (AiPanelData *data);

static void
on_agent_done (const gchar *error_msg, gpointer user_data)
{
    StreamState *state = (StreamState *) user_data;
    AiPanelData *data  = state->data;

    /* Hide thinking indicator */
    gtk_widget_hide (data->status_bar);
    gtk_spinner_stop (GTK_SPINNER (data->spinner));
    gtk_widget_set_sensitive (data->send_btn, TRUE);
    gtk_widget_set_sensitive (data->context_btn, TRUE);
    gtk_widget_hide (data->cancel_btn);
    data->is_streaming = FALSE;

    if (error_msg) {
        /* Remove incomplete streaming row if present */
        if (data->streaming_row) {
            gtk_widget_destroy (data->streaming_row);
            data->streaming_row = NULL;
            data->streaming_buf = NULL;
        }
        /* Show error as a bubble */
        gchar *err_text = g_strdup_printf ("⚠️ **Error:** %s", error_msg);
        add_complete_bubble (data, "AETHER AI", err_text, FALSE);
        g_free (err_text);
    } else {
        /* Replace streaming row with final markdown-rendered version */
        if (state->accumulated->len > 0)
            finalize_streaming_row (data, state->accumulated->str);
        else if (data->streaming_row) {
            gtk_widget_destroy (data->streaming_row);
            data->streaming_row = NULL;
        }

        /* ---- Persist the exchange ---- */
        persist_new_messages (data);

        /* ---- Auto-title after first exchange ---- */
        if (!data->conv_titled && data->pending_user_text) {
            gchar *auto_title = ai_conversation_auto_title (data->pending_user_text);
            ai_conversation_set_title (data->conv_id, auto_title);
            if (data->conv_title_label)
                gtk_label_set_text (GTK_LABEL (data->conv_title_label), auto_title);
            g_free (auto_title);
            data->conv_titled = TRUE;
            g_free (data->pending_user_text);
            data->pending_user_text = NULL;
        }
    }

    g_string_free (state->accumulated, TRUE);
    g_free (state);
}

/* ================================================================== */
/* Conversation persistence helpers                                     */
/* ================================================================== */

/* Format a Unix timestamp as a relative string: "just now", "5m ago", etc. */
static gchar *
format_relative_time (gint64 unix_secs)
{
    gint64 now  = g_get_real_time () / G_USEC_PER_SEC;
    gint64 diff = now - unix_secs;
    if (diff < 0)    diff = 0;
    if (diff < 60)   return g_strdup ("just now");
    if (diff < 3600) return g_strdup_printf ("%"G_GINT64_FORMAT"m ago", diff / 60);
    if (diff < 86400)return g_strdup_printf ("%"G_GINT64_FORMAT"h ago", diff / 3600);
    if (diff < 604800)return g_strdup_printf ("%"G_GINT64_FORMAT"d ago", diff / 86400);
    return g_strdup_printf ("%"G_GINT64_FORMAT"w ago", diff / 604800);
}

/* Save all messages from presend_history_len onward to the conversation file */
static void
persist_new_messages (AiPanelData *data)
{
    gint total = ai_agent_session_get_message_count (data->session);
    for (gint i = data->presend_history_len; i < total; i++) {
        const AiMessage *msg = ai_agent_session_get_message_at (data->session, i);
        if (!msg) continue;
        /* Skip system messages — regenerated from context on load */
        if (msg->role == AI_ROLE_SYSTEM) continue;
        ai_conversation_append_message (data->conv_id, msg->role, msg->content,
                                        msg->tool_call_id, msg->tool_name);
    }
    ai_conversation_increment_turn (data->conv_id);
}

/* ================================================================== */
/* Conversation switcher popover                                        */
/* ================================================================== */

/* Forward declaration */
static void clear_chat_ui (AiPanelData *data);

typedef struct {
    AiPanelData *panel;
    gchar       *conv_id;   /* owned */
} ConvActionCtx;

static void
conv_action_ctx_free (ConvActionCtx *ctx)
{
    g_free (ctx->conv_id);
    g_free (ctx);
}

/* Load all messages from a saved conversation into the panel */
static void
load_conversation_into_panel (AiPanelData *data, const gchar *conv_id)
{
    if (!conv_id || !*conv_id) return;

    /* Cancel any in-flight request */
    if (data->is_streaming) {
        ai_agent_session_cancel (data->session);
        gtk_widget_hide (data->status_bar);
        gtk_spinner_stop (GTK_SPINNER (data->spinner));
        gtk_widget_set_sensitive (data->send_btn, TRUE);
        gtk_widget_set_sensitive (data->context_btn, TRUE);
        gtk_widget_hide (data->cancel_btn);
        data->is_streaming = FALSE;
        data->streaming_row = NULL;
        data->streaming_buf = NULL;
    }

    /* Reset session (keeps fresh system prompt) */
    ai_agent_session_reset (data->session);

    /* Clear chat UI */
    clear_chat_ui (data);

    /* Load messages from file */
    GPtrArray *msgs = ai_conversation_load_messages (conv_id);
    if (!msgs) return;

    /* Load messages into session history */
    ai_agent_session_load_history (data->session, msgs);

    /* Rebuild UI: show user and assistant messages as bubbles */
    for (guint i = 0; i < msgs->len; i++) {
        const AiMessage *msg = (const AiMessage *) g_ptr_array_index (msgs, i);
        if (msg->role == AI_ROLE_USER) {
            add_complete_bubble (data, "YOU", msg->content ? msg->content : "", TRUE);
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            add_complete_bubble (data, "AETHER AI", msg->content ? msg->content : "", FALSE);
        }
        /* Tool messages are not shown in the UI — they're internal context */
    }
    g_ptr_array_free (msgs, TRUE);

    /* Switch to this conversation */
    g_free (data->conv_id);
    data->conv_id             = g_strdup (conv_id);
    data->presend_history_len = ai_agent_session_get_message_count (data->session);
    data->conv_titled         = TRUE;   /* Loaded conversations already have titles */
    g_free (data->pending_user_text);
    data->pending_user_text   = NULL;

    /* Close popover */
    if (data->conv_popover)
        gtk_popover_popdown (GTK_POPOVER (data->conv_popover));

    /* Update window title label if present */
    if (data->conv_title_label) {
        GPtrArray *all = ai_conversation_list_all ();
        for (guint i = 0; i < all->len; i++) {
            const AiConversationMeta *m = (const AiConversationMeta *) g_ptr_array_index (all, i);
            if (g_strcmp0 (m->id, conv_id) == 0) {
                gtk_label_set_text (GTK_LABEL (data->conv_title_label), m->title);
                break;
            }
        }
        g_ptr_array_free (all, TRUE);
    }
}

static void
on_conv_load_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    ConvActionCtx *ctx = (ConvActionCtx *) user_data;
    load_conversation_into_panel (ctx->panel, ctx->conv_id);
}

static void
on_conv_delete_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    ConvActionCtx *ctx  = (ConvActionCtx *) user_data;
    AiPanelData   *data = ctx->panel;

    /* Confirm deletion */
    GtkWidget *dialog = gtk_message_dialog_new (
        data->window ? GTK_WINDOW (data->window) : NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Delete this conversation? This cannot be undone.");
    gtk_window_set_title (GTK_WINDOW (dialog), "Delete Conversation");
    gint resp = gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);

    if (resp != GTK_RESPONSE_YES) return;

    ai_conversation_delete (ctx->conv_id);

    /* If this was the active conversation, start a new one */
    if (g_strcmp0 (data->conv_id, ctx->conv_id) == 0) {
        g_free (data->conv_id);
        data->conv_id   = ai_conversation_new_id ();
        data->conv_titled = FALSE;
        g_free (data->pending_user_text);
        data->pending_user_text = NULL;
        ai_agent_session_reset (data->session);
        clear_chat_ui (data);
        if (data->conv_title_label)
            gtk_label_set_text (GTK_LABEL (data->conv_title_label), "New Chat");
    }

    /* Refresh the popover list */
    if (data->conv_popover) {
        gtk_popover_popdown (GTK_POPOVER (data->conv_popover));
    }
}

/* Populate the conversation list box inside the popover */
static void
refresh_conv_list (AiPanelData *data)
{
    if (!data->conv_list_box) return;

    /* Clear existing rows */
    GList *rows = gtk_container_get_children (GTK_CONTAINER (data->conv_list_box));
    for (GList *l = rows; l; l = l->next)
        gtk_widget_destroy (GTK_WIDGET (l->data));
    g_list_free (rows);

    GPtrArray *all = ai_conversation_list_all ();

    if (all->len == 0) {
        GtkWidget *empty = gtk_label_new ("No saved conversations");
        gtk_style_context_add_class (gtk_widget_get_style_context (empty), "dim-label");
        gtk_widget_set_margin_top    (empty, 16);
        gtk_widget_set_margin_bottom (empty, 16);
        gtk_list_box_insert (GTK_LIST_BOX (data->conv_list_box), empty, -1);
        gtk_widget_show (empty);
        g_ptr_array_free (all, TRUE);
        return;
    }

    for (guint i = 0; i < all->len; i++) {
        const AiConversationMeta *meta =
            (const AiConversationMeta *) g_ptr_array_index (all, i);

        gboolean is_active = (g_strcmp0 (meta->id, data->conv_id) == 0);

        GtkWidget *row = gtk_list_box_row_new ();
        gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
        gtk_list_box_row_set_selectable  (GTK_LIST_BOX_ROW (row), FALSE);

        GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_style_context_add_class (gtk_widget_get_style_context (hbox), "conv-row");
        if (is_active)
            gtk_style_context_add_class (gtk_widget_get_style_context (hbox), "conv-row-active");

        /* Text column */
        GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand (vbox, TRUE);

        /* Title */
        gchar *safe_title = g_markup_escape_text (meta->title, -1);
        gchar *title_markup;
        if (is_active)
            title_markup = g_strdup_printf ("<b>%s</b>", safe_title);
        else
            title_markup = g_strdup (safe_title);
        g_free (safe_title);

        GtkWidget *title_lbl = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (title_lbl), title_markup);
        gtk_label_set_ellipsize (GTK_LABEL (title_lbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars (GTK_LABEL (title_lbl), 28);
        gtk_widget_set_halign (title_lbl, GTK_ALIGN_START);
        gtk_style_context_add_class (gtk_widget_get_style_context (title_lbl), "conv-row-title");
        g_free (title_markup);

        /* Meta: turns count + relative time */
        gchar *rel_time = format_relative_time (meta->modified_at);
        gchar *meta_text = g_strdup_printf ("%d turn%s • %s",
            meta->turn_count,
            meta->turn_count == 1 ? "" : "s",
            rel_time);
        g_free (rel_time);

        GtkWidget *meta_lbl = gtk_label_new (meta_text);
        g_free (meta_text);
        gtk_widget_set_halign (meta_lbl, GTK_ALIGN_START);
        gtk_style_context_add_class (gtk_widget_get_style_context (meta_lbl), "conv-row-meta");

        gtk_box_pack_start (GTK_BOX (vbox), title_lbl, FALSE, FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), meta_lbl,  FALSE, FALSE, 0);

        /* Load button (only for inactive conversations) */
        if (!is_active) {
            ConvActionCtx *load_ctx = g_new0 (ConvActionCtx, 1);
            load_ctx->panel   = data;
            load_ctx->conv_id = g_strdup (meta->id);

            GtkWidget *load_btn = gtk_button_new_with_label ("↵ Open");
            gtk_style_context_add_class (gtk_widget_get_style_context (load_btn), "flat");
            gtk_widget_set_valign (load_btn, GTK_ALIGN_CENTER);
            g_signal_connect_data (load_btn, "clicked",
                                   G_CALLBACK (on_conv_load_clicked),
                                   load_ctx, (GClosureNotify)conv_action_ctx_free, 0);
            gtk_box_pack_end (GTK_BOX (hbox), load_btn, FALSE, FALSE, 0);
        } else {
            GtkWidget *active_lbl = gtk_label_new ("● active");
            gtk_style_context_add_class (gtk_widget_get_style_context (active_lbl), "dim-label");
            gtk_widget_set_valign (active_lbl, GTK_ALIGN_CENTER);
            gtk_box_pack_end (GTK_BOX (hbox), active_lbl, FALSE, FALSE, 0);
        }

        /* Delete button */
        {
            ConvActionCtx *del_ctx = g_new0 (ConvActionCtx, 1);
            del_ctx->panel   = data;
            del_ctx->conv_id = g_strdup (meta->id);

            GtkWidget *del_btn = gtk_button_new_from_icon_name ("user-trash-symbolic",
                                                                 GTK_ICON_SIZE_SMALL_TOOLBAR);
            gtk_style_context_add_class (gtk_widget_get_style_context (del_btn), "conv-delete-btn");
            gtk_button_set_relief (GTK_BUTTON (del_btn), GTK_RELIEF_NONE);
            gtk_widget_set_valign (del_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text (del_btn, "Delete this conversation");
            g_signal_connect_data (del_btn, "clicked",
                                   G_CALLBACK (on_conv_delete_clicked),
                                   del_ctx, (GClosureNotify)conv_action_ctx_free, 0);
            gtk_box_pack_end (GTK_BOX (hbox), del_btn, FALSE, FALSE, 0);
        }

        gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
        gtk_container_add (GTK_CONTAINER (row), hbox);
        gtk_list_box_insert (GTK_LIST_BOX (data->conv_list_box), row, -1);
        gtk_widget_show_all (row);
    }

    g_ptr_array_free (all, TRUE);
}

static void
on_conv_btn_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;
    refresh_conv_list (data);
    gtk_popover_popup (GTK_POPOVER (data->conv_popover));
}

static void
on_conv_new_btn_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;
    gtk_popover_popdown (GTK_POPOVER (data->conv_popover));

    /* Trigger "new chat" */
    if (data->is_streaming) {
        ai_agent_session_cancel (data->session);
        gtk_widget_hide (data->status_bar);
        gtk_spinner_stop (GTK_SPINNER (data->spinner));
        gtk_widget_set_sensitive (data->send_btn, TRUE);
        gtk_widget_set_sensitive (data->context_btn, TRUE);
        gtk_widget_hide (data->cancel_btn);
        data->is_streaming = FALSE;
        data->streaming_row = NULL;
        data->streaming_buf = NULL;
    }
    ai_agent_session_reset (data->session);
    clear_chat_ui (data);

    g_free (data->conv_id);
    data->conv_id             = ai_conversation_new_id ();
    data->conv_titled         = FALSE;
    data->presend_history_len = ai_agent_session_get_message_count (data->session);
    g_free (data->pending_user_text);
    data->pending_user_text   = NULL;

    if (data->conv_title_label)
        gtk_label_set_text (GTK_LABEL (data->conv_title_label), "New Chat");
}

/* Build the conversations popover (called once) */
static GtkWidget *
build_conv_popover (GtkWidget *relative_to, AiPanelData *data)
{
    GtkWidget *popover = gtk_popover_new (relative_to);
    gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_BOTTOM);
    gtk_widget_set_size_request (popover, 300, -1);
    gtk_style_context_add_class (gtk_widget_get_style_context (popover), "conv-popover");

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    /* Header */
    GtkWidget *hdr = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class (gtk_widget_get_style_context (hdr), "conv-popover-header");
    gtk_widget_set_margin_start  (hdr, 12);
    gtk_widget_set_margin_end    (hdr, 8);
    gtk_widget_set_margin_top    (hdr, 8);
    gtk_widget_set_margin_bottom (hdr, 8);

    GtkWidget *hdr_lbl = gtk_label_new ("💬 Conversations");
    gtk_widget_set_hexpand (hdr_lbl, TRUE);
    gtk_widget_set_halign  (hdr_lbl, GTK_ALIGN_START);

    GtkWidget *new_btn = gtk_button_new_with_label ("+ New");
    gtk_style_context_add_class (gtk_widget_get_style_context (new_btn), "conv-new-btn");
    g_signal_connect (new_btn, "clicked", G_CALLBACK (on_conv_new_btn_clicked), data);

    gtk_box_pack_start (GTK_BOX (hdr), hdr_lbl,  TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (hdr), new_btn,  FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hdr, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox),
        gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* Scrollable conversation list */
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (scroll, -1, 280);

    data->conv_list_box = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (data->conv_list_box), GTK_SELECTION_NONE);
    gtk_container_add (GTK_CONTAINER (scroll), data->conv_list_box);
    gtk_box_pack_start (GTK_BOX (vbox), scroll, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (popover), vbox);
    gtk_widget_show_all (vbox);
    return popover;
}

/* ================================================================== */
/* Chat UI clear helper                                                 */
/* ================================================================== */

static void
clear_chat_ui (AiPanelData *data)
{
    GList *rows = gtk_container_get_children (GTK_CONTAINER (data->chat_list));
    for (GList *l = rows; l; l = l->next)
        gtk_widget_destroy (GTK_WIDGET (l->data));
    g_list_free (rows);
    data->streaming_row = NULL;
    data->streaming_buf = NULL;
}

/* ================================================================== */
/* Send logic                                                           */
/* ================================================================== */

static void
do_send (AiPanelData *data)
{
    if (data->is_streaming) return;

    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->input_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
    gchar *trimmed = g_strstrip (g_strdup (text));
    g_free (text);

    if (!trimmed || !*trimmed) {
        g_free (trimmed);
        return;
    }

    /* If "attach context" is enabled, prepend file info */
    gchar *final_prompt;
    if (data->attach_context && data->window) {
        AiContext *ctx = ai_context_capture (data->window);
        if (ctx->active_file_path) {
            final_prompt = g_strdup_printf (
                "**Active file:** `%s` (line %d)\n\n%s",
                ctx->active_file_path, ctx->cursor_line, trimmed);
        } else {
            final_prompt = g_strdup (trimmed);
        }
        ai_context_free (ctx);
        /* Reset context toggle */
        data->attach_context = FALSE;
        gtk_style_context_remove_class (
            gtk_widget_get_style_context (data->context_btn), "context-btn-active");
        gtk_button_set_label (GTK_BUTTON (data->context_btn), "📎 Context");
    } else {
        final_prompt = g_strdup (trimmed);
    }
    g_free (trimmed);

    /* Show user bubble */
    add_complete_bubble (data, "YOU", final_prompt, TRUE);

    /* Clear input */
    gtk_text_buffer_set_text (buf, "", -1);

    /* Show thinking indicator */
    gtk_widget_show (data->status_bar);
    gtk_spinner_start (GTK_SPINNER (data->spinner));
    gtk_label_set_text (GTK_LABEL (data->thinking_label), "AI is thinking…");
    gtk_widget_set_sensitive (data->send_btn, FALSE);
    gtk_widget_set_sensitive (data->context_btn, FALSE);
    gtk_widget_show (data->cancel_btn);
    data->is_streaming = TRUE;

    /* Update model badge */
    const AiSettings *settings = ai_settings_get ();
    gchar *badge_text = g_strdup_printf ("%s / %s",
        ai_provider_get_name (settings->provider),
        settings->model_name ? settings->model_name : "?");
    gtk_label_set_text (GTK_LABEL (data->model_badge), badge_text);
    g_free (badge_text);

    /* Record history length before the agent appends the user message */
    data->presend_history_len = ai_agent_session_get_message_count (data->session);

    /* Save first user message for auto-titling */
    if (!data->conv_titled && !data->pending_user_text)
        data->pending_user_text = g_strdup (final_prompt);

    /* Send to agent */
    StreamState *state       = g_new0 (StreamState, 1);
    state->data              = data;
    state->accumulated       = g_string_new ("");

    ai_agent_session_send (data->session, final_prompt,
                           on_agent_token, on_agent_done, state);
    g_free (final_prompt);
}

/* ================================================================== */
/* Signal handlers                                                      */
/* ================================================================== */

static void
on_send_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    do_send ((AiPanelData *) user_data);
}

static void
on_cancel_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;
    ai_agent_session_cancel (data->session);
    /* on_agent_done will NOT be called — cleanup manually */
    gtk_widget_hide (data->status_bar);
    gtk_spinner_stop (GTK_SPINNER (data->spinner));
    gtk_widget_set_sensitive (data->send_btn, TRUE);
    gtk_widget_set_sensitive (data->context_btn, TRUE);
    gtk_widget_hide (data->cancel_btn);
    data->is_streaming = FALSE;

    if (data->streaming_row) {
        /* Show partial response */
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds (data->streaming_buf, &s, &e);
        gchar *partial = gtk_text_buffer_get_text (data->streaming_buf, &s, &e, FALSE);
        if (partial && *partial) {
            gchar *with_note = g_strdup_printf ("%s\n\n*(cancelled)*", partial);
            finalize_streaming_row (data, with_note);
            g_free (with_note);
        } else {
            gtk_widget_destroy (data->streaming_row);
            data->streaming_row = NULL;
            data->streaming_buf = NULL;
        }
        g_free (partial);
    }
}

static void
on_settings_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;
    ai_settings_show_dialog (GTK_WINDOW (data->window));

    /* Update badge after settings change */
    const AiSettings *settings = ai_settings_get ();
    gchar *badge_text = g_strdup_printf ("%s / %s",
        ai_provider_get_name (settings->provider),
        settings->model_name ? settings->model_name : "?");
    gtk_label_set_text (GTK_LABEL (data->model_badge), badge_text);
    g_free (badge_text);
}

static void
on_new_chat_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;

    /* Cancel any in-flight request */
    if (data->is_streaming)
        on_cancel_clicked (NULL, data);

    /* Reset session */
    ai_agent_session_reset (data->session);

    /* Clear chat UI */
    clear_chat_ui (data);

    /* Start a fresh conversation */
    g_free (data->conv_id);
    data->conv_id             = ai_conversation_new_id ();
    data->conv_titled         = FALSE;
    data->presend_history_len = ai_agent_session_get_message_count (data->session);
    g_free (data->pending_user_text);
    data->pending_user_text   = NULL;

    if (data->conv_title_label)
        gtk_label_set_text (GTK_LABEL (data->conv_title_label), "New Chat");
}

static void
on_context_btn_clicked (GtkButton *btn, gpointer user_data)
{
    (void) btn;
    AiPanelData *data = (AiPanelData *) user_data;
    data->attach_context = !data->attach_context;

    if (data->attach_context) {
        gtk_style_context_add_class (
            gtk_widget_get_style_context (data->context_btn), "context-btn-active");
        gtk_button_set_label (GTK_BUTTON (data->context_btn), "📎 Context ✓");
    } else {
        gtk_style_context_remove_class (
            gtk_widget_get_style_context (data->context_btn), "context-btn-active");
        gtk_button_set_label (GTK_BUTTON (data->context_btn), "📎 Context");
    }
}

static gboolean
on_input_key_press (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void) widget;
    /* Ctrl+Enter to send */
    if (event->keyval == GDK_KEY_Return &&
        (event->state & GDK_CONTROL_MASK)) {
        do_send ((AiPanelData *) user_data);
        return TRUE;
    }
    return FALSE;
}

/* ================================================================== */
/* Panel constructor                                                    */
/* ================================================================== */

static void
free_panel_data (gpointer user_data)
{
    AiPanelData *data = (AiPanelData *) user_data;
    ai_agent_session_free (data->session);
    g_free (data->conv_id);
    g_free (data->pending_user_text);
    g_free (data);
}

GtkWidget *
ai_panel_new (AetherIdeWindow *window)
{
    ensure_css ();

    AiPanelData *data = g_new0 (AiPanelData, 1);
    data->window      = window;
    data->session     = ai_agent_session_new ("Chat 1");

    /* Initialise or load most-recent conversation */
    {
        GPtrArray *existing = ai_conversation_list_all ();
        if (existing->len > 0) {
            /* Load the most recent conversation ID (already sorted by modified_at) */
            const AiConversationMeta *latest =
                (const AiConversationMeta *) g_ptr_array_index (existing, 0);
            data->conv_id   = g_strdup (latest->id);
            data->conv_titled = TRUE;   /* Will be loaded from file */
        } else {
            data->conv_id   = ai_conversation_new_id ();
            data->conv_titled = FALSE;
        }
        data->presend_history_len = ai_agent_session_get_message_count (data->session);
        g_ptr_array_free (existing, TRUE);
    }

    GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class (gtk_widget_get_style_context (root), "ai-panel");
    g_object_set_data_full (G_OBJECT (root), "panel_data", data, free_panel_data);

    /* ---------------------------------------------------------------- */
    /* Header                                                            */
    /* ---------------------------------------------------------------- */
    GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class (gtk_widget_get_style_context (header), "chat-header");
    gtk_widget_set_margin_top    (header, 4);
    gtk_widget_set_margin_bottom (header, 4);
    gtk_widget_set_margin_start  (header, 8);
    gtk_widget_set_margin_end    (header, 8);

    /* Title */
    GtkWidget *title = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (title), "<b>AetherAI</b>");
    gtk_widget_set_hexpand (title, FALSE);

    /* Current conversation title */
    data->conv_title_label = gtk_label_new ("New Chat");
    gtk_label_set_ellipsize (GTK_LABEL (data->conv_title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (data->conv_title_label), 18);
    gtk_style_context_add_class (gtk_widget_get_style_context (data->conv_title_label),
                                 "conv-title-label");
    gtk_widget_set_hexpand (data->conv_title_label, TRUE);
    gtk_widget_set_halign  (data->conv_title_label, GTK_ALIGN_START);

    /* Conversations history button */
    data->conv_btn = gtk_button_new_from_icon_name ("document-open-recent-symbolic",
                                                     GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_relief (GTK_BUTTON (data->conv_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (data->conv_btn, "Conversation History");
    g_signal_connect (data->conv_btn, "clicked", G_CALLBACK (on_conv_btn_clicked), data);

    /* Model badge */
    {
        const AiSettings *settings = ai_settings_get ();
        gchar *badge = g_strdup_printf ("%s / %s",
            ai_provider_get_name (settings->provider),
            settings->model_name ? settings->model_name : "?");
        data->model_badge = gtk_label_new (badge);
        g_free (badge);
    }
    gtk_style_context_add_class (gtk_widget_get_style_context (data->model_badge),
                                 "model-badge");
    gtk_widget_set_halign  (data->model_badge, GTK_ALIGN_END);

    /* New chat button */
    GtkWidget *new_btn = gtk_button_new_from_icon_name ("list-add-symbolic",
                                                         GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_relief (GTK_BUTTON (new_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (new_btn, "New Chat");
    g_signal_connect (new_btn, "clicked", G_CALLBACK (on_new_chat_clicked), data);

    /* Settings button */
    GtkWidget *settings_btn = gtk_button_new_from_icon_name ("emblem-system-symbolic",
                                                              GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_relief (GTK_BUTTON (settings_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (settings_btn, "AI Settings");
    g_signal_connect (settings_btn, "clicked", G_CALLBACK (on_settings_clicked), data);

    gtk_box_pack_start (GTK_BOX (header), title,            FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (header), data->conv_title_label, TRUE,  TRUE,  2);
    gtk_box_pack_start (GTK_BOX (header), data->model_badge,      FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (header), data->conv_btn,         FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (header), new_btn,                FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (header), settings_btn,           FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (root), header, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (root),
                        gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* ---------------------------------------------------------------- */
    /* Chat history                                                      */
    /* ---------------------------------------------------------------- */
    data->scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (data->scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    data->chat_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (data->chat_list), GTK_SELECTION_NONE);
    gtk_container_add (GTK_CONTAINER (data->scroll), data->chat_list);
    gtk_box_pack_start (GTK_BOX (root), data->scroll, TRUE, TRUE, 0);

    /* ---------------------------------------------------------------- */
    /* Thinking / status bar                                             */
    /* ---------------------------------------------------------------- */
    data->status_bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start  (data->status_bar, 12);
    gtk_widget_set_margin_end    (data->status_bar, 12);
    gtk_widget_set_margin_top    (data->status_bar, 4);
    gtk_widget_set_margin_bottom (data->status_bar, 4);

    data->spinner = gtk_spinner_new ();
    gtk_widget_set_size_request (data->spinner, 14, 14);

    data->thinking_label = gtk_label_new ("AI is thinking…");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (data->thinking_label), "dim-label");

    gtk_box_pack_start (GTK_BOX (data->status_bar), data->spinner,       FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (data->status_bar), data->thinking_label, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (root), data->status_bar, FALSE, FALSE, 0);
    gtk_widget_hide (data->status_bar);

    /* ---------------------------------------------------------------- */
    /* Separator                                                         */
    /* ---------------------------------------------------------------- */
    gtk_box_pack_start (GTK_BOX (root),
                        gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    /* ---------------------------------------------------------------- */
    /* Input area                                                        */
    /* ---------------------------------------------------------------- */
    GtkWidget *input_area = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class (gtk_widget_get_style_context (input_area), "input-area");

    /* Top row: context + cancel buttons */
    GtkWidget *top_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

    data->context_btn = gtk_button_new_with_label ("📎 Context");
    gtk_style_context_add_class (gtk_widget_get_style_context (data->context_btn),
                                 "context-btn");
    gtk_widget_set_tooltip_text (data->context_btn,
                                 "Attach current file context to next message");
    g_signal_connect (data->context_btn, "clicked",
                      G_CALLBACK (on_context_btn_clicked), data);

    data->cancel_btn = gtk_button_new_with_label ("⏹ Stop");
    gtk_style_context_add_class (gtk_widget_get_style_context (data->cancel_btn),
                                 "cancel-btn");
    gtk_widget_set_tooltip_text (data->cancel_btn, "Cancel current AI response");
    g_signal_connect (data->cancel_btn, "clicked",
                      G_CALLBACK (on_cancel_clicked), data);
    gtk_widget_hide (data->cancel_btn);

    gtk_box_pack_start (GTK_BOX (top_row), data->context_btn, FALSE, FALSE, 0);
    gtk_box_pack_end   (GTK_BOX (top_row), data->cancel_btn,  FALSE, FALSE, 0);

    /* Text input */
    GtkWidget *input_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (input_scroll),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (input_scroll),
                                         GTK_SHADOW_IN);
    gtk_widget_set_size_request (input_scroll, -1, 90);

    data->input_view = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (data->input_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_tooltip_text (data->input_view, "Type a message… (Ctrl+Enter to send)");
    g_signal_connect (data->input_view, "key-press-event",
                      G_CALLBACK (on_input_key_press), data);
    gtk_container_add (GTK_CONTAINER (input_scroll), data->input_view);

    /* Bottom row: send button */
    GtkWidget *bottom_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *hint = gtk_label_new ("Ctrl+Enter to send");
    gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
    gtk_widget_set_hexpand (hint, TRUE);
    gtk_widget_set_halign (hint, GTK_ALIGN_START);

    data->send_btn = gtk_button_new_with_label ("Send ➤");
    gtk_style_context_add_class (gtk_widget_get_style_context (data->send_btn), "send-btn");
    g_signal_connect (data->send_btn, "clicked", G_CALLBACK (on_send_clicked), data);

    gtk_box_pack_start (GTK_BOX (bottom_row), hint,           TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (bottom_row), data->send_btn, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (input_area), top_row,      FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (input_area), input_scroll, TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (input_area), bottom_row,   FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (root), input_area, FALSE, FALSE, 0);

    gtk_widget_show_all (root);
    gtk_widget_hide (data->status_bar);
    gtk_widget_hide (data->cancel_btn);

    /* Build conversation popover (attached to conv_btn) */
    data->conv_popover = build_conv_popover (data->conv_btn, data);

    /* If we have a saved conversation, load it now */
    if (data->conv_titled && data->conv_id) {
        /* conv_titled was set TRUE because we have an existing conv — load it */
        load_conversation_into_panel (data, data->conv_id);
    }

    return root;
}
