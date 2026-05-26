#ifndef AETHER_LSP_CLIENT_H
#define AETHER_LSP_CLIENT_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _AetherLspClient AetherLspClient;

AetherLspClient *aether_lsp_client_new (const gchar *command);
void aether_lsp_client_free (AetherLspClient *client);

// Sends a JSON-RPC notification to the server (e.g. textDocument/didOpen)
void aether_lsp_client_send_notification (AetherLspClient *client, const gchar *method, GVariant *params);

G_END_DECLS

#endif /* AETHER_LSP_CLIENT_H */
