#ifndef APP_GRAPHICS_H
#define APP_GRAPHICS_H

#include "graphics_types.h"
#include "window_manager.h"
#include "font_renderer.h"

// Application graphics context
typedef struct app_graphics_context app_graphics_context_t;

// Drawing modes
typedef enum {
    DRAW_MODE_IMMEDIATE = 0,    // Draw operations are immediately visible
    DRAW_MODE_BUFFERED         // Draw operations are buffered until flush
} app_draw_mode_t;

// Application window creation parameters
typedef struct {
    int32_t x, y;              // Initial position
    uint32_t width, height;    // Initial size
    char title[64];            // Window title
    uint32_t flags;            // Window flags
    
    // Event callbacks
    void (*on_paint)(app_graphics_context_t* ctx);
    void (*on_resize)(app_graphics_context_t* ctx, uint32_t new_width, uint32_t new_height);
    void (*on_close)(app_graphics_context_t* ctx);
    void (*on_key)(app_graphics_context_t* ctx, uint32_t keycode, bool pressed);
    void (*on_mouse)(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t buttons);
    
    void* user_data;           // Application-specific data
} app_window_params_t;

// Graphics context structure
struct app_graphics_context {
    window_handle_t window_handle;
    window_t* window;
    graphics_surface_t* surface;
    
    // Current drawing state
    graphics_color_t foreground_color;
    graphics_color_t background_color;
    font_t* current_font;
    text_style_t text_style;
    app_draw_mode_t draw_mode;
    
    // Clipping rectangle
    graphics_rect_t clip_rect;
    bool clipping_enabled;
    
    // Transform matrix (for future use)
    struct {
        float xx, xy, yx, yy, x0, y0;
    } transform;
    
    // Application callbacks
    void (*on_paint)(app_graphics_context_t* ctx);
    void (*on_resize)(app_graphics_context_t* ctx, uint32_t new_width, uint32_t new_height);
    void (*on_close)(app_graphics_context_t* ctx);
    void (*on_key)(app_graphics_context_t* ctx, uint32_t keycode, bool pressed);
    void (*on_mouse)(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t buttons);
    
    void* user_data;
    bool needs_redraw;
    bool valid;
};

// Application graphics API initialization
graphics_result_t app_graphics_init(void);
graphics_result_t app_graphics_shutdown(void);
bool app_graphics_is_initialized(void);

// Window management
app_graphics_context_t* app_create_window(const app_window_params_t* params);
graphics_result_t app_destroy_window(app_graphics_context_t* ctx);
graphics_result_t app_show_window(app_graphics_context_t* ctx);
graphics_result_t app_hide_window(app_graphics_context_t* ctx);

// Window properties
graphics_result_t app_set_window_title(app_graphics_context_t* ctx, const char* title);
graphics_result_t app_set_window_position(app_graphics_context_t* ctx, int32_t x, int32_t y);
graphics_result_t app_set_window_size(app_graphics_context_t* ctx, uint32_t width, uint32_t height);
graphics_result_t app_get_window_size(app_graphics_context_t* ctx, uint32_t* width, uint32_t* height);

// Drawing context management
graphics_result_t app_begin_drawing(app_graphics_context_t* ctx);
graphics_result_t app_end_drawing(app_graphics_context_t* ctx);
graphics_result_t app_flush_drawing(app_graphics_context_t* ctx);
graphics_result_t app_set_draw_mode(app_graphics_context_t* ctx, app_draw_mode_t mode);

// Color management
graphics_result_t app_set_foreground_color(app_graphics_context_t* ctx, graphics_color_t color);
graphics_result_t app_set_background_color(app_graphics_context_t* ctx, graphics_color_t color);
graphics_result_t app_get_foreground_color(app_graphics_context_t* ctx, graphics_color_t* color);
graphics_result_t app_get_background_color(app_graphics_context_t* ctx, graphics_color_t* color);

// Basic drawing operations
graphics_result_t app_clear(app_graphics_context_t* ctx);
graphics_result_t app_clear_with_color(app_graphics_context_t* ctx, graphics_color_t color);
graphics_result_t app_draw_pixel(app_graphics_context_t* ctx, int32_t x, int32_t y);
graphics_result_t app_draw_pixel_with_color(app_graphics_context_t* ctx, int32_t x, int32_t y, graphics_color_t color);

// Line and shape drawing
graphics_result_t app_draw_line(app_graphics_context_t* ctx, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
graphics_result_t app_draw_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t width, uint32_t height);
graphics_result_t app_fill_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t width, uint32_t height);
graphics_result_t app_draw_circle(app_graphics_context_t* ctx, int32_t center_x, int32_t center_y, uint32_t radius);
graphics_result_t app_fill_circle(app_graphics_context_t* ctx, int32_t center_x, int32_t center_y, uint32_t radius);

// Advanced drawing
graphics_result_t app_draw_rounded_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, 
                                       uint32_t width, uint32_t height, uint32_t radius);
graphics_result_t app_fill_rounded_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, 
                                       uint32_t width, uint32_t height, uint32_t radius);
graphics_result_t app_draw_ellipse(app_graphics_context_t* ctx, int32_t x, int32_t y, 
                                  uint32_t width, uint32_t height);
graphics_result_t app_fill_ellipse(app_graphics_context_t* ctx, int32_t x, int32_t y, 
                                  uint32_t width, uint32_t height);

// Text rendering
graphics_result_t app_set_font(app_graphics_context_t* ctx, font_t* font);
graphics_result_t app_get_font(app_graphics_context_t* ctx, font_t** font);
graphics_result_t app_set_text_style(app_graphics_context_t* ctx, const text_style_t* style);
graphics_result_t app_get_text_style(app_graphics_context_t* ctx, text_style_t* style);

graphics_result_t app_draw_text(app_graphics_context_t* ctx, int32_t x, int32_t y, const char* text);
graphics_result_t app_draw_text_aligned(app_graphics_context_t* ctx, const graphics_rect_t* bounds, 
                                       const char* text, text_align_t alignment);
graphics_result_t app_measure_text(app_graphics_context_t* ctx, const char* text, 
                                  uint32_t* width, uint32_t* height);

// Clipping
graphics_result_t app_set_clip_rect(app_graphics_context_t* ctx, const graphics_rect_t* rect);
graphics_result_t app_get_clip_rect(app_graphics_context_t* ctx, graphics_rect_t* rect);
graphics_result_t app_enable_clipping(app_graphics_context_t* ctx, bool enable);
graphics_result_t app_reset_clip_rect(app_graphics_context_t* ctx);

// Image and surface operations
graphics_result_t app_draw_surface(app_graphics_context_t* ctx, int32_t x, int32_t y, 
                                  const graphics_surface_t* surface);
graphics_result_t app_draw_surface_rect(app_graphics_context_t* ctx, int32_t dst_x, int32_t dst_y,
                                       const graphics_surface_t* surface, 
                                       const graphics_rect_t* src_rect);
graphics_result_t app_create_surface(uint32_t width, uint32_t height, graphics_surface_t** surface);
graphics_result_t app_destroy_surface(graphics_surface_t* surface);

// Event handling and main loop
typedef enum {
    APP_EVENT_NONE = 0,
    APP_EVENT_QUIT,
    APP_EVENT_WINDOW_CLOSE,
    APP_EVENT_WINDOW_RESIZE,
    APP_EVENT_KEY_DOWN,
    APP_EVENT_KEY_UP,
    APP_EVENT_MOUSE_MOVE,
    APP_EVENT_MOUSE_DOWN,
    APP_EVENT_MOUSE_UP
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    app_graphics_context_t* ctx;
    union {
        struct {
            uint32_t keycode;
            uint32_t modifiers;
        } key;
        struct {
            int32_t x, y;
            uint32_t buttons;
        } mouse;
        struct {
            uint32_t width, height;
        } resize;
    };
} app_event_t;

graphics_result_t app_poll_events(app_event_t* event);
graphics_result_t app_wait_event(app_event_t* event);
graphics_result_t app_run_main_loop(void);
graphics_result_t app_quit_main_loop(void);

// Utility functions
graphics_result_t app_invalidate_window(app_graphics_context_t* ctx);
graphics_result_t app_invalidate_rect(app_graphics_context_t* ctx, const graphics_rect_t* rect);
graphics_result_t app_update_window(app_graphics_context_t* ctx);

// Configuration and capabilities
typedef struct {
    bool hardware_acceleration;
    bool alpha_blending;
    bool anti_aliasing;
    uint32_t max_texture_size;
    uint32_t max_surface_size;
} app_graphics_caps_t;

graphics_result_t app_get_capabilities(app_graphics_caps_t* caps);
graphics_result_t app_get_desktop_size(uint32_t* width, uint32_t* height);

// Helper macros
#define APP_WINDOW_PARAMS_DEFAULT { \
    .x = 100, .y = 100, \
    .width = 640, .height = 480, \
    .title = "Application Window", \
    .flags = WINDOW_FLAGS_DEFAULT, \
    .on_paint = NULL, \
    .on_resize = NULL, \
    .on_close = NULL, \
    .on_key = NULL, \
    .on_mouse = NULL, \
    .user_data = NULL \
}

// Color constants for convenience
#define APP_COLOR_BLACK        COLOR_BLACK
#define APP_COLOR_WHITE        COLOR_WHITE
#define APP_COLOR_RED          COLOR_RED
#define APP_COLOR_GREEN        COLOR_GREEN
#define APP_COLOR_BLUE         COLOR_BLUE
#define APP_COLOR_YELLOW       COLOR_YELLOW
#define APP_COLOR_CYAN         COLOR_CYAN
#define APP_COLOR_MAGENTA      COLOR_MAGENTA
#define APP_COLOR_GRAY         COLOR_GRAY
#define APP_COLOR_LIGHT_GRAY   COLOR_LIGHT_GRAY
#define APP_COLOR_DARK_GRAY    COLOR_DARK_GRAY

#endif // APP_GRAPHICS_H