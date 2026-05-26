#ifndef AI_PANEL_H
#define AI_PANEL_H

#include <gtk/gtk.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/*
 * Creates the AI Agent panel widget.
 * This panel includes a chat history view, an input area, and a settings button.
 */
GtkWidget *ai_panel_new (AetherIdeWindow *window);

G_END_DECLS

#endif /* AI_PANEL_H */
