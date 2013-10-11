/*
*   fehlstart - a small launcher written in c99
*   this source is published under the GPLv3 license.
*   get the license from: http://www.gnu.org/licenses/gpl-3.0.txt
*   copyright 2013 maep and contributors
*/

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>
#include <gtk/gtk.h>

// draw labels, action upper label, input lower label
// action font size is reduced if it doesn't fit in window
void draw_labels(cairo_t* cr, GtkStyle* st, const char* action, const char* input);

// draws the icon
void draw_icon(cairo_t* cr, GdkPixbuf* icon);

// draw the indicator dots left & right, max 3
void draw_dots(cairo_t* cr, GtkStyle* st, int index, int max);

// draw window backround
void draw_window(cairo_t* cr, GtkStyle* st);

// clears context with transparent black
void clear(cairo_t* cr);

#endif
