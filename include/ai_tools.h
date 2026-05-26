#ifndef AI_TOOLS_H
#define AI_TOOLS_H

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

/* 
 * Returns a JsonNode containing an array of tool definitions 
 * compatible with the OpenAI API specification.
 */
JsonNode *ai_tools_get_definitions (void);

/* 
 * Executes a tool by name with the given JSON arguments string.
 * Returns a newly allocated string containing the execution result or error message.
 */
gchar *ai_tools_execute (const gchar *tool_name, const gchar *args_json_str);

G_END_DECLS

#endif /* AI_TOOLS_H */
