/* editor_tab.c — Editor tab creation, open, and save operations */

#include "editor_tab.h"
#include "vcodex_window_private.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static GtkWidget *
get_notebook (AetherIdeWindow *self)
{
    return aether_ide_window_get_notebook (self);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
on_tab_close_clicked (GtkButton *button, GtkWidget *scroll)
{
    GtkWidget *notebook = gtk_widget_get_parent (scroll);
    if (GTK_IS_NOTEBOOK (notebook)) {
        gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), scroll);
        gtk_notebook_remove_page (GTK_NOTEBOOK (notebook), page_num);
    }
}

void
create_editor_tab (AetherIdeWindow *self,
                   const gchar     *title,
                   const gchar     *filepath,
                   const gchar     *content)
{
    GtkWidget *notebook = get_notebook (self);

    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkSourceBuffer *buffer = gtk_source_buffer_new (NULL);

    if (content) {
        gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), content, -1);
    }

    if (filepath) {
        GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
        gboolean result_uncertain;
        gchar *content_type = g_content_type_guess (filepath, NULL, 0, &result_uncertain);
        GtkSourceLanguage *lang =
            gtk_source_language_manager_guess_language (lm, filepath, content_type);
        if (lang) {
            gtk_source_buffer_set_language (buffer, lang);
        }
        g_free (content_type);
    }

    GtkWidget *source_view = gtk_source_view_new_with_buffer (buffer);
    gtk_source_view_set_background_pattern (GTK_SOURCE_VIEW (source_view),
                                            GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);

    if (filepath) {
        g_object_set_data_full (G_OBJECT (source_view), "filepath",
                                g_strdup (filepath), g_free);
    }

    gtk_source_view_set_show_line_numbers     (GTK_SOURCE_VIEW (source_view), TRUE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW (source_view), FALSE);
    gtk_source_view_set_auto_indent           (GTK_SOURCE_VIEW (source_view), TRUE);

    gtk_container_add (GTK_CONTAINER (scroll), source_view);
    gtk_widget_show_all (scroll);

    /* --- Build tab label widget --- */
    GtkWidget *tab_box   = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *tab_label = gtk_label_new (title);
    GtkWidget *close_btn = gtk_button_new_from_icon_name ("window-close-symbolic",
                                                          GTK_ICON_SIZE_MENU);

    gtk_widget_set_focus_on_click (close_btn, FALSE);
    GtkStyleContext *ctx = gtk_widget_get_style_context (close_btn);
    gtk_style_context_add_class (ctx, "flat");
    gtk_style_context_add_class (ctx, "circular");

    gtk_box_pack_start (GTK_BOX (tab_box), tab_label, TRUE,  TRUE,  0);
    gtk_box_pack_start (GTK_BOX (tab_box), close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all (tab_box);

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scroll, tab_box);
    g_signal_connect (close_btn, "clicked",
                      G_CALLBACK (on_tab_close_clicked), scroll);

    gint num_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));
    gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), num_pages - 1);
}

void
on_open_clicked (GtkButton *button, AetherIdeWindow *self)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new ("Open File",
                                     GTK_WINDOW (self),
                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                     "_Open",   GTK_RESPONSE_ACCEPT,
                                     NULL);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char   *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        gchar  *content  = NULL;
        if (g_file_get_contents (filename, &content, NULL, NULL)) {
            gchar *basename = g_path_get_basename (filename);
            create_editor_tab (self, basename, filename, content);
            g_free (basename);
            g_free (content);
        }
        g_free (filename);
    }
    gtk_widget_destroy (dialog);
}

void
on_save_clicked (GtkButton *button, AetherIdeWindow *self)
{
    GtkWidget *notebook = get_notebook (self);
    gint current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook));
    if (current_page < 0) return;

    GtkWidget *scroll      = gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), current_page);
    GtkWidget *source_view = gtk_bin_get_child (GTK_BIN (scroll));

    const gchar    *filepath = g_object_get_data (G_OBJECT (source_view), "filepath");
    GtkTextBuffer  *buf      = gtk_text_view_get_buffer (GTK_TEXT_VIEW (source_view));

    if (filepath) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds (buf, &start, &end);
        gchar *text = gtk_text_buffer_get_text (buf, &start, &end, FALSE);
        g_file_set_contents (filepath, text, -1, NULL);
        g_free (text);
    }
}
