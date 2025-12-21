#include "include/panicui_wm.h"
#include "include/memory.h"
#include "include/string.h"

static panicui_window_t* g_windows[MAX_WINDOWS];
static int g_window_count = 0;
static panicui_window_t* g_active_window = NULL;

void panicui_wm_init() {
    memset(g_windows, 0, sizeof(g_windows));
    g_window_count = 0;
    g_active_window = NULL;
}

panicui_window_t* panicui_wm_create_window(int x, int y, int width, int height, const char* title) {
    if (g_window_count >= MAX_WINDOWS) {
        return NULL;
    }

    panicui_window_t* win = kmalloc(sizeof(panicui_window_t));
    if (!win) {
        return NULL;
    }

    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->title = title;
    win->active = false;
    win->draw = NULL;
    win->handle_input = NULL;

    g_windows[g_window_count++] = win;

    if (!g_active_window) {
        win->active = true;
        g_active_window = win;
    }

    return win;
}

void panicui_wm_draw(graphics_surface_t* surface) {
    for (int i = 0; i < g_window_count; ++i) {
        panicui_window_t* win = g_windows[i];
        panic_draw_window(win->x, win->y, win->width, win->height, win->title);
        if (win->draw) {
            win->draw(win, surface);
        }
    }
}

static panicui_window_t* find_window_at(int x, int y) {
    for (int i = g_window_count - 1; i >= 0; --i) {
        panicui_window_t* win = g_windows[i];
        if (x >= win->x && x < win->x + win->width &&
            y >= win->y && y < win->y + win->height) {
            return win;
        }
    }
    return NULL;
}

void panicui_wm_handle_input(const input_event_t* event) {
    if (event->type == INPUT_EVENT_KEY_PRESS) {
        if (g_active_window && g_active_window->handle_input) {
            g_active_window->handle_input(g_active_window, event);
        }
    } else if (event->type == INPUT_EVENT_MOUSE_BUTTON_PRESS) {
        panicui_window_t* target_win = find_window_at(event->mouse_button.x, event->mouse_button.y);
        if (target_win) {
            if (g_active_window) {
                g_active_window->active = false;
            }
            target_win->active = true;
            g_active_window = target_win;

            if (target_win->handle_input) {
                target_win->handle_input(target_win, event);
            }
        }
    } else if (event->type == INPUT_EVENT_MOUSE_MOVE) {
        // Mouse move events are handled by the graphics manager to move the cursor.
        // We can still forward them to the active window if needed for things like hover effects.
        if (g_active_window && g_active_window->handle_input) {
            g_active_window->handle_input(g_active_window, event);
        }
    }
}
