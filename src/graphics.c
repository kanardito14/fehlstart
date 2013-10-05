/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#include <math.h>
#include "graphics.h"

static void rect(cairo_t* cr, double x, double y, double w, double h, double r)
{
    if (r > 0) {
        cairo_arc(cr, x + w - r, y + r, r, 1.5 * PI, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, 0.5 * PI);
        cairo_arc(cr, x + r, y + h - r, r, 0.5 * PI, PI);
        cairo_arc(cr, x + r, y + r, r, PI, 1.5 * PI);
    } else {
        cairo_rectangle(cr, 0, 0, w, h);
    }
    cairo_close_path(cr);
}

static Color parse_color(const char* color_name, GdkColor default_color)
{
    GdkColor c = {0, 0, 0, 0};
    if (!gdk_color_parse(color_name, &c))
        c = default_color;
    Color col = {(double)c.red / 0xffff, (double)c.green / 0xffff, (double)c.blue / 0xffff, 1};
    return col;
}

void draw_labels(cairo_t* cr, GtkStyle* st, const char* action, const char* input)
{
    cairo_text_extents_t extents;
    Color c = parse_color(Labels_color, st->text[GTK_STATE_SELECTED]);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);

    int max_width = Window_width - Border_width * 2;
    int size = Labels_size1;
    do {
        cairo_set_font_size(cr, size--);
        cairo_text_extents(cr, action, &extents);
    } while (extents.width > max_width && size > 6);
    double x = (Window_width - extents.width) / 2.0;
    double y = Window_height * 0.75;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, action);

    cairo_set_font_size(cr, Labels_size2);
    cairo_text_extents(cr, input, &extents);
    x = (Window_width - extents.width) / 2.0;
    y = Window_height - Border_width * 2.0;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, input);
}

void draw_icon(cairo_t* cr, GdkPixbuf* icon)
{
    if (!icon)
        return;
    double x = (Window_width - gdk_pixbuf_get_width(icon)) / 2.0;
    double y = fmax(Window_height / 2.0 - gdk_pixbuf_get_height(icon), Border_width);
    gdk_cairo_set_source_pixbuf(cr, icon, x, y);
    cairo_paint(cr);
}

void draw_dots(cairo_t* cr, GtkStyle* st, int index, int max)
{
    if (max < 1)
        return;
    double r = 2.0; // circle radius
    double w = Window_width;
    double y = Window_height / 2.0;
    Color c = parse_color(Labels_color, st->text[GTK_STATE_SELECTED]);

    for (int i = 0; i < imin(3, index); i++)
        cairo_arc(cr, r * 3 * (i + 1), y, r, 0, 2 * PI);
    for (int i = 0; i < imin(3, max - index - 1); i++)
        cairo_arc(cr, w - (r * 3 * (i + 1)), y, r, 0, 2 * PI);

    cairo_close_path(cr);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_fill(cr);
}

void draw_window(cairo_t* cr, GtkStyle* st, int flags)
{
    double w = Window_width, h = Window_height;
    double brad = fmax(w, h) / 10; // corner radius
    double w2 = w / 2, h3 = w * 3;
    double crad = flags & WINDOW_ROUND ? sqrt(w2 * w2 + h3 * h3) : 0;
    Color c = parse_color(Window_color, st->bg[GTK_STATE_SELECTED]);

    rect(cr, 0, 0, w, h, brad);
    cairo_clip(cr);

    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_paint(cr);

    if (Window_arch) {
        cairo_move_to(cr, 0, 0);
        cairo_line_to(cr, w, 0);
        // TODO properly calculate angles (asin/acos)
        cairo_arc_negative(cr, w2, h * 0.6 + h3, crad, -0.10 * PI, -0.80 * PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
        cairo_fill(cr);
    }

    c = parse_color(Border_color, st->text[GTK_STATE_SELECTED]);
    rect(cr, 0, 0, w, h, brad);
    cairo_set_line_width(cr, Border_width * 2);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_stroke(cr);
}

void clear(cairo_t* cr)
{
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}
