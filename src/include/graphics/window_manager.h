#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "graphics_types.h"

// Window states
typedef enum {
    WINDOW_STATE_NORMAL = 0,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_FULLSCREEN,
    WINDOW_STATE_CLOSED
} window_state_t;

// Window flags
#define WINDOW_FLAG_RESIZABLE       (1 << 0)
#define WINDOW_FLAG_MOVABLE         (1 << 1)
#define WINDOW_FLAG_CLOSABLE        (1 << 2)
#define WINDOW_FLAG_MINIMIZABLE     (1 << 3)
#define WINDOW_FLAG_MAXIMIZABLE     (1 << 4)
#define WINDOW_FLAG_DECORATED       (1 << 5)
#define WINDOW_FLAG_TOPMOST         (1 << 6)
#define WINDOW_FLAG_MODAL          (1 << 7)

// Default window flags
#define WINDOW_FLAGS_DEFAULT        (WINDOW_FLAG_RESIZABLE | WINDOW_FLAG_MOVABLE | \
                                    WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_MINIMIZABLE | \
                                    WINDOW_FLAG_MAXIMIZABLE | WINDOW_FLAG_DECORATED)

// Window handle type
typedef uint32_t window_handle_t;
#define INVALID_WINDOW_HANDLE 0

// Window structure
typedef struct window {
    window_handle_t handle;
    int32_t x, y;                   // Position
    uint32_t width, height;         // Size
    uint32_t min_width, min_height; // Minimum size constraints
    uint32_t max_width, max_height; // Maximum size constraints
    
    char title[64];                 // Window title
    window_state_t state;           // Current window state
    uint32_t flags;                 // Window behavior flags
    int32_t z_order;                // Z-order for layering
    
    graphics_surface_t* surface;    // Window's rendering surface
    graphics_surface_t* back_buffer; // Double buffering support
    
    bool visible;                   // Visibility state
    bool focused;                   // Focus state
    bool dirty;                     // Needs redraw
    
    // Window callbacks
    void (*on_paint)(struct window* window, graphics_surface_t* surface);
    void (*on_resize)(struct window* window, uint32_t new_width, uint32_t new_height);
    void (*on_move)(struct window* window, int32_t new_x, int32_t new_y);
    void (*on_close)(struct window* window);
    void (*on_focus)(struct window* window, bool focused);
    void (*on_input)(struct window* window, const input_event_t* event);
    
    struct window* next;            // Linked list for window management
    void* user_data;                // Application-specific data
} window_t;

// Window manager initialization and shutdown
graphics_result_t window_manager_init(void);
graphics_result_t window_manager_shutdown(void);
bool window_manager_is_initialized(void);

// Window creation and destruction
window_handle_t window_create(int32_t x, int32_t y, uint32_t width, uint32_t height,
                             const char* title, uint32_t flags);
graphics_result_t window_destroy(window_handle_t handle);
window_t* window_get(window_handle_t handle);

// Window properties
graphics_result_t window_set_title(window_handle_t handle, const char* title);
graphics_result_t window_get_title(window_handle_t handle, char* title, size_t size);
graphics_result_t window_set_position(window_handle_t handle, int32_t x, int32_t y);
graphics_result_t window_get_position(window_handle_t handle, int32_t* x, int32_t* y);
graphics_result_t window_set_size(window_handle_t handle, uint32_t width, uint32_t height);
graphics_result_t window_get_size(window_handle_t handle, uint32_t* width, uint32_t* height);
graphics_result_t window_set_constraints(window_handle_t handle, 
                                       uint32_t min_width, uint32_t min_height,
                                       uint32_t max_width, uint32_t max_height);

// Window state management
graphics_result_t window_show(window_handle_t handle);
graphics_result_t window_hide(window_handle_t handle);
graphics_result_t window_minimize(window_handle_t handle);
graphics_result_t window_maximize(window_handle_t handle);
graphics_result_t window_restore(window_handle_t handle);
graphics_result_t window_set_fullscreen(window_handle_t handle, bool fullscreen);

// Focus management
graphics_result_t window_focus(window_handle_t handle);
window_handle_t window_get_focused(void);
graphics_result_t window_bring_to_front(window_handle_t handle);
graphics_result_t window_send_to_back(window_handle_t handle);

// Window callbacks
graphics_result_t window_set_paint_callback(window_handle_t handle,
                                          void (*callback)(window_t* window, graphics_surface_t* surface));
graphics_result_t window_set_resize_callback(window_handle_t handle,
                                           void (*callback)(window_t* window, uint32_t new_width, uint32_t new_height));
graphics_result_t window_set_input_callback(window_handle_t handle,
                                          void (*callback)(window_t* window, const input_event_t* event));

// Rendering and compositing
graphics_result_t window_invalidate(window_handle_t handle);
graphics_result_t window_invalidate_rect(window_handle_t handle, const graphics_rect_t* rect);
graphics_result_t window_get_surface(window_handle_t handle, graphics_surface_t** surface);
graphics_result_t window_present(window_handle_t handle);

// Window enumeration and management
graphics_result_t window_enumerate(window_handle_t* handles, uint32_t* count);
window_handle_t window_find_by_title(const char* title);
window_handle_t window_at_point(int32_t x, int32_t y);

// Compositor functions
graphics_result_t compositor_update(void);
graphics_result_t compositor_force_redraw(void);
graphics_result_t compositor_enable_vsync(bool enable);

// Desktop management
graphics_result_t desktop_set_wallpaper(const graphics_surface_t* wallpaper);
graphics_result_t desktop_get_size(uint32_t* width, uint32_t* height);
graphics_result_t desktop_invalidate(void);

// Window manager configuration
typedef struct {
    uint32_t title_bar_height;
    uint32_t border_width;
    graphics_color_t title_bar_color;
    graphics_color_t border_color;
    graphics_color_t title_text_color;
    bool enable_shadows;
    bool enable_animations;
    uint32_t animation_duration_ms;
} window_manager_config_t;

graphics_result_t window_manager_get_config(window_manager_config_t* config);
graphics_result_t window_manager_set_config(const window_manager_config_t* config);

#endif // WINDOW_MANAGER_H