/* layout_controls.c — Custom drawn layout toggle buttons */

#include "layout_controls.h"
#include <math.h>

typedef enum {
    LAYOUT_ICON_SIDEBAR,
    LAYOUT_ICON_BOTTOM,
    LAYOUT_ICON_RIGHT
} LayoutIconType;

typedef struct {
    LayoutIconType type;
    GtkWidget *target_widget;
    GtkWidget *sidebar_wrapper; // Specific to sidebar handling
} LayoutControlData;

static void
draw_rounded_rect (cairo_t *cr, double x, double y, double width, double height, double radius)
{
    cairo_new_sub_path (cr);
    cairo_arc (cr, x + width - radius, y + radius, radius, -G_PI/2, 0);
    cairo_arc (cr, x + width - radius, y + height - radius, radius, 0, G_PI/2);
    cairo_arc (cr, x + radius, y + height - radius, radius, G_PI/2, G_PI);
    cairo_arc (cr, x + radius, y + radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path (cr);
}

static gboolean
on_draw_icon (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    LayoutIconType type = GPOINTER_TO_INT (user_data);
    GtkWidget *button = gtk_widget_get_parent (widget);
    gboolean is_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    GdkRGBA color;
    gtk_style_context_get_color (context, gtk_style_context_get_state (context), &color);

    double x = 4.0, y = 4.0, w = 16.0, h = 16.0, r = 2.0;

    // Draw main outline
    cairo_set_source_rgba (cr, color.red, color.green, color.blue, color.alpha);
    cairo_set_line_width (cr, 1.5);
    draw_rounded_rect (cr, x, y, w, h, r);
    cairo_stroke (cr);

    // Draw dividing lines and fills based on type
    if (type == LAYOUT_ICON_SIDEBAR) {
        if (is_active) {
            cairo_rectangle (cr, x, y, 6, h);
            cairo_clip (cr);
            draw_rounded_rect (cr, x, y, w, h, r);
            cairo_fill (cr);
            cairo_reset_clip (cr);
        } else {
            cairo_move_to (cr, x + 6, y);
            cairo_line_to (cr, x + 6, y + h);
            cairo_stroke (cr);
        }
    } else if (type == LAYOUT_ICON_BOTTOM) {
        if (is_active) {
            cairo_rectangle (cr, x, y + 10, w, 6);
            cairo_clip (cr);
            draw_rounded_rect (cr, x, y, w, h, r);
            cairo_fill (cr);
            cairo_reset_clip (cr);
        } else {
            cairo_move_to (cr, x, y + 10);
            cairo_line_to (cr, x + w, y + 10);
            cairo_stroke (cr);
        }
    } else if (type == LAYOUT_ICON_RIGHT) {
        if (is_active) {
            cairo_rectangle (cr, x + 10, y, 6, h);
            cairo_clip (cr);
            draw_rounded_rect (cr, x, y, w, h, r);
            cairo_fill (cr);
            cairo_reset_clip (cr);
        } else {
            cairo_move_to (cr, x + 10, y);
            cairo_line_to (cr, x + 10, y + h);
            cairo_stroke (cr);
        }
    }

    return FALSE;
}

static void
on_layout_button_toggled (GtkToggleButton *button, gpointer user_data)
{
    LayoutControlData *data = (LayoutControlData *) user_data;
    gboolean is_active = gtk_toggle_button_get_active (button);

    if (data->type == LAYOUT_ICON_SIDEBAR) {
        gtk_widget_set_visible (data->target_widget, is_active);
        if (!is_active && data->sidebar_wrapper) {
            gtk_widget_set_size_request (data->sidebar_wrapper, -1, -1);
        } else if (data->sidebar_wrapper) {
            gtk_widget_set_size_request (data->sidebar_wrapper, 250, -1);
        }
    } else {
        gtk_widget_set_visible (data->target_widget, is_active);
    }

    // Force redraw of the drawing area to reflect active state
    GtkWidget *drawing_area = gtk_bin_get_child (GTK_BIN (button));
    gtk_widget_queue_draw (drawing_area);
}

static void
free_layout_data (gpointer user_data, GClosure *closure)
{
    g_free (user_data);
}

static GtkWidget *
create_layout_button (LayoutIconType type, GtkWidget *target_widget, GtkWidget *sidebar_wrapper, const gchar *tooltip)
{
    GtkWidget *button = gtk_toggle_button_new ();
    gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text (button, tooltip);

    GtkWidget *drawing_area = gtk_drawing_area_new ();
    gtk_widget_set_size_request (drawing_area, 24, 24);
    
    g_signal_connect (drawing_area, "draw", G_CALLBACK (on_draw_icon), GINT_TO_POINTER (type));
    
    gtk_container_add (GTK_CONTAINER (button), drawing_area);

    LayoutControlData *data = g_new0 (LayoutControlData, 1);
    data->type = type;
    data->target_widget = target_widget;
    data->sidebar_wrapper = sidebar_wrapper;

    g_signal_connect_data (button, "toggled", G_CALLBACK (on_layout_button_toggled),
                           data, (GClosureNotify) free_layout_data, 0);

    // Initial state
    if (target_widget) {
        gboolean is_visible = gtk_widget_get_visible (target_widget);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), is_visible);
    }

    return button;
}

GtkWidget *
layout_controls_new (GtkWidget *sidebar_wrapper,
                     GtkWidget *sidebar_stack,
                     GtkWidget *bottom_panel,
                     GtkWidget *ai_panel)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class (gtk_widget_get_style_context (box), "linked");

    GtkWidget *btn_sidebar = create_layout_button (LAYOUT_ICON_SIDEBAR, sidebar_stack, sidebar_wrapper, "Toggle Sidebar");
    GtkWidget *btn_bottom  = create_layout_button (LAYOUT_ICON_BOTTOM, bottom_panel, NULL, "Toggle Bottom Panel");
    GtkWidget *btn_right   = create_layout_button (LAYOUT_ICON_RIGHT, ai_panel, NULL, "Toggle AI Panel");

    gtk_box_pack_start (GTK_BOX (box), btn_sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), btn_bottom, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), btn_right, FALSE, FALSE, 0);

    gtk_widget_show_all (box);
    return box;
}
