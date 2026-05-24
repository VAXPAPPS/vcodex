#include "aether_ide_window.h"
#include <vte/vte.h>

struct _AetherIdeWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *main_paned;
    GtkWidget *sidebar_box;
    GtkWidget *editor_paned;
    
    GtkWidget *notebook;
    GtkWidget *bottom_panel;
    GtkWidget *terminal;
    
    GtkWidget *command_popover;
    GtkWidget *command_entry;
    
    GtkWidget *tree_view;
    GtkTreeStore *tree_store;
};

enum {
    COLUMN_NAME,
    COLUMN_PATH,
    COLUMN_IS_DIR,
    NUM_COLUMNS
};

G_DEFINE_TYPE (AetherIdeWindow, aether_ide_window, GTK_TYPE_APPLICATION_WINDOW)

static void
create_editor_tab (AetherIdeWindow *self, const gchar *title, const gchar *filepath, const gchar *content)
{
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    GtkSourceBuffer *buffer = gtk_source_buffer_new (NULL);
    if (content) {
        gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), content, -1);
    }
    
    if (filepath) {
        GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();
        gboolean result_uncertain;
        gchar *content_type = g_content_type_guess (filepath, NULL, 0, &result_uncertain);
        GtkSourceLanguage *lang = gtk_source_language_manager_guess_language (lm, filepath, content_type);
        if (lang) {
            gtk_source_buffer_set_language (buffer, lang);
        }
        g_free (content_type);
    }

    // GtkSourceStyleSchemeManager *sm = gtk_source_style_scheme_manager_get_default ();
    // GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme (sm, "classic");
    // if (scheme) {
    //     gtk_source_buffer_set_style_scheme (buffer, scheme);
    // }

    GtkWidget *source_view = gtk_source_view_new_with_buffer (buffer);
    gtk_source_view_set_background_pattern (GTK_SOURCE_VIEW (source_view), GTK_SOURCE_BACKGROUND_PATTERN_TYPE_NONE);
    
    if (filepath) {
        g_object_set_data_full (G_OBJECT (source_view), "filepath", g_strdup (filepath), g_free);
    }

    gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (source_view), TRUE);
    gtk_source_view_set_highlight_current_line (GTK_SOURCE_VIEW (source_view), FALSE);
    gtk_source_view_set_auto_indent (GTK_SOURCE_VIEW (source_view), TRUE);
    
    gtk_container_add (GTK_CONTAINER (scroll), source_view);
    gtk_widget_show_all (scroll);
    
    GtkWidget *tab_label = gtk_label_new (title);
    gtk_notebook_append_page (GTK_NOTEBOOK (self->notebook), scroll, tab_label);
    
    gint num_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (self->notebook));
    gtk_notebook_set_current_page (GTK_NOTEBOOK (self->notebook), num_pages - 1);
}

static void
on_open_clicked (GtkButton *button, AetherIdeWindow *self)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open File",
                                      GTK_WINDOW (self),
                                      GTK_FILE_CHOOSER_ACTION_OPEN,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Open", GTK_RESPONSE_ACCEPT,
                                      NULL);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        gchar *content = NULL;
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

static void
on_save_clicked (GtkButton *button, AetherIdeWindow *self)
{
    gint current_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (self->notebook));
    if (current_page < 0) return;

    GtkWidget *scroll = gtk_notebook_get_nth_page (GTK_NOTEBOOK (self->notebook), current_page);
    GtkWidget *source_view = gtk_bin_get_child (GTK_BIN (scroll));
    
    const gchar *filepath = g_object_get_data (G_OBJECT (source_view), "filepath");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (source_view));
    
    if (filepath) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        gchar *content = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        g_file_set_contents (filepath, content, -1, NULL);
        g_free (content);
    }
}

static void
populate_tree_model (GtkTreeStore *store, GtkTreeIter *parent, const gchar *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    if (!dir) return;

    const gchar *name;
    while ((name = g_dir_read_name (dir)) != NULL) {
        if (name[0] == '.') continue;

        gchar *full_path = g_build_filename (path, name, NULL);
        gboolean is_dir = g_file_test (full_path, G_FILE_TEST_IS_DIR);
        GtkTreeIter iter;
        gtk_tree_store_append (store, &iter, parent);
        gtk_tree_store_set (store, &iter, COLUMN_NAME, name, COLUMN_PATH, full_path, COLUMN_IS_DIR, is_dir, -1);

        if (is_dir) {
            populate_tree_model (store, &iter, full_path);
        }
        g_free (full_path);
    }
    g_dir_close (dir);
}

static void
on_tree_row_activated (GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, AetherIdeWindow *self)
{
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
    if (gtk_tree_model_get_iter (model, &iter, path)) {
        gchar *filepath;
        gboolean is_dir;
        gtk_tree_model_get (model, &iter, COLUMN_PATH, &filepath, COLUMN_IS_DIR, &is_dir, -1);
        
        if (!is_dir && g_file_test (filepath, G_FILE_TEST_IS_REGULAR)) {
            gchar *content = NULL;
            if (g_file_get_contents (filepath, &content, NULL, NULL)) {
                gchar *basename = g_path_get_basename (filepath);
                create_editor_tab (self, basename, filepath, content);
                g_free (basename);
                g_free (content);
            }
        } else if (is_dir) {
            // Toggle folder expansion on single click
            if (gtk_tree_view_row_expanded (tree_view, path)) {
                gtk_tree_view_collapse_row (tree_view, path);
            } else {
                gtk_tree_view_expand_row (tree_view, path, FALSE);
            }
        }
        g_free (filepath);
    }
}

static void
on_open_folder_clicked (GtkButton *button, AetherIdeWindow *self)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new ("Open Folder",
                                      GTK_WINDOW (self),
                                      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                      "_Cancel", GTK_RESPONSE_CANCEL,
                                      "_Open", GTK_RESPONSE_ACCEPT,
                                      NULL);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        gtk_tree_store_clear (self->tree_store);
        populate_tree_model (self->tree_store, NULL, folder);
        g_free (folder);
    }
    gtk_widget_destroy (dialog);
}

static void
aether_ide_window_class_init (AetherIdeWindowClass *class)
{
}

static gboolean
search_tree_recursive (GtkTreeModel *model, GtkTreeIter *iter, const gchar *target, GtkTreeIter *out_iter)
{
    do {
        gchar *path;
        gtk_tree_model_get (model, iter, COLUMN_PATH, &path, -1);
        gboolean match = (g_strcmp0 (path, target) == 0);
        g_free (path);
        if (match) {
            *out_iter = *iter;
            return TRUE;
        }
        if (gtk_tree_model_iter_has_child (model, iter)) {
            GtkTreeIter child;
            gtk_tree_model_iter_children (model, &child, iter);
            if (search_tree_recursive (model, &child, target, out_iter)) {
                return TRUE;
            }
        }
    } while (gtk_tree_model_iter_next (model, iter));
    return FALSE;
}

static void
on_notebook_switch_page (GtkNotebook *notebook, GtkWidget *page, guint page_num, AetherIdeWindow *self)
{
    GtkWidget *source_view = gtk_bin_get_child (GTK_BIN (page));
    if (!source_view || !GTK_SOURCE_IS_VIEW(source_view)) return;
    
    const gchar *filepath = g_object_get_data (G_OBJECT (source_view), "filepath");
    if (!filepath) return;

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->tree_store), &iter)) {
        GtkTreeIter result;
        if (search_tree_recursive (GTK_TREE_MODEL (self->tree_store), &iter, filepath, &result)) {
            GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (self->tree_store), &result);
            gtk_tree_view_set_cursor (GTK_TREE_VIEW (self->tree_view), path, NULL, FALSE);
            gtk_tree_path_free (path);
        }
    }
}

static gint
sort_tree_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
    gboolean is_dir_a, is_dir_b;
    gchar *name_a, *name_b;
    gtk_tree_model_get (model, a, COLUMN_IS_DIR, &is_dir_a, COLUMN_NAME, &name_a, -1);
    gtk_tree_model_get (model, b, COLUMN_IS_DIR, &is_dir_b, COLUMN_NAME, &name_b, -1);

    gint ret = 0;
    if (is_dir_a && !is_dir_b) {
        ret = -1;
    } else if (!is_dir_a && is_dir_b) {
        ret = 1;
    } else {
        if (name_a && name_b) {
            ret = g_ascii_strcasecmp (name_a, name_b);
        }
    }

    g_free (name_a);
    g_free (name_b);
    return ret;
}

static void
apply_transparent_theme (GtkWidget *widget)
{
    gtk_widget_set_app_paintable (widget, TRUE);
    GdkScreen *screen = gtk_widget_get_screen (widget);
    GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
    if (visual != NULL && gdk_screen_is_composited (screen)) {
        gtk_widget_set_visual (widget, visual);
    }

    GtkCssProvider *provider = gtk_css_provider_new ();
    const gchar *css = 
        "window.background { background-color: rgba(0, 0, 0, 0.3); } "
        "paned { background-color: transparent; } "
        "notebook { background-color: transparent; } "
        "notebook tab { background-color: rgba(0, 0, 0, 0.3); } "
        "textview, textview text, textview.view, textview border { background-color: rgba(0,0,0,0); } "
        "treeview, treeview.view { background-color: rgba(0,0,0,0); } "
        "headerbar { background-color: rgba(0, 0, 0, 0); border: none; }";
    gtk_css_provider_load_from_data (provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen (screen,
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);
}

static void
aether_ide_window_init (AetherIdeWindow *self)
{
    // Apply transparent theme
    apply_transparent_theme (GTK_WIDGET (self));

    // Configure window
    gtk_window_set_default_size (GTK_WINDOW (self), 1000, 700);
    gtk_window_set_title (GTK_WINDOW (self), "AetherIDE");

    // Header bar
    GtkWidget *header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header_bar), TRUE);
    gtk_header_bar_set_title (GTK_HEADER_BAR (header_bar), "AetherIDE");
    
    GtkWidget *open_btn = gtk_button_new_with_label ("Open");
    GtkWidget *open_folder_btn = gtk_button_new_with_label ("Open Folder");
    GtkWidget *save_btn = gtk_button_new_with_label ("Save");
    GtkWidget *cmd_btn = gtk_button_new_with_label ("Command Palette");
    
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), open_btn);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), open_folder_btn);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), save_btn);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (header_bar), cmd_btn);
    
    g_signal_connect (open_btn, "clicked", G_CALLBACK (on_open_clicked), self);
    g_signal_connect (open_folder_btn, "clicked", G_CALLBACK (on_open_folder_clicked), self);
    g_signal_connect (save_btn, "clicked", G_CALLBACK (on_save_clicked), self);
    
    // Command Palette Setup
    self->command_popover = gtk_popover_new (cmd_btn);
    GtkWidget *popover_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width (GTK_CONTAINER (popover_box), 10);
    
    self->command_entry = gtk_search_entry_new ();
    gtk_widget_set_size_request (self->command_entry, 300, -1);
    gtk_box_pack_start (GTK_BOX (popover_box), self->command_entry, FALSE, FALSE, 0);
    
    GtkWidget *cmd_list = gtk_list_box_new ();
    GtkWidget *dummy_item = gtk_label_new ("> Run Build Task");
    gtk_list_box_insert (GTK_LIST_BOX (cmd_list), dummy_item, -1);
    gtk_box_pack_start (GTK_BOX (popover_box), cmd_list, TRUE, TRUE, 0);
    
    gtk_container_add (GTK_CONTAINER (self->command_popover), popover_box);
    gtk_widget_show_all (popover_box);
    
    g_signal_connect_swapped (cmd_btn, "clicked", G_CALLBACK (gtk_widget_show), self->command_popover);
    
    gtk_window_set_titlebar (GTK_WINDOW (self), header_bar);

    // Main layout
    self->main_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add (GTK_CONTAINER (self), self->main_paned);

    // Sidebar
    self->sidebar_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (self->sidebar_box, 200, -1);
    
    GtkWidget *sidebar_label = gtk_label_new ("Explorer");
    gtk_box_pack_start (GTK_BOX (self->sidebar_box), sidebar_label, FALSE, FALSE, 10);
    
    self->tree_store = gtk_tree_store_new (NUM_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (self->tree_store), COLUMN_NAME, sort_tree_func, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self->tree_store), COLUMN_NAME, GTK_SORT_ASCENDING);
    
    self->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->tree_store));
    g_object_unref (self->tree_store);
    
    gtk_tree_view_set_activate_on_single_click (GTK_TREE_VIEW (self->tree_view), TRUE);
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes ("File", renderer, "text", COLUMN_NAME, NULL);
    gtk_tree_view_append_column (GTK_TREE_VIEW (self->tree_view), column);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->tree_view), FALSE);
    
    g_signal_connect (self->tree_view, "row-activated", G_CALLBACK (on_tree_row_activated), self);
    
    GtkWidget *tree_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (tree_scroll), self->tree_view);
    gtk_box_pack_start (GTK_BOX (self->sidebar_box), tree_scroll, TRUE, TRUE, 0);
    
    gtk_paned_pack1 (GTK_PANED (self->main_paned), self->sidebar_box, FALSE, FALSE);

    // Editor Paned
    self->editor_paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack2 (GTK_PANED (self->main_paned), self->editor_paned, TRUE, FALSE);

    // Notebook (Tabs)
    self->notebook = gtk_notebook_new ();
    gtk_paned_pack1 (GTK_PANED (self->editor_paned), self->notebook, TRUE, FALSE);
    
    g_signal_connect (self->notebook, "switch-page", G_CALLBACK (on_notebook_switch_page), self);

    // Default empty view for testing
    create_editor_tab (self, "Untitled-1", NULL, NULL);

    // Bottom Panel
    self->bottom_panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (self->bottom_panel, -1, 150);
    
    self->terminal = vte_terminal_new ();
    GdkRGBA bg_color = {0.0, 0.0, 0.0, 0.0};
    vte_terminal_set_color_background (VTE_TERMINAL (self->terminal), &bg_color);
    
    gchar **envp = g_get_environ ();
    gchar **command = g_new0 (gchar *, 2);
    command[0] = g_strdup (g_environ_getenv (envp, "SHELL"));
    if (!command[0]) command[0] = g_strdup ("/bin/bash");
    
    vte_terminal_spawn_async (VTE_TERMINAL (self->terminal),
                              VTE_PTY_DEFAULT,
                              NULL, // working directory
                              command,
                              NULL, // environment
                              G_SPAWN_DEFAULT,
                              NULL, NULL,
                              NULL,
                              -1, // timeout
                              NULL, NULL, NULL);
    g_strfreev (command);
    g_strfreev (envp);

    GtkWidget *term_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (term_scroll), self->terminal);
    gtk_box_pack_start (GTK_BOX (self->bottom_panel), term_scroll, TRUE, TRUE, 0);

    gtk_paned_pack2 (GTK_PANED (self->editor_paned), self->bottom_panel, FALSE, FALSE);

    gtk_widget_show_all (GTK_WIDGET (self));
}

AetherIdeWindow *
aether_ide_window_new (GtkApplication *app)
{
    return g_object_new (AETHER_TYPE_IDE_WINDOW, "application", app, NULL);
}
