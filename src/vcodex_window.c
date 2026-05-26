/*
 * vcodex_window.c
 *
 * Main window: struct definition, GObject glue, accessor implementations,
 * UI layout construction, and CSS theming.
 *
 * Heavy logic lives in the sub-modules:
 *   editor_tab.c   — tab creation / open / save
 *   file_explorer.c — tree-view population and navigation
 *   search_panel.c — in-project text search
 *   terminal_panel.c — VTE terminal widget
 */

#include "vcodex_window.h"
#include "vcodex_window_private.h"
#include "editor_tab.h"
#include "file_explorer.h"
#include "search_panel.h"
#include "terminal_panel.h"

/* ------------------------------------------------------------------ */
/* Struct definition                                                    */
/* ------------------------------------------------------------------ */

struct _AetherIdeWindow {
    GtkApplicationWindow parent_instance;

    GtkWidget *main_paned;
    GtkWidget *editor_paned;
    GtkWidget *notebook;
    GtkWidget *bottom_panel;

    GtkWidget *sidebar_wrapper;
    GtkWidget *activity_bar;
    GtkWidget *sidebar_stack;
    GtkWidget *explorer_page;
    GtkWidget *search_page;

    GtkWidget *command_popover;
    GtkWidget *command_entry;
    GtkWidget *sidebar_search_entry;

    GtkWidget    *tree_view;
    GtkWidget    *search_results_view;
    GtkTreeStore *tree_store;
    GtkTreeStore *search_store;

    gchar *current_workspace_dir;
};

G_DEFINE_TYPE (AetherIdeWindow, vcodex_window, GTK_TYPE_APPLICATION_WINDOW)

/* ------------------------------------------------------------------ */
/* Private accessors (used by sub-modules via vcodex_window_private.h) */
/* ------------------------------------------------------------------ */

GtkWidget *
aether_ide_window_get_notebook (AetherIdeWindow *self)
{
    return self->notebook;
}

GtkTreeStore *
aether_ide_window_get_tree_store (AetherIdeWindow *self)
{
    return self->tree_store;
}

GtkWidget *
aether_ide_window_get_tree_view (AetherIdeWindow *self)
{
    return self->tree_view;
}

GtkTreeStore *
aether_ide_window_get_search_store (AetherIdeWindow *self)
{
    return self->search_store;
}

GtkWidget *
aether_ide_window_get_search_view (AetherIdeWindow *self)
{
    return self->search_results_view;
}

const gchar *
aether_ide_window_get_workspace_dir (AetherIdeWindow *self)
{
    return self->current_workspace_dir;
}

void
aether_ide_window_set_workspace_dir (AetherIdeWindow *self, const gchar *dir)
{
    g_free (self->current_workspace_dir);
    self->current_workspace_dir = g_strdup (dir);
}

/* ------------------------------------------------------------------ */
/* CSS / transparency theme                                             */
/* ------------------------------------------------------------------ */

static void
apply_transparent_theme (GtkWidget *widget)
{
    gtk_widget_set_app_paintable (widget, TRUE);

    GdkScreen *screen = gtk_widget_get_screen (widget);
    GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
    if (visual && gdk_screen_is_composited (screen))
        gtk_widget_set_visual (widget, visual);

    static const gchar css[] =
        "window.background { background-color: rgba(0,0,0,0); }"
        "paned             { background-color: rgba(0,0,0,0); }"
        "notebook          { background-color: rgba(0,0,0,0); border: none; }"
        "notebook tab      { background-color: rgba(0,0,0,0); border: none; }"
        "notebook stack    { background-color: rgba(0,0,0,0); }"
        "scrolledwindow, viewport { background-color: rgba(0,0,0,0); border: none; }"
        "textview, textview text, textview.view,"
        "textview border, textview border.left"
        "  { background-color: rgba(0,0,0,0); background: none; }"
        "treeview, treeview.view { background-color: rgba(0,0,0,0); }"
        "treeview:selected { background-color: rgba(0,255,255,0.3); color: white; }"
        "activitybar       { background-color: rgba(0,0,0,0);"
        "                    border-right: 1px solid rgba(255,255,255,0.1); }"
        "activitybar radio { padding: 10px; border-radius: 0;"
        "                    background-color: rgba(0,0,0,0); }"
        "activitybar radio:checked { border-left: 2px solid cyan; }"
        "headerbar         { background-color: rgba(0,0,0,0); border: none; }";

    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen (screen,
                                               GTK_STYLE_PROVIDER (provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref (provider);
}

/* ------------------------------------------------------------------ */
/* Activity-bar toggle                                                  */
/* ------------------------------------------------------------------ */

static void
on_activity_btn_toggled (GtkToggleButton *button, GtkWidget *page)
{
    if (!gtk_toggle_button_get_active (button)) return;
    GtkWidget *stack = gtk_widget_get_parent (page);
    if (GTK_IS_STACK (stack))
        gtk_stack_set_visible_child (GTK_STACK (stack), page);
}

/* ------------------------------------------------------------------ */
/* GObject class init                                                   */
/* ------------------------------------------------------------------ */

static void
vcodex_window_class_init (AetherIdeWindowClass *klass)
{
    /* Nothing to override for now */
}

/* ------------------------------------------------------------------ */
/* Window init — assembles the top-level layout                         */
/* ------------------------------------------------------------------ */

static void
vcodex_window_init (AetherIdeWindow *self)
{
    apply_transparent_theme (GTK_WIDGET (self));
    gtk_window_set_default_size (GTK_WINDOW (self), 1000, 700);
    gtk_window_set_title (GTK_WINDOW (self), "AetherIDE");

    /* ---- Header bar ---- */
    GtkWidget *header_bar = gtk_header_bar_new ();
    gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header_bar), TRUE);
    gtk_header_bar_set_title (GTK_HEADER_BAR (header_bar), "AetherIDE");

    GtkWidget *open_btn        = gtk_button_new_with_label ("Open");
    GtkWidget *open_folder_btn = gtk_button_new_with_label ("Open Folder");
    GtkWidget *save_btn        = gtk_button_new_with_label ("Save");
    GtkWidget *cmd_btn         = gtk_button_new_with_label ("Command Palette");

    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), open_btn);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), open_folder_btn);
    gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), save_btn);
    gtk_header_bar_pack_end   (GTK_HEADER_BAR (header_bar), cmd_btn);

    g_signal_connect (open_btn,        "clicked", G_CALLBACK (on_open_clicked),        self);
    g_signal_connect (open_folder_btn, "clicked", G_CALLBACK (on_open_folder_clicked), self);
    g_signal_connect (save_btn,        "clicked", G_CALLBACK (on_save_clicked),        self);

    /* Command palette popover */
    self->command_popover = gtk_popover_new (cmd_btn);
    GtkWidget *popover_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width (GTK_CONTAINER (popover_box), 10);

    self->command_entry = gtk_search_entry_new ();
    gtk_widget_set_size_request (self->command_entry, 300, -1);
    gtk_box_pack_start (GTK_BOX (popover_box), self->command_entry, FALSE, FALSE, 0);

    GtkWidget *cmd_list   = gtk_list_box_new ();
    GtkWidget *dummy_item = gtk_label_new ("> Run Build Task");
    gtk_list_box_insert (GTK_LIST_BOX (cmd_list), dummy_item, -1);
    gtk_box_pack_start (GTK_BOX (popover_box), cmd_list, TRUE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (self->command_popover), popover_box);
    gtk_widget_show_all (popover_box);
    g_signal_connect_swapped (cmd_btn, "clicked",
                              G_CALLBACK (gtk_widget_show), self->command_popover);

    gtk_window_set_titlebar (GTK_WINDOW (self), header_bar);

    /* ---- Main horizontal pane ---- */
    self->main_paned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add (GTK_CONTAINER (self), self->main_paned);

    /* ---- Sidebar: activity bar + stack ---- */
    self->sidebar_wrapper = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request (self->sidebar_wrapper, 250, -1);
    gtk_paned_pack1 (GTK_PANED (self->main_paned), self->sidebar_wrapper, FALSE, FALSE);

    self->activity_bar = gtk_box_new (GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_top (self->activity_bar, 10);
    gtk_style_context_add_class (gtk_widget_get_style_context (self->activity_bar), "activitybar");
    gtk_box_pack_start (GTK_BOX (self->sidebar_wrapper), self->activity_bar, FALSE, FALSE, 0);

    self->sidebar_stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->sidebar_stack),
                                   GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_pack_start (GTK_BOX (self->sidebar_wrapper), self->sidebar_stack, TRUE, TRUE, 0);

    /* Explorer page */
    self->explorer_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *explorer_label = gtk_label_new ("Explorer");
    gtk_widget_set_margin_top    (explorer_label, 10);
    gtk_widget_set_margin_bottom (explorer_label, 10);
    gtk_box_pack_start (GTK_BOX (self->explorer_page), explorer_label, FALSE, FALSE, 0);

    /* Search page */
    self->search_page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *search_label = gtk_label_new ("Search");
    gtk_widget_set_margin_top    (search_label, 10);
    gtk_widget_set_margin_bottom (search_label, 10);
    gtk_box_pack_start (GTK_BOX (self->search_page), search_label, FALSE, FALSE, 0);

    self->sidebar_search_entry = gtk_search_entry_new ();
    gtk_widget_set_margin_start  (self->sidebar_search_entry, 5);
    gtk_widget_set_margin_end    (self->sidebar_search_entry, 5);
    gtk_widget_set_margin_bottom (self->sidebar_search_entry, 10);
    gtk_box_pack_start (GTK_BOX (self->search_page),
                        self->sidebar_search_entry, FALSE, FALSE, 0);

    /* Activity bar buttons */
    GtkWidget *explorer_btn = gtk_radio_button_new (NULL);
    gtk_button_set_image (GTK_BUTTON (explorer_btn),
        gtk_image_new_from_icon_name ("folder-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (explorer_btn), FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (explorer_btn), "flat");

    GtkWidget *search_btn = gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON (explorer_btn));
    gtk_button_set_image (GTK_BUTTON (search_btn),
        gtk_image_new_from_icon_name ("system-search-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (search_btn), FALSE);
    gtk_style_context_add_class (gtk_widget_get_style_context (search_btn), "flat");

    gtk_box_pack_start (GTK_BOX (self->activity_bar), explorer_btn, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (self->activity_bar), search_btn,   FALSE, FALSE, 0);

    g_signal_connect (explorer_btn, "toggled", G_CALLBACK (on_activity_btn_toggled), self->explorer_page);
    g_signal_connect (search_btn,   "toggled", G_CALLBACK (on_activity_btn_toggled), self->search_page);

    gtk_stack_add_named (GTK_STACK (self->sidebar_stack), self->explorer_page, "explorer");
    gtk_stack_add_named (GTK_STACK (self->sidebar_stack), self->search_page,   "search");

    /* File-explorer tree */
    self->tree_store = gtk_tree_store_new (NUM_COLUMNS,
                                           G_TYPE_ICON, G_TYPE_STRING,
                                           G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (self->tree_store),
                                     COLUMN_NAME, sort_tree_func, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self->tree_store),
                                          COLUMN_NAME, GTK_SORT_ASCENDING);

    self->tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->tree_store));
    g_object_unref (self->tree_store);
    gtk_tree_view_set_activate_on_single_click (GTK_TREE_VIEW (self->tree_view), TRUE);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->tree_view), FALSE);

    GtkTreeViewColumn *col       = gtk_tree_view_column_new ();
    GtkCellRenderer   *icon_rend = gtk_cell_renderer_pixbuf_new ();
    GtkCellRenderer   *text_rend = gtk_cell_renderer_text_new ();
    g_object_set (icon_rend, "stock-size", GTK_ICON_SIZE_MENU, NULL);
    gtk_tree_view_column_pack_start (col, icon_rend, FALSE);
    gtk_tree_view_column_add_attribute (col, icon_rend, "gicon", COLUMN_ICON);
    gtk_tree_view_column_pack_start (col, text_rend, TRUE);
    gtk_tree_view_column_add_attribute (col, text_rend, "text", COLUMN_NAME);
    gtk_tree_view_append_column (GTK_TREE_VIEW (self->tree_view), col);

    g_signal_connect (self->tree_view, "row-activated",
                      G_CALLBACK (on_tree_row_activated), self);

    GtkWidget *tree_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (tree_scroll), self->tree_view);
    gtk_box_pack_start (GTK_BOX (self->explorer_page), tree_scroll, TRUE, TRUE, 0);

    /* Search results tree */
    self->search_store = gtk_tree_store_new (NUM_SEARCH_COLUMNS,
                                             G_TYPE_ICON, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_INT);
    self->search_results_view =
        gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->search_store));
    g_object_unref (self->search_store);
    gtk_tree_view_set_activate_on_single_click (
        GTK_TREE_VIEW (self->search_results_view), TRUE);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->search_results_view), FALSE);

    GtkTreeViewColumn *scol       = gtk_tree_view_column_new ();
    GtkCellRenderer   *sicon_rend = gtk_cell_renderer_pixbuf_new ();
    GtkCellRenderer   *stext_rend = gtk_cell_renderer_text_new ();
    g_object_set (sicon_rend, "stock-size", GTK_ICON_SIZE_MENU, NULL);
    gtk_tree_view_column_pack_start (scol, sicon_rend, FALSE);
    gtk_tree_view_column_add_attribute (scol, sicon_rend, "gicon", SEARCH_COL_ICON);
    gtk_tree_view_column_pack_start (scol, stext_rend, TRUE);
    gtk_tree_view_column_add_attribute (scol, stext_rend, "text", SEARCH_COL_TEXT);
    gtk_tree_view_append_column (GTK_TREE_VIEW (self->search_results_view), scol);

    g_signal_connect (self->search_results_view, "row-activated",
                      G_CALLBACK (on_search_row_activated), self);
    g_signal_connect (self->sidebar_search_entry, "search-changed",
                      G_CALLBACK (on_sidebar_search_changed), self);

    GtkWidget *search_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (search_scroll), self->search_results_view);
    gtk_box_pack_start (GTK_BOX (self->search_page), search_scroll, TRUE, TRUE, 0);

    /* ---- Editor pane (right side) ---- */
    self->editor_paned = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack2 (GTK_PANED (self->main_paned), self->editor_paned, TRUE, FALSE);

    self->notebook = gtk_notebook_new ();
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (self->notebook), TRUE);
    gtk_paned_pack1 (GTK_PANED (self->editor_paned), self->notebook, TRUE, FALSE);

    g_signal_connect (self->notebook, "switch-page",
                      G_CALLBACK (on_notebook_switch_page), self);

    create_editor_tab (self, "Untitled-1", NULL, NULL);

    /* ---- Bottom panel (terminal) ---- */
    self->bottom_panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request (self->bottom_panel, -1, 150);

    GtkWidget *term_scroll = terminal_panel_new ();
    gtk_box_pack_start (GTK_BOX (self->bottom_panel), term_scroll, TRUE, TRUE, 0);

    gtk_paned_pack2 (GTK_PANED (self->editor_paned), self->bottom_panel, FALSE, FALSE);

    gtk_widget_show_all (GTK_WIDGET (self));
}

/* ------------------------------------------------------------------ */
/* Public constructor                                                   */
/* ------------------------------------------------------------------ */

AetherIdeWindow *
vcodex_window_new (GtkApplication *app)
{
    return g_object_new (AETHER_TYPE_IDE_WINDOW, "application", app, NULL);
}
