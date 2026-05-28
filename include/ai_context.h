#ifndef AI_CONTEXT_H
#define AI_CONTEXT_H

/*
 * ai_context.h — IDE context capture for the AI Agent
 *
 * Captures the current state of the IDE (active file, cursor position,
 * selected text, workspace path) and formats it for injection into the
 * AI system prompt.
 */

#include <glib.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

typedef struct {
    gchar *workspace_path;    /* Absolute path to the opened folder; may be NULL */
    gchar *active_file_path;  /* Absolute path to active editor tab; may be NULL */
    gchar *active_file_content; /* Full text content; may be NULL for large files */
    gchar *language_id;       /* e.g. "c", "python", "javascript"; may be NULL */
    gint   cursor_line;       /* 1-based; 0 if unknown */
    gint   cursor_col;        /* 1-based; 0 if unknown */
    gchar *selected_text;     /* Currently selected text; may be NULL */
} AiContext;

/**
 * ai_context_capture:
 * Reads the current state from @window and allocates a new AiContext.
 * The caller must free with ai_context_free().
 */
AiContext *ai_context_capture (AetherIdeWindow *window);

/**
 * ai_context_free:
 */
void ai_context_free (AiContext *ctx);

/**
 * ai_context_build_system_prompt:
 * Builds the AI system prompt string incorporating the current context.
 * Returns a newly-allocated string; caller must g_free().
 */
gchar *ai_context_build_system_prompt (const AiContext *ctx);

/**
 * ai_context_get_active_source_view:
 * Returns the GtkSourceView of the currently active editor tab, or NULL.
 * The returned widget is owned by the notebook; do not unref.
 */
GtkWidget *ai_context_get_active_source_view (AetherIdeWindow *window);

G_END_DECLS

#endif /* AI_CONTEXT_H */
