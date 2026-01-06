/*
 * Forest OS PanicUI Window Manager - Stub Implementation
 *
 * The TTY-based panic screen does not use windowing.
 * These functions are stubs for compatibility.
 */

#include "include/panicui_wm.h"
#include "include/graphics/graphics_manager.h"
#include "include/string.h"
#include "include/memory.h"

void panicui_wm_init(void) {
    // No-op for TTY implementation
}

panicui_window_t* panicui_wm_create_window(int x, int y, int width, int height, const char* title) {
    (void)x; (void)y; (void)width; (void)height; (void)title;
    return NULL;
}

void panicui_wm_draw(graphics_surface_t* surface) {
    (void)surface;
}

void panicui_wm_handle_input(const input_event_t* event) {
    (void)event;
}
