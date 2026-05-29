#include "git_gutter.h"
#include "git_manager.h"

/* ------------------------------------------------------------------------- */
/* AetherGitGutterRenderer: custom renderer for drawing git status blocks    */
/* ------------------------------------------------------------------------- */

#define AETHER_TYPE_GIT_GUTTER_RENDERER (aether_git_gutter_renderer_get_type ())
G_DECLARE_FINAL_TYPE (AetherGitGutterRenderer, aether_git_gutter_renderer, AETHER, GIT_GUTTER_RENDERER, GtkSourceGutterRenderer)

struct _AetherGitGutterRenderer {
    GtkSourceGutterRenderer parent_instance;
    GHashTable *hunks; /* gint line_number -> GitHunkKind */
    gchar *filepath;
    guint debounce_id;
};

G_DEFINE_TYPE (AetherGitGutterRenderer, aether_git_gutter_renderer, GTK_SOURCE_TYPE_GUTTER_RENDERER)

static void
aether_git_gutter_renderer_draw (GtkSourceGutterRenderer      *renderer,
                                 cairo_t                      *cr,
                                 GdkRectangle                 *background_area,
                                 GdkRectangle                 *cell_area,
                                 GtkTextIter                  *start,
                                 GtkTextIter                  *end,
                                 GtkSourceGutterRendererState  state)
{
    AetherGitGutterRenderer *self = AETHER_GIT_GUTTER_RENDERER (renderer);
    
    if (!self->hunks) return;
    
    gint line = gtk_text_iter_get_line (start) + 1; /* 1-indexed */
    
    gpointer kind_ptr = g_hash_table_lookup (self->hunks, GINT_TO_POINTER (line));
    if (!kind_ptr) return; /* No change on this line */
    
    GitHunkKind kind = (GitHunkKind) GPOINTER_TO_INT (kind_ptr);
    
    /* Draw indicator */
    gdouble width = 3.0;
    
    if (kind == GIT_HUNK_ADDED) {
        cairo_set_source_rgba (cr, 72.0/255.0, 187.0/255.0, 120.0/255.0, 0.8); /* Green */
        cairo_rectangle (cr, cell_area->x, cell_area->y, width, cell_area->height);
        cairo_fill (cr);
    } else if (kind == GIT_HUNK_MODIFIED) {
        cairo_set_source_rgba (cr, 246.0/255.0, 173.0/255.0, 85.0/255.0, 0.8); /* Yellow */
        cairo_rectangle (cr, cell_area->x, cell_area->y, width, cell_area->height);
        cairo_fill (cr);
    } else if (kind == GIT_HUNK_REMOVED) {
        /* Draw a small red triangle at the bottom left of the cell */
        cairo_set_source_rgba (cr, 252.0/255.0, 129.0/255.0, 74.0/255.0, 0.8); /* Red */
        cairo_move_to (cr, cell_area->x, cell_area->y + cell_area->height);
        cairo_line_to (cr, cell_area->x + width * 2, cell_area->y + cell_area->height);
        cairo_line_to (cr, cell_area->x, cell_area->y + cell_area->height - width * 2);
        cairo_close_path (cr);
        cairo_fill (cr);
    }
}

static void
aether_git_gutter_renderer_dispose (GObject *object)
{
    AetherGitGutterRenderer *self = AETHER_GIT_GUTTER_RENDERER (object);
    if (self->debounce_id > 0) {
        g_source_remove (self->debounce_id);
        self->debounce_id = 0;
    }
    if (self->hunks) {
        g_hash_table_destroy (self->hunks);
        self->hunks = NULL;
    }
    g_free (self->filepath);
    G_OBJECT_CLASS (aether_git_gutter_renderer_parent_class)->dispose (object);
}

static void
aether_git_gutter_renderer_class_init (AetherGitGutterRendererClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkSourceGutterRendererClass *renderer_class = GTK_SOURCE_GUTTER_RENDERER_CLASS (klass);
    
    object_class->dispose = aether_git_gutter_renderer_dispose;
    renderer_class->draw = aether_git_gutter_renderer_draw;
}

static void
aether_git_gutter_renderer_init (AetherGitGutterRenderer *self)
{
    self->hunks = g_hash_table_new (g_direct_hash, g_direct_equal);
    gtk_source_gutter_renderer_set_size (GTK_SOURCE_GUTTER_RENDERER (self), 4);
    gtk_source_gutter_renderer_set_visible (GTK_SOURCE_GUTTER_RENDERER (self), TRUE);
}

/* ------------------------------------------------------------------------- */
/* Gutter Update Logic                                                         */
/* ------------------------------------------------------------------------- */

static gboolean
update_hunks_cb (gpointer user_data)
{
    AetherGitGutterRenderer *self = AETHER_GIT_GUTTER_RENDERER (user_data);
    self->debounce_id = 0;
    
    g_hash_table_remove_all (self->hunks);
    
    if (self->filepath) {
        GPtrArray *diff_hunks = git_manager_diff_file (self->filepath);
        if (diff_hunks) {
            for (guint i = 0; i < diff_hunks->len; i++) {
                GitHunk *h = g_ptr_array_index (diff_hunks, i);
                
                /* Mark lines in the hash table */
                for (gint j = 0; j < h->new_lines; j++) {
                    gint line = h->new_start + j;
                    g_hash_table_insert (self->hunks, GINT_TO_POINTER (line), GINT_TO_POINTER (h->kind));
                }
                
                if (h->new_lines == 0 && h->kind == GIT_HUNK_REMOVED) {
                     g_hash_table_insert (self->hunks, GINT_TO_POINTER (h->new_start), GINT_TO_POINTER (GIT_HUNK_REMOVED));
                }
            }
            g_ptr_array_free (diff_hunks, TRUE);
        }
    }
    
    gtk_source_gutter_renderer_queue_draw (GTK_SOURCE_GUTTER_RENDERER (self));
    return G_SOURCE_REMOVE;
}

static void
on_buffer_changed (GtkTextBuffer *buffer, gpointer user_data)
{
    AetherGitGutterRenderer *self = AETHER_GIT_GUTTER_RENDERER (user_data);
    if (self->debounce_id > 0) {
        g_source_remove (self->debounce_id);
    }
    /* Debounce 300ms */
    self->debounce_id = g_timeout_add (300, update_hunks_cb, self);
}

void
git_gutter_attach (GtkSourceView *view, const gchar *filepath)
{
    if (!view || !filepath) return;
    
    GtkSourceGutter *gutter = gtk_source_view_get_gutter (view, GTK_TEXT_WINDOW_LEFT);
    AetherGitGutterRenderer *renderer = g_object_new (AETHER_TYPE_GIT_GUTTER_RENDERER, NULL);
    renderer->filepath = g_strdup (filepath);
    
    gtk_source_gutter_insert (gutter, GTK_SOURCE_GUTTER_RENDERER (renderer), 10);
    
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    g_signal_connect (buffer, "changed", G_CALLBACK(on_buffer_changed), renderer);
    
    /* Initial update */
    update_hunks_cb (renderer);
}
