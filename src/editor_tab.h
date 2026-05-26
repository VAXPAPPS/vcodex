#ifndef EDITOR_TAB_H
#define EDITOR_TAB_H

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>
#include "vcodex_window.h"

G_BEGIN_DECLS

/*
 * Creates a new editor tab inside the notebook.
 * title    - The label shown on the tab.
 * filepath - Path used for syntax detection and save; may be NULL.
 * content  - Initial buffer text; may be NULL for an empty buffer.
 */
void create_editor_tab (AetherIdeWindow *self,
                        const gchar     *title,
                        const gchar     *filepath,
                        const gchar     *content);

/* Opens a file-chooser dialog and opens the selected file in a new tab. */
void on_open_clicked        (GtkButton *button, AetherIdeWindow *self);

/* Saves the currently active tab back to its filepath on disk. */
void on_save_clicked        (GtkButton *button, AetherIdeWindow *self);

/* Removes a tab when its close button is clicked. */
void on_tab_close_clicked   (GtkButton *button, GtkWidget *scroll);

G_END_DECLS

#endif /* EDITOR_TAB_H */
