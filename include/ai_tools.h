#ifndef AI_TOOLS_H
#define AI_TOOLS_H

/*
 * ai_tools.h — Extended native tool system for the AI Agent
 *
 * Tools are native C functions exposed to the AI model via the
 * function-calling / tool-use API. Each tool has:
 *   - A JSON schema definition (sent to the model)
 *   - A C implementation
 *   - A permission level (auto / confirm / deny)
 */

#include <glib.h>
#include <json-glib/json-glib.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Tool permission levels                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    TOOL_PERM_AUTO    = 0,  /* Execute without asking */
    TOOL_PERM_CONFIRM = 1,  /* Show confirmation dialog first */
    TOOL_PERM_DENY    = 2   /* Never execute (safety block) */
} AiToolPermission;

/* ------------------------------------------------------------------ */
/* Tool execution context                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    AetherIdeWindow *window;        /* For tools that interact with the UI */
    const gchar     *workspace_path; /* Sandbox root — tools may not escape this */
} AiToolContext;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/**
 * ai_tools_init:
 * Initialize the tool system with the IDE window reference.
 * Must be called before any tool execution.
 */
void ai_tools_init (AetherIdeWindow *window);

/**
 * ai_tools_get_definitions:
 * Returns a JsonNode (array) containing all tool schema definitions
 * in OpenAI/Anthropic tool-calling format.
 * Caller must json_node_free() the result.
 */
JsonNode *ai_tools_get_definitions (void);

/**
 * ai_tools_execute:
 * @tool_name: Name of the tool to execute.
 * @args_json: JSON string of arguments.
 *
 * Executes the named tool. For TOOL_PERM_CONFIRM tools, shows a dialog.
 * Returns a newly-allocated result string (JSON or plain text).
 * Caller must g_free().
 */
gchar *ai_tools_execute (const gchar *tool_name, const gchar *args_json);

/**
 * ai_tools_get_permission:
 * Returns the current permission level for a tool.
 */
AiToolPermission ai_tools_get_permission (const gchar *tool_name);

/**
 * ai_tools_set_permission:
 * Allows the user to override a tool's permission level.
 */
void ai_tools_set_permission (const gchar *tool_name, AiToolPermission perm);

G_END_DECLS

#endif /* AI_TOOLS_H */
