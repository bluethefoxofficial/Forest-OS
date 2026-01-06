#ifndef DKS_CORE_H
#define DKS_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "dks_theme.h"

// Forward declarations
typedef struct dks_window dks_window_t;
typedef struct dks_desktop dks_desktop_t;

// Maximum limits
#define DKS_MAX_WINDOWS 32
#define DKS_WINDOW_MAX_TITLE 64

// Window state
typedef enum {
    DKS_WINDOW_STATE_NORMAL = 0,
    DKS_WINDOW_STATE_MINIMIZED,
    DKS_WINDOW_STATE_MAXIMIZED,
    DKS_WINDOW_STATE_FULLSCREEN,
    DKS_WINDOW_STATE_HIDDEN
} dks_window_state_t;

// Window flags
typedef enum {
    DKS_WINDOW_FLAG_NONE        = 0,
    DKS_WINDOW_FLAG_RESIZABLE   = (1 << 0),
    DKS_WINDOW_FLAG_MOVABLE     = (1 << 1),
    DKS_WINDOW_FLAG_CLOSABLE    = (1 << 2),
    DKS_WINDOW_FLAG_MINIMIZABLE = (1 << 3),
    DKS_WINDOW_FLAG_MAXIMIZABLE = (1 << 4),
    DKS_WINDOW_FLAG_DECORATED   = (1 << 5),
    DKS_WINDOW_FLAG_TOPMOST     = (1 << 6),
    DKS_WINDOW_FLAG_MODAL       = (1 << 7),
    DKS_WINDOW_FLAG_TOOL        = (1 << 8),  // Smaller titlebar
    DKS_WINDOW_FLAG_DIALOG      = (1 << 9),
    DKS_WINDOW_FLAG_DEFAULT     = (DKS_WINDOW_FLAG_RESIZABLE | DKS_WINDOW_FLAG_MOVABLE |
                                   DKS_WINDOW_FLAG_CLOSABLE | DKS_WINDOW_FLAG_MINIMIZABLE |
                                   DKS_WINDOW_FLAG_MAXIMIZABLE | DKS_WINDOW_FLAG_DECORATED)
} dks_window_flags_t;

// Window hit test results
typedef enum {
    DKS_HIT_NONE = 0,
    DKS_HIT_CLIENT,
    DKS_HIT_TITLEBAR,
    DKS_HIT_CLOSE,
    DKS_HIT_MINIMIZE,
    DKS_HIT_MAXIMIZE,
    DKS_HIT_RESIZE_N,
    DKS_HIT_RESIZE_S,
    DKS_HIT_RESIZE_E,
    DKS_HIT_RESIZE_W,
    DKS_HIT_RESIZE_NW,
    DKS_HIT_RESIZE_NE,
    DKS_HIT_RESIZE_SW,
    DKS_HIT_RESIZE_SE,
    DKS_HIT_MENUBAR,
    DKS_HIT_BORDER
} dks_hit_result_t;

// Window callback types
typedef void (*dks_window_paint_t)(dks_window_t* window, graphics_surface_t* surface);
typedef void (*dks_window_close_t)(dks_window_t* window);
typedef void (*dks_window_resize_t)(dks_window_t* window, uint32_t new_width, uint32_t new_height);
typedef void (*dks_window_focus_t)(dks_window_t* window, bool focused);
typedef bool (*dks_window_input_t)(dks_window_t* window, const input_event_t* event);

// Window structure
struct dks_window {
    // Identity
    uint32_t id;
    bool in_use;
    char title[DKS_WINDOW_MAX_TITLE];

    // Geometry
    int32_t x, y;
    uint32_t width, height;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;

    // Saved geometry (for restore from maximized)
    int32_t saved_x, saved_y;
    uint32_t saved_width, saved_height;

    // State
    dks_window_state_t state;
    uint32_t flags;
    int32_t z_order;
    bool focused;
    bool dirty;

    // Colors (can override theme)
    bool custom_colors;
    graphics_color_t border_color;
    graphics_color_t fill_color;
    graphics_color_t titlebar_color;

    // Content
    graphics_surface_t* surface;        // Client area surface
    widget_t* root_widget;              // Widget tree root
    widget_t* menubar;                  // Optional menu bar

    // Callbacks
    dks_window_paint_t on_paint;
    dks_window_close_t on_close;
    dks_window_resize_t on_resize;
    dks_window_focus_t on_focus;
    dks_window_input_t on_input;

    // User data
    void* user_data;

    // App identifier
    char app_id[32];
    bmp_image_t* app_icon;
};

// Desktop core initialization
void dks_core_init(void);
void dks_core_shutdown(void);
bool dks_core_is_initialized(void);

// Main loop
void dks_core_run(void);
void dks_core_quit(void);
bool dks_core_is_running(void);

// Screen info
uint32_t dks_get_screen_width(void);
uint32_t dks_get_screen_height(void);
graphics_surface_t* dks_get_screen_surface(void);

// Window management
dks_window_t* dks_window_create(const char* title, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t flags);
dks_window_t* dks_window_create_centered(const char* title, uint32_t width, uint32_t height, uint32_t flags);
void dks_window_destroy(dks_window_t* window);
dks_window_t* dks_window_find(uint32_t id);
dks_window_t* dks_window_get_focused(void);
uint32_t dks_window_count(void);

// Window operations
void dks_window_show(dks_window_t* window);
void dks_window_hide(dks_window_t* window);
void dks_window_minimize(dks_window_t* window);
void dks_window_maximize(dks_window_t* window);
void dks_window_restore(dks_window_t* window);
void dks_window_close(dks_window_t* window);
void dks_window_focus(dks_window_t* window);
void dks_window_bring_to_front(dks_window_t* window);
void dks_window_send_to_back(dks_window_t* window);

// Window properties
void dks_window_set_title(dks_window_t* window, const char* title);
void dks_window_set_position(dks_window_t* window, int32_t x, int32_t y);
void dks_window_set_size(dks_window_t* window, uint32_t width, uint32_t height);
void dks_window_set_min_size(dks_window_t* window, uint32_t min_w, uint32_t min_h);
void dks_window_set_max_size(dks_window_t* window, uint32_t max_w, uint32_t max_h);
void dks_window_set_icon(dks_window_t* window, bmp_image_t* icon);
void dks_window_center(dks_window_t* window);

// Window content
void dks_window_set_root_widget(dks_window_t* window, widget_t* widget);
void dks_window_set_menubar(dks_window_t* window, widget_t* menubar);
graphics_surface_t* dks_window_get_surface(dks_window_t* window);
void dks_window_invalidate(dks_window_t* window);
void dks_window_invalidate_rect(dks_window_t* window, const graphics_rect_t* rect);

// Window callbacks
void dks_window_set_paint_callback(dks_window_t* window, dks_window_paint_t callback);
void dks_window_set_close_callback(dks_window_t* window, dks_window_close_t callback);
void dks_window_set_resize_callback(dks_window_t* window, dks_window_resize_t callback);
void dks_window_set_focus_callback(dks_window_t* window, dks_window_focus_t callback);
void dks_window_set_input_callback(dks_window_t* window, dks_window_input_t callback);

// Hit testing
dks_hit_result_t dks_window_hit_test(dks_window_t* window, int32_t x, int32_t y);
dks_window_t* dks_window_at_point(int32_t x, int32_t y);

// Get client area dimensions
void dks_window_get_client_rect(dks_window_t* window, graphics_rect_t* rect);
uint32_t dks_window_get_titlebar_height(dks_window_t* window);

// Rendering (internal, but exposed for custom rendering)
void dks_render_frame(void);
void dks_render_window(dks_window_t* window, graphics_surface_t* target);
void dks_render_window_decorations(dks_window_t* window, graphics_surface_t* target);

// Cursor management
typedef enum {
    DKS_CURSOR_ARROW,
    DKS_CURSOR_POINTER,
    DKS_CURSOR_TEXT,
    DKS_CURSOR_WAIT,
    DKS_CURSOR_RESIZE_H,
    DKS_CURSOR_RESIZE_V,
    DKS_CURSOR_RESIZE_DIAG1,
    DKS_CURSOR_RESIZE_DIAG2,
    DKS_CURSOR_MOVE,
    DKS_CURSOR_NOT_ALLOWED
} dks_cursor_type_t;

void dks_set_cursor(dks_cursor_type_t cursor);
void dks_show_cursor(bool show);

#endif // DKS_CORE_H
