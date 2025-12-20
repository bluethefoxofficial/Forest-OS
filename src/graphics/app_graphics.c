#include "../include/graphics/app_graphics.h"
#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/window_manager.h"
#include "../include/graphics/font_renderer.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/libc/stdlib.h"
#include "../include/debuglog.h"

// Application graphics state
static struct {
    bool initialized;
    bool main_loop_running;
    app_graphics_context_t* context_list;
    uint32_t context_count;
} app_graphics_state = {
    .initialized = false,
    .main_loop_running = false,
    .context_list = NULL,
    .context_count = 0
};

// Helper functions
static void app_window_paint_callback(window_t* window, graphics_surface_t* surface);
static void app_window_resize_callback(window_t* window, uint32_t new_width, uint32_t new_height);
static void app_window_close_callback(window_t* window);
static void app_window_input_callback(window_t* window, const input_event_t* event);
static app_graphics_context_t* find_context_by_window(window_handle_t handle);
static graphics_result_t add_context_to_list(app_graphics_context_t* ctx);
static graphics_result_t remove_context_from_list(app_graphics_context_t* ctx);
static bool point_in_clip_rect(app_graphics_context_t* ctx, int32_t x, int32_t y);

static void app_window_input_callback(window_t* window, const input_event_t* event) {
    (void)window;
    (void)event;
}

graphics_result_t app_graphics_init(void) {
    debuglog(DEBUG_INFO, "Initializing application graphics API...\n");
    
    if (app_graphics_state.initialized) {
        debuglog(DEBUG_WARN, "Application graphics API already initialized\n");
        return GRAPHICS_SUCCESS;
    }
    
    // Check that underlying graphics systems are initialized
    if (!graphics_is_initialized()) {
        debuglog(DEBUG_ERROR, "Graphics manager not initialized\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    if (!window_manager_is_initialized()) {
        debuglog(DEBUG_ERROR, "Window manager not initialized\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    if (!font_renderer_is_initialized()) {
        debuglog(DEBUG_ERROR, "Font renderer not initialized\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    app_graphics_state.initialized = true;
    app_graphics_state.main_loop_running = false;
    app_graphics_state.context_list = NULL;
    app_graphics_state.context_count = 0;
    
    debuglog(DEBUG_INFO, "Application graphics API initialized successfully\n");
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_graphics_shutdown(void) {
    if (!app_graphics_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "Shutting down application graphics API...\n");
    
    // Destroy all contexts
    app_graphics_context_t* current = app_graphics_state.context_list;
    while (current) {
        app_graphics_context_t* next = (app_graphics_context_t*)current->user_data; // Using user_data as next pointer
        app_destroy_window(current);
        current = next;
    }
    
    app_graphics_state.initialized = false;
    app_graphics_state.main_loop_running = false;
    
    debuglog(DEBUG_INFO, "Application graphics API shutdown complete\n");
    return GRAPHICS_SUCCESS;
}

bool app_graphics_is_initialized(void) {
    return app_graphics_state.initialized;
}

app_graphics_context_t* app_create_window(const app_window_params_t* params) {
    if (!params || !app_graphics_state.initialized) {
        debuglog(DEBUG_ERROR, "Invalid parameters for window creation\n");
        return NULL;
    }
    
    // Create graphics context
    app_graphics_context_t* ctx = kmalloc(sizeof(app_graphics_context_t));
    if (!ctx) {
        debuglog(DEBUG_ERROR, "Failed to allocate memory for graphics context\n");
        return NULL;
    }
    
    memset(ctx, 0, sizeof(app_graphics_context_t));
    
    // Create underlying window
    window_handle_t handle = window_create(params->x, params->y, params->width, params->height,
                                          params->title, params->flags);
    if (handle == INVALID_WINDOW_HANDLE) {
        debuglog(DEBUG_ERROR, "Failed to create window\n");
        kfree(ctx);
        return NULL;
    }
    
    // Initialize context
    ctx->window_handle = handle;
    ctx->window = window_get(handle);
    if (!ctx->window) {
        debuglog(DEBUG_ERROR, "Failed to get window object\n");
        window_destroy(handle);
        kfree(ctx);
        return NULL;
    }
    
    // Get window surface
    if (window_get_surface(handle, &ctx->surface) != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to get window surface\n");
        window_destroy(handle);
        kfree(ctx);
        return NULL;
    }
    
    // Initialize drawing state
    ctx->foreground_color = COLOR_WHITE;
    ctx->background_color = COLOR_BLACK;
    ctx->draw_mode = DRAW_MODE_IMMEDIATE;
    ctx->clipping_enabled = false;
    ctx->clip_rect.x = 0;
    ctx->clip_rect.y = 0;
    ctx->clip_rect.width = params->width;
    ctx->clip_rect.height = params->height;
    
    // Set up identity transform
    ctx->transform.xx = 1.0f;
    ctx->transform.xy = 0.0f;
    ctx->transform.yx = 0.0f;
    ctx->transform.yy = 1.0f;
    ctx->transform.x0 = 0.0f;
    ctx->transform.y0 = 0.0f;
    
    // Get system font
    font_get_system_font(&ctx->current_font);
    
    // Initialize text style
    ctx->text_style = (text_style_t)DEFAULT_TEXT_STYLE;
    
    // Set callbacks
    ctx->on_paint = params->on_paint;
    ctx->on_resize = params->on_resize;
    ctx->on_close = params->on_close;
    ctx->on_key = params->on_key;
    ctx->on_mouse = params->on_mouse;
    ctx->user_data = params->user_data;
    ctx->needs_redraw = true;
    ctx->valid = true;
    
    // Set window callbacks
    window_set_paint_callback(handle, app_window_paint_callback);
    window_set_resize_callback(handle, app_window_resize_callback);
    ctx->window->on_close = app_window_close_callback;
    ctx->window->on_input = app_window_input_callback;
    
    // Add to context list
    add_context_to_list(ctx);
    
    debuglog(DEBUG_INFO, "Created application window '%s' (%ux%u)\n", 
            params->title, params->width, params->height);
    
    return ctx;
}

graphics_result_t app_destroy_window(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Destroying application window\n");
    
    // Call close callback if set
    if (ctx->on_close) {
        ctx->on_close(ctx);
    }
    
    // Remove from context list
    remove_context_from_list(ctx);
    
    // Destroy underlying window
    window_destroy(ctx->window_handle);
    
    // Mark context as invalid
    ctx->valid = false;
    
    // Free context
    kfree(ctx);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_begin_drawing(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid || !ctx->surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // For immediate mode, nothing special needed
    // For buffered mode, you might clear a back buffer here
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_end_drawing(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (ctx->draw_mode == DRAW_MODE_BUFFERED) {
        return app_flush_drawing(ctx);
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_flush_drawing(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Present the window
    window_present(ctx->window_handle);
    
    // Update compositor
    compositor_update();
    
    ctx->needs_redraw = false;
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_set_foreground_color(app_graphics_context_t* ctx, graphics_color_t color) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    ctx->foreground_color = color;
    ctx->text_style.foreground = color;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_set_background_color(app_graphics_context_t* ctx, graphics_color_t color) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    ctx->background_color = color;
    ctx->text_style.background = color;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_clear(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    return app_clear_with_color(ctx, ctx->background_color);
}

graphics_result_t app_clear_with_color(app_graphics_context_t* ctx, graphics_color_t color) {
    if (!ctx || !ctx->valid || !ctx->surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = graphics_color_to_pixel(color, ctx->surface->format);
    
    // Clear the surface
    switch (ctx->surface->format) {
        case PIXEL_FORMAT_RGB_565:
        case PIXEL_FORMAT_RGB_555: {
            uint16_t* pixels = (uint16_t*)ctx->surface->pixels;
            uint32_t pixel_count = (ctx->surface->pitch / 2) * ctx->surface->height;
            for (uint32_t i = 0; i < pixel_count; i++) {
                pixels[i] = (uint16_t)pixel_value;
            }
            break;
        }
        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_RGBA_8888: {
            uint32_t* pixels = (uint32_t*)ctx->surface->pixels;
            uint32_t pixel_count = (ctx->surface->pitch / 4) * ctx->surface->height;
            for (uint32_t i = 0; i < pixel_count; i++) {
                pixels[i] = pixel_value;
            }
            break;
        }
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    ctx->needs_redraw = true;
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_draw_pixel(app_graphics_context_t* ctx, int32_t x, int32_t y) {
    return app_draw_pixel_with_color(ctx, x, y, ctx->foreground_color);
}

graphics_result_t app_draw_pixel_with_color(app_graphics_context_t* ctx, int32_t x, int32_t y, graphics_color_t color) {
    if (!ctx || !ctx->valid || !ctx->surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Check clipping
    if (ctx->clipping_enabled && !point_in_clip_rect(ctx, x, y)) {
        return GRAPHICS_SUCCESS;
    }
    
    // Check bounds
    if (x < 0 || x >= (int32_t)ctx->surface->width || y < 0 || y >= (int32_t)ctx->surface->height) {
        return GRAPHICS_SUCCESS;
    }
    
    uint32_t pixel_value = graphics_color_to_pixel(color, ctx->surface->format);
    
    switch (ctx->surface->format) {
        case PIXEL_FORMAT_RGB_565:
        case PIXEL_FORMAT_RGB_555: {
            uint16_t* pixels = (uint16_t*)ctx->surface->pixels;
            pixels[y * (ctx->surface->pitch / 2) + x] = (uint16_t)pixel_value;
            break;
        }
        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_RGBA_8888: {
            uint32_t* pixels = (uint32_t*)ctx->surface->pixels;
            pixels[y * (ctx->surface->pitch / 4) + x] = pixel_value;
            break;
        }
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    ctx->needs_redraw = true;
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_draw_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    graphics_rect_t rect = {x, y, width, height};
    return graphics_draw_rect(&rect, ctx->foreground_color, false);
}

graphics_result_t app_fill_rect(app_graphics_context_t* ctx, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Simple filled rectangle implementation
    for (uint32_t dy = 0; dy < height; dy++) {
        for (uint32_t dx = 0; dx < width; dx++) {
            app_draw_pixel_with_color(ctx, x + dx, y + dy, ctx->foreground_color);
        }
    }
    
    ctx->needs_redraw = true;
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_draw_line(app_graphics_context_t* ctx, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Simple Bresenham line algorithm
    int32_t dx = abs(x2 - x1);
    int32_t dy = abs(y2 - y1);
    int32_t x_inc = (x1 < x2) ? 1 : -1;
    int32_t y_inc = (y1 < y2) ? 1 : -1;
    int32_t error = dx - dy;
    
    int32_t x = x1;
    int32_t y = y1;
    
    while (true) {
        app_draw_pixel(ctx, x, y);
        
        if (x == x2 && y == y2) {
            break;
        }
        
        int32_t error2 = error * 2;
        if (error2 > -dy) {
            error -= dy;
            x += x_inc;
        }
        if (error2 < dx) {
            error += dx;
            y += y_inc;
        }
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_set_font(app_graphics_context_t* ctx, font_t* font) {
    if (!ctx || !ctx->valid || !font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    ctx->current_font = font;
    return GRAPHICS_SUCCESS;
}

graphics_result_t app_draw_text(app_graphics_context_t* ctx, int32_t x, int32_t y, const char* text) {
    if (!ctx || !ctx->valid || !text || !ctx->current_font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    return font_render_text(ctx->current_font, ctx->surface, x, y, text, &ctx->text_style);
}

graphics_result_t app_measure_text(app_graphics_context_t* ctx, const char* text, uint32_t* width, uint32_t* height) {
    if (!ctx || !ctx->valid || !text || !width || !height || !ctx->current_font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    return font_measure_text(ctx->current_font, text, width, height);
}

graphics_result_t app_invalidate_window(app_graphics_context_t* ctx) {
    if (!ctx || !ctx->valid) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    ctx->needs_redraw = true;
    return window_invalidate(ctx->window_handle);
}

// Callback implementations
static void app_window_paint_callback(window_t* window, graphics_surface_t* surface) {
    if (!window) {
        return;
    }
    
    app_graphics_context_t* ctx = find_context_by_window(window->handle);
    if (ctx && ctx->on_paint) {
        ctx->on_paint(ctx);
    }
}

static void app_window_resize_callback(window_t* window, uint32_t new_width, uint32_t new_height) {
    if (!window) {
        return;
    }
    
    app_graphics_context_t* ctx = find_context_by_window(window->handle);
    if (ctx) {
        // Update clipping rectangle
        if (!ctx->clipping_enabled) {
            ctx->clip_rect.width = new_width;
            ctx->clip_rect.height = new_height;
        }
        
        if (ctx->on_resize) {
            ctx->on_resize(ctx, new_width, new_height);
        }
    }
}

static void app_window_close_callback(window_t* window) {
    if (!window) {
        return;
    }
    
    app_graphics_context_t* ctx = find_context_by_window(window->handle);
    if (ctx && ctx->on_close) {
        ctx->on_close(ctx);
    }
}

// Helper function implementations
static app_graphics_context_t* find_context_by_window(window_handle_t handle) {
    app_graphics_context_t* current = app_graphics_state.context_list;
    while (current) {
        if (current->window_handle == handle) {
            return current;
        }
        current = (app_graphics_context_t*)current->user_data; // Using user_data as next pointer
    }
    return NULL;
}

static graphics_result_t add_context_to_list(app_graphics_context_t* ctx) {
    if (!ctx) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    ctx->user_data = app_graphics_state.context_list; // Using user_data as next pointer
    app_graphics_state.context_list = ctx;
    app_graphics_state.context_count++;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t remove_context_from_list(app_graphics_context_t* ctx) {
    if (!ctx) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (app_graphics_state.context_list == ctx) {
        app_graphics_state.context_list = (app_graphics_context_t*)ctx->user_data;
    } else {
        app_graphics_context_t* current = app_graphics_state.context_list;
        while (current && (app_graphics_context_t*)current->user_data != ctx) {
            current = (app_graphics_context_t*)current->user_data;
        }
        if (current) {
            current->user_data = ctx->user_data;
        }
    }
    
    app_graphics_state.context_count--;
    return GRAPHICS_SUCCESS;
}

static bool point_in_clip_rect(app_graphics_context_t* ctx, int32_t x, int32_t y) {
    if (!ctx) {
        return false;
    }
    
    return (x >= ctx->clip_rect.x && 
            x < ctx->clip_rect.x + (int32_t)ctx->clip_rect.width &&
            y >= ctx->clip_rect.y && 
            y < ctx->clip_rect.y + (int32_t)ctx->clip_rect.height);
}
