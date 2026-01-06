/*
 * Forest OS PanicUI Graphics - Stub Implementation
 *
 * The TTY-based panic screen uses its own rendering.
 * These functions are stubs for compatibility.
 */

#include "include/panicui_gfx.h"
#include "include/graphics/graphics_manager.h"
#include "include/string.h"

void panic_draw_window(int x, int y, int width, int height, const char* title) {
    (void)x; (void)y; (void)width; (void)height; (void)title;
}

void panic_draw_button(int x, int y, int width, int height, const char* text, bool pressed) {
    (void)x; (void)y; (void)width; (void)height; (void)text; (void)pressed;
}

void panic_draw_textbox(int x, int y, int width, int height, const char* text) {
    (void)x; (void)y; (void)width; (void)height; (void)text;
}
