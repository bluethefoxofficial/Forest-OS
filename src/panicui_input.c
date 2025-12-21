#include "include/panicui_input.h"
#include "include/graphics/graphics_manager.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/panicui_wm.h"

// A simple cursor bitmap (16x16)
static const char* aabb = ""
"X...............X..............."
"XX..............X..............."
"X.X.............X..............."
"X..X............X..............."
"X...X...........X..............."
"X....X..........X..............."
"X.....X.........X..............."
"X......X........X..............."
"X.......X.......X..............."
"X........X......X..............."
"X.........X.....X..............."
"X......XXXXX....X..............."
"X...X..X........X..............."
"X..X X..X........X..............."
"X.X  X..X........X..............."
"XX    X..X........X..............."
"X      X..X........X..............."
"        X..........X.............."
"         X.........X.............."
"          X.........X............."
"           X.........X............"
"            X.........X..........."
"             X.........X.........."
"              X.........X........."
"               X.........X........"
"                X.........X......."
"                 X.........X......"
"                  X.........X....."
"                   X.........X...."
"                    X.........X..."
"                     X.........X.."
"                      X.........X."
"                       X.........X";

static graphics_surface_t* g_cursor_surface = NULL;

static void create_cursor_surface() {
    if (g_cursor_surface) {
        return;
    }

    graphics_create_surface(16, 16, PIXEL_FORMAT_RGBA_8888, &g_cursor_surface);
    if (!g_cursor_surface) {
        return;
    }

    uint32_t* pixels = (uint32_t*)g_cursor_surface->pixels;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            char p = aabb[y * 16 + x];
            if (p == 'X') {
                pixels[y * 16 + x] = 0xFF000000; // Black
            } else if (p == '.') {
                pixels[y * 16 + x] = 0xFFFFFFFF; // White
            } else {
                pixels[y * 16 + x] = 0x00000000; // Transparent
            }
        }
    }
}

void panicui_init_input() {
    create_cursor_surface();
    if (g_cursor_surface) {
        graphics_set_cursor(g_cursor_surface, 0, 0);
        graphics_show_cursor(true);
    }
    graphics_register_input_handler(panicui_process_input_event);
}

void panicui_process_input_event(const input_event_t* event) {
    if (event->type == INPUT_EVENT_MOUSE_MOVE) {
        graphics_move_cursor(event->mouse_move.x, event->mouse_move.y);
    } else if (event->type == INPUT_EVENT_KEY_PRESS) {
        // This will be handled by the window manager
    }
    panicui_wm_handle_input(event);
}
