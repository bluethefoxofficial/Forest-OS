#ifndef PANICUI_GFX_H
#define PANICUI_GFX_H

#include "graphics/graphics_manager.h"

// Define some standard colors for the PanicUI
#define PANIC_BG_COLOR      ((graphics_color_t){0, 0, 139, 255})  // Dark Blue
#define PANIC_BORDER_COLOR  ((graphics_color_t){128, 128, 128, 255}) // Gray
#define PANIC_TITLE_COLOR   ((graphics_color_t){255, 255, 255, 255}) // White
#define PANIC_TEXT_COLOR    ((graphics_color_t){255, 255, 255, 255}) // White
#define PANIC_ACCENT_COLOR  ((graphics_color_t){255, 0, 0, 255}) // Red

// Function to draw a window with a title bar
void panic_draw_window(int x, int y, int width, int height, const char* title);

// Function to draw a button
void panic_draw_button(int x, int y, int width, int height, const char* text, bool pressed);

// Function to draw a text box
void panic_draw_textbox(int x, int y, int width, int height, const char* text);

#endif // PANICUI_GFX_H