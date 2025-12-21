#ifndef PANICUI_WM_H
#define PANICUI_WM_H

#include "graphics/graphics_manager.h"
#include "panicui_gfx.h"

#define MAX_WINDOWS 10

typedef struct {
    int x, y, width, height;
    const char* title;
    bool active;
    void (*draw)(void* self, graphics_surface_t* surface);
    void (*handle_input)(void* self, const input_event_t* event);
} panicui_window_t;

void panicui_wm_init(void);
panicui_window_t* panicui_wm_create_window(int x, int y, int width, int height, const char* title);
void panicui_wm_draw(graphics_surface_t* surface);
void panicui_wm_handle_input(const input_event_t* event);

#endif // PANICUI_WM_H