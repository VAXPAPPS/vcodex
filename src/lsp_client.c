#include "lsp_client.h"
#include <string.h>

struct _AetherLspClient {
    GSubprocess *subprocess;
    GDataOutputStream *out_stream;
    GDataInputStream *in_stream;
};

AetherLspClient *
aether_lsp_client_new (const gchar *command)
{
    AetherLspClient *client = g_new0 (AetherLspClient, 1);
    
    GError *error = NULL;
    gchar **argv;
    g_shell_parse_argv (command, NULL, &argv, &error);
    
    client->subprocess = g_subprocess_newv ((const gchar * const *)argv,
                                            G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                            &error);
    g_strfreev (argv);

    if (error) {
        g_printerr ("Failed to start LSP server: %s\n", error->message);
        g_error_free (error);
        g_free (client);
        return NULL;
    }

    client->out_stream = g_data_output_stream_new (g_subprocess_get_stdin_pipe (client->subprocess));
    client->in_stream = g_data_input_stream_new (g_subprocess_get_stdout_pipe (client->subprocess));

    return client;
}

void
aether_lsp_client_free (AetherLspClient *client)
{
    if (client) {
        g_object_unref (client->out_stream);
        g_object_unref (client->in_stream);
        g_object_unref (client->subprocess);
        g_free (client);
    }
}

void
aether_lsp_client_send_notification (AetherLspClient *client, const gchar *method, GVariant *params)
{
    // Minimal JSON-RPC formatting (Mock implementation)
    // A full implementation requires encoding the params into JSON string.
    // For now, this is just a placeholder architecture to satisfy Phase 4.
    gchar *content = g_strdup_printf ("{\"jsonrpc\": \"2.0\", \"method\": \"%s\"}", method);
    gchar *message = g_strdup_printf ("Content-Length: %zu\r\n\r\n%s", strlen(content), content);
    
    g_data_output_stream_put_string (client->out_stream, message, NULL, NULL);
    
    g_free (content);
    g_free (message);
}
