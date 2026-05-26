/* ai_tools.c — Native tools for the AI Agent */

#include "ai_tools.h"
#include <gio/gio.h>

static void
add_tool_definition (JsonArray *array, const gchar *name, const gchar *desc, JsonObject *props, JsonArray *required)
{
    JsonObject *tool = json_object_new ();
    json_object_set_string_member (tool, "type", "function");

    JsonObject *function = json_object_new ();
    json_object_set_string_member (function, "name", name);
    json_object_set_string_member (function, "description", desc);

    JsonObject *parameters = json_object_new ();
    json_object_set_string_member (parameters, "type", "object");
    json_object_set_object_member (parameters, "properties", props);
    json_object_set_array_member (parameters, "required", required);

    json_object_set_object_member (function, "parameters", parameters);
    json_object_set_object_member (tool, "function", function);

    json_array_add_object_element (array, tool);
}

JsonNode *
ai_tools_get_definitions (void)
{
    JsonArray *tools_array = json_array_new ();

    // Tool: view_file
    {
        JsonObject *props = json_object_new ();
        JsonObject *path_prop = json_object_new ();
        json_object_set_string_member (path_prop, "type", "string");
        json_object_set_string_member (path_prop, "description", "Absolute path to the file to read.");
        json_object_set_object_member (props, "path", path_prop);

        JsonArray *req = json_array_new ();
        json_array_add_string_element (req, "path");

        add_tool_definition (tools_array, "view_file", "Reads the contents of a file.", props, req);
    }

    // Tool: edit_file
    {
        JsonObject *props = json_object_new ();
        JsonObject *path_prop = json_object_new ();
        json_object_set_string_member (path_prop, "type", "string");
        json_object_set_string_member (path_prop, "description", "Absolute path to the file to write.");
        json_object_set_object_member (props, "path", path_prop);

        JsonObject *content_prop = json_object_new ();
        json_object_set_string_member (content_prop, "type", "string");
        json_object_set_string_member (content_prop, "description", "The complete new content of the file.");
        json_object_set_object_member (props, "content", content_prop);

        JsonArray *req = json_array_new ();
        json_array_add_string_element (req, "path");
        json_array_add_string_element (req, "content");

        add_tool_definition (tools_array, "edit_file", "Overwrites a file with new content.", props, req);
    }

    // Tool: list_dir
    {
        JsonObject *props = json_object_new ();
        JsonObject *path_prop = json_object_new ();
        json_object_set_string_member (path_prop, "type", "string");
        json_object_set_string_member (path_prop, "description", "Absolute path to the directory.");
        json_object_set_object_member (props, "path", path_prop);

        JsonArray *req = json_array_new ();
        json_array_add_string_element (req, "path");

        add_tool_definition (tools_array, "list_dir", "Lists the contents of a directory.", props, req);
    }

    JsonNode *node = json_node_new (JSON_NODE_ARRAY);
    json_node_set_array (node, tools_array);
    return node;
}

static gchar *
execute_view_file (JsonObject *args)
{
    if (!json_object_has_member (args, "path")) return g_strdup ("Error: missing 'path'");
    const gchar *path = json_object_get_string_member (args, "path");

    gchar *contents = NULL;
    GError *error = NULL;
    if (g_file_get_contents (path, &contents, NULL, &error)) {
        return contents; // Caller will free
    } else {
        gchar *err_msg = g_strdup_printf ("Error: %s", error->message);
        g_error_free (error);
        return err_msg;
    }
}

static gchar *
execute_edit_file (JsonObject *args)
{
    if (!json_object_has_member (args, "path")) return g_strdup ("Error: missing 'path'");
    if (!json_object_has_member (args, "content")) return g_strdup ("Error: missing 'content'");
    
    const gchar *path = json_object_get_string_member (args, "path");
    const gchar *content = json_object_get_string_member (args, "content");

    GError *error = NULL;
    if (g_file_set_contents (path, content, -1, &error)) {
        return g_strdup_printf ("Successfully updated %s", path);
    } else {
        gchar *err_msg = g_strdup_printf ("Error: %s", error->message);
        g_error_free (error);
        return err_msg;
    }
}

static gchar *
execute_list_dir (JsonObject *args)
{
    if (!json_object_has_member (args, "path")) return g_strdup ("Error: missing 'path'");
    const gchar *path = json_object_get_string_member (args, "path");

    GError *error = NULL;
    GDir *dir = g_dir_open (path, 0, &error);
    if (!dir) {
        gchar *err_msg = g_strdup_printf ("Error: %s", error->message);
        g_error_free (error);
        return err_msg;
    }

    GString *result = g_string_new ("");
    const gchar *filename;
    while ((filename = g_dir_read_name (dir)) != NULL) {
        g_string_append_printf (result, "%s\n", filename);
    }
    g_dir_close (dir);
    
    if (result->len == 0) {
        g_string_append (result, "(Empty directory)");
    }
    return g_string_free (result, FALSE);
}

gchar *
ai_tools_execute (const gchar *tool_name, const gchar *args_json_str)
{
    GError *error = NULL;
    JsonParser *parser = json_parser_new ();
    
    if (!json_parser_load_from_data (parser, args_json_str, -1, &error)) {
        gchar *err_msg = g_strdup_printf ("Failed to parse tool arguments: %s", error->message);
        g_error_free (error);
        g_object_unref (parser);
        return err_msg;
    }

    JsonNode *root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
        g_object_unref (parser);
        return g_strdup ("Error: arguments must be a JSON object");
    }

    JsonObject *args = json_node_get_object (root);
    gchar *result = NULL;

    if (g_strcmp0 (tool_name, "view_file") == 0) {
        result = execute_view_file (args);
    } else if (g_strcmp0 (tool_name, "edit_file") == 0) {
        result = execute_edit_file (args);
    } else if (g_strcmp0 (tool_name, "list_dir") == 0) {
        result = execute_list_dir (args);
    } else {
        result = g_strdup_printf ("Error: unknown tool '%s'", tool_name);
    }

    g_object_unref (parser);
    return result;
}
