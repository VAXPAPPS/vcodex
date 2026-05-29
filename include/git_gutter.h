#ifndef GIT_GUTTER_H
#define GIT_GUTTER_H

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

/**
 * git_gutter_attach:
 * Attaches a git gutter renderer to the given GtkSourceView for the specified
 * file path. It will automatically update the gutter by polling git status on
 * buffer changes (with a debounce).
 */
void git_gutter_attach (GtkSourceView *view, const gchar *filepath);

G_END_DECLS

#endif /* GIT_GUTTER_H */
