/* ai_panel.c — AI Agent Chat Interface */

#include "ai_panel.h"
#include "ai_settings.h"

typedef struct {
    AetherIdeWindow *window;
    GtkWidget *list_box;
    GtkWidget *input_view;
} AiPanelData;

static void
on_settings_clicked (GtkButton *button, gpointer user_data)
{
    AiPanelData *data = (AiPanelData *) user_data;
    ai_settings_show_dialog (GTK_WINDOW (data->window));
}

static void
add_message_to_chat (AiPanelData *data, const gchar *role, const gchar *text)
{
    GtkWidget *row = gtk_list_box_row_new ();
    gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
    gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (row), FALSE);

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top (box, 10);
    gtk_widget_set_margin_bottom (box, 10);
    gtk_widget_set_margin_start (box, 10);
    gtk_widget_set_margin_end (box, 10);

    // Role Label
    gchar *markup = g_strdup_printf ("<b>%s</b>", role);
    GtkWidget *role_label = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (role_label), markup);
    gtk_widget_set_halign (role_label, GTK_ALIGN_START);
    g_free (markup);

    // Text Label (will be updated to GtkSourceView later for code)
    GtkWidget *text_label = gtk_label_new (text);
    gtk_label_set_line_wrap (GTK_LABEL (text_label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (text_label), 0.0);
    gtk_label_set_selectable (GTK_LABEL (text_label), TRUE);

    gtk_box_pack_start (GTK_BOX (box), role_label, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), text_label, FALSE, FALSE, 0);

    gtk_container_add (GTK_CONTAINER (row), box);
    gtk_list_box_insert (GTK_LIST_BOX (data->list_box), row, -1);
    gtk_widget_show_all (row);
    
    // Scroll to bottom
    GtkWidget *scroll = gtk_widget_get_parent (gtk_widget_get_parent (data->list_box));
    if (GTK_IS_SCROLLED_WINDOW (scroll)) {
        GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
        gtk_adjustment_set_value (vadjustment, gtk_adjustment_get_upper (vadjustment));
    }
}

#include "ai_agent.h"

static void
on_agent_response (const gchar *response_text, const gchar *error_msg, gpointer user_data)
{
    AiPanelData *data = (AiPanelData *) user_data;
    if (error_msg) {
        gchar *err = g_strdup_printf ("Error: %s", error_msg);
        add_message_to_chat (data, "AI Agent", err);
        g_free (err);
    } else {
        add_message_to_chat (data, "AI Agent", response_text);
    }
}

static void
on_submit_clicked (GtkButton *button, gpointer user_data)
{
    AiPanelData *data = (AiPanelData *) user_data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->input_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

    if (text && strlen (g_strstrip (text)) > 0) {
        add_message_to_chat (data, "User", text);
        // Clear input
        gtk_text_buffer_set_text (buffer, "", -1);
        
        ai_agent_send_prompt (text, on_agent_response, data);
    }
    
    g_free (text);
}

static void
free_panel_data (gpointer user_data)
{
    g_free (user_data);
}

GtkWidget *
ai_panel_new (AetherIdeWindow *window)
{
    AiPanelData *data = g_new0 (AiPanelData, 1);
    data->window = window;

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data_full (G_OBJECT (vbox), "panel_data", data, free_panel_data);

    // Header
    GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top (header, 5);
    gtk_widget_set_margin_bottom (header, 5);
    gtk_widget_set_margin_start (header, 10);
    gtk_widget_set_margin_end (header, 10);

    GtkWidget *title = gtk_label_new ("<b>AI Agent</b>");
    gtk_label_set_use_markup (GTK_LABEL (title), TRUE);
    gtk_widget_set_halign (title, GTK_ALIGN_START);
    
    GtkWidget *settings_btn = gtk_button_new_from_icon_name ("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON (settings_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (settings_btn, "AI Settings");
    g_signal_connect (settings_btn, "clicked", G_CALLBACK (on_settings_clicked), data);

    gtk_box_pack_start (GTK_BOX (header), title, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (header), settings_btn, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), header, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start (GTK_BOX (vbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // Chat History
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    data->list_box = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (data->list_box), GTK_SELECTION_NONE);
    gtk_container_add (GTK_CONTAINER (scroll), data->list_box);
    gtk_box_pack_start (GTK_BOX (vbox), scroll, TRUE, TRUE, 0);

    // Separator
    gtk_box_pack_start (GTK_BOX (vbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // Input Area
    GtkWidget *input_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top (input_box, 10);
    gtk_widget_set_margin_bottom (input_box, 10);
    gtk_widget_set_margin_start (input_box, 10);
    gtk_widget_set_margin_end (input_box, 10);

    GtkWidget *input_scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (input_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (input_scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request (input_scroll, -1, 80);

    data->input_view = gtk_text_view_new ();
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (data->input_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add (GTK_CONTAINER (input_scroll), data->input_view);

    GtkWidget *submit_btn = gtk_button_new_with_label ("Send");
    gtk_widget_set_halign (submit_btn, GTK_ALIGN_END);
    g_signal_connect (submit_btn, "clicked", G_CALLBACK (on_submit_clicked), data);

    gtk_box_pack_start (GTK_BOX (input_box), input_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (input_box), submit_btn, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (vbox), input_box, FALSE, FALSE, 0);

    gtk_widget_show_all (vbox);
    return vbox;
}
