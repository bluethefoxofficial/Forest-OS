#include "../include/graphics/window_manager.h"
#include "../include/graphics/graphics_manager.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"

// Window manager state
static struct {
    bool initialized;
    window_t* window_list;
    window_handle_t next_handle;
    window_handle_t focused_window;
    uint32_t window_count;
    
    // Desktop properties
    uint32_t desktop_width;
    uint32_t desktop_height;
    graphics_surface_t* desktop_surface;
    graphics_surface_t* wallpaper;
    
    // Composition buffer
    graphics_surface_t* composition_buffer;
    bool needs_redraw;
    
    // Configuration
    window_manager_config_t config;
} wm_state = {
    .initialized = false,
    .window_list = NULL,
    .next_handle = 1,
    .focused_window = INVALID_WINDOW_HANDLE,
    .window_count = 0,
    .desktop_surface = NULL,
    .wallpaper = NULL,
    .composition_buffer = NULL,
    .needs_redraw = true
};

// Default window manager configuration
static const window_manager_config_t default_config = {
    .title_bar_height = 24,
    .border_width = 2,
    .title_bar_color = {64, 64, 64, 255},
    .border_color = {128, 128, 128, 255},
    .title_text_color = {255, 255, 255, 255},
    .enable_shadows = false,
    .enable_animations = false,
    .animation_duration_ms = 250
};

// Helper functions
static graphics_result_t create_window_surface(window_t* window);
static graphics_result_t destroy_window_surface(window_t* window);
static graphics_result_t draw_window_decorations(window_t* window);
static graphics_result_t composite_windows(void);
static window_t* find_window_by_handle(window_handle_t handle);
static graphics_result_t remove_window_from_list(window_t* window);
static graphics_result_t add_window_to_list(window_t* window);
static graphics_result_t update_z_order(void);

graphics_result_t window_manager_init(void) {
    debuglog(DEBUG_INFO, "Initializing window manager...\n");
    
    if (wm_state.initialized) {
        debuglog(DEBUG_WARN, "Window manager already initialized\n");
        return GRAPHICS_SUCCESS;
    }
    
    // Get desktop dimensions from graphics system
    video_mode_t current_mode;
    if (graphics_get_current_mode(&current_mode) != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to get current video mode\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    wm_state.desktop_width = current_mode.width;
    wm_state.desktop_height = current_mode.height;
    
    // Create desktop surface
    graphics_result_t result = graphics_create_surface(
        wm_state.desktop_width, wm_state.desktop_height,
        current_mode.format, &wm_state.desktop_surface
    );
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to create desktop surface\n");
        return result;
    }
    
    // Create composition buffer
    result = graphics_create_surface(
        wm_state.desktop_width, wm_state.desktop_height,
        current_mode.format, &wm_state.composition_buffer
    );
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to create composition buffer\n");
        graphics_destroy_surface(wm_state.desktop_surface);
        return result;
    }
    
    // Initialize configuration
    wm_state.config = default_config;
    
    // Clear desktop
    graphics_clear_screen(COLOR_DARK_GRAY);
    
    wm_state.initialized = true;
    debuglog(DEBUG_INFO, "Window manager initialized successfully (%ux%u)\n",
            wm_state.desktop_width, wm_state.desktop_height);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_manager_shutdown(void) {
    if (!wm_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "Shutting down window manager...\n");
    
    // Destroy all windows
    window_t* current = wm_state.window_list;
    while (current) {
        window_t* next = current->next;
        window_destroy(current->handle);
        current = next;
    }
    
    // Clean up surfaces
    if (wm_state.desktop_surface) {
        graphics_destroy_surface(wm_state.desktop_surface);
        wm_state.desktop_surface = NULL;
    }
    
    if (wm_state.composition_buffer) {
        graphics_destroy_surface(wm_state.composition_buffer);
        wm_state.composition_buffer = NULL;
    }
    
    if (wm_state.wallpaper) {
        graphics_destroy_surface(wm_state.wallpaper);
        wm_state.wallpaper = NULL;
    }
    
    wm_state.initialized = false;
    debuglog(DEBUG_INFO, "Window manager shutdown complete\n");
    
    return GRAPHICS_SUCCESS;
}

bool window_manager_is_initialized(void) {
    return wm_state.initialized;
}

window_handle_t window_create(int32_t x, int32_t y, uint32_t width, uint32_t height,
                             const char* title, uint32_t flags) {
    if (!wm_state.initialized) {
        debuglog(DEBUG_ERROR, "Window manager not initialized\n");
        return INVALID_WINDOW_HANDLE;
    }
    
    // Validate parameters
    if (width == 0 || height == 0) {
        debuglog(DEBUG_ERROR, "Invalid window dimensions: %ux%u\n", width, height);
        return INVALID_WINDOW_HANDLE;
    }
    
    // Create window structure
    window_t* window = kmalloc(sizeof(window_t));
    if (!window) {
        debuglog(DEBUG_ERROR, "Failed to allocate memory for window\n");
        return INVALID_WINDOW_HANDLE;
    }
    
    memset(window, 0, sizeof(window_t));
    
    // Initialize window properties
    window->handle = wm_state.next_handle++;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->min_width = 100;
    window->min_height = 50;
    window->max_width = wm_state.desktop_width;
    window->max_height = wm_state.desktop_height;
    window->flags = flags;
    window->state = WINDOW_STATE_NORMAL;
    window->z_order = wm_state.window_count;
    window->visible = true;
    window->focused = false;
    window->dirty = true;
    
    // Copy title
    if (title) {
        strncpy(window->title, title, sizeof(window->title) - 1);
        window->title[sizeof(window->title) - 1] = '\0';
    } else {
        strcpy(window->title, "Untitled Window");
    }
    
    // Create window surface
    if (create_window_surface(window) != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to create window surface\n");
        kfree(window);
        return INVALID_WINDOW_HANDLE;
    }
    
    // Add to window list
    add_window_to_list(window);
    wm_state.window_count++;
    
    // Focus the new window
    window_focus(window->handle);
    
    // Mark for redraw
    wm_state.needs_redraw = true;
    
    debuglog(DEBUG_INFO, "Created window '%s' (handle: %u, %dx%d at %d,%d)\n",
            window->title, window->handle, width, height, x, y);
    
    return window->handle;
}

graphics_result_t window_destroy(window_handle_t handle) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Destroying window '%s' (handle: %u)\n", window->title, handle);
    
    // Call close callback if set
    if (window->on_close) {
        window->on_close(window);
    }
    
    // Remove focus if this window was focused
    if (wm_state.focused_window == handle) {
        wm_state.focused_window = INVALID_WINDOW_HANDLE;
        // TODO: Focus next window in z-order
    }
    
    // Destroy window surface
    destroy_window_surface(window);
    
    // Remove from window list
    remove_window_from_list(window);
    wm_state.window_count--;
    
    // Update z-order
    update_z_order();
    
    // Free window structure
    kfree(window);
    
    // Mark for redraw
    wm_state.needs_redraw = true;
    
    return GRAPHICS_SUCCESS;
}

window_t* window_get(window_handle_t handle) {
    return find_window_by_handle(handle);
}

graphics_result_t window_set_title(window_handle_t handle, const char* title) {
    window_t* window = find_window_by_handle(handle);
    if (!window || !title) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    strncpy(window->title, title, sizeof(window->title) - 1);
    window->title[sizeof(window->title) - 1] = '\0';
    window->dirty = true;
    wm_state.needs_redraw = true;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_set_position(window_handle_t handle, int32_t x, int32_t y) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Constrain to desktop bounds
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + (int32_t)window->width > (int32_t)wm_state.desktop_width) {
        x = (int32_t)wm_state.desktop_width - (int32_t)window->width;
    }
    if (y + (int32_t)window->height > (int32_t)wm_state.desktop_height) {
        y = (int32_t)wm_state.desktop_height - (int32_t)window->height;
    }
    
    window->x = x;
    window->y = y;
    window->dirty = true;
    wm_state.needs_redraw = true;
    
    // Call move callback
    if (window->on_move) {
        window->on_move(window, x, y);
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_focus(window_handle_t handle) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Remove focus from current window
    if (wm_state.focused_window != INVALID_WINDOW_HANDLE) {
        window_t* old_focused = find_window_by_handle(wm_state.focused_window);
        if (old_focused) {
            old_focused->focused = false;
            old_focused->dirty = true;
            if (old_focused->on_focus) {
                old_focused->on_focus(old_focused, false);
            }
        }
    }
    
    // Set new focus
    wm_state.focused_window = handle;
    window->focused = true;
    window->dirty = true;
    wm_state.needs_redraw = true;
    
    // Call focus callback
    if (window->on_focus) {
        window->on_focus(window, true);
    }
    
    // Bring to front
    window_bring_to_front(handle);
    
    return GRAPHICS_SUCCESS;
}

window_handle_t window_get_focused(void) {
    return wm_state.focused_window;
}

graphics_result_t window_set_paint_callback(
        window_handle_t handle,
        void (*callback)(window_t* window, graphics_surface_t* surface)) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    window->on_paint = callback;
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_set_resize_callback(
        window_handle_t handle,
        void (*callback)(window_t* window, uint32_t new_width, uint32_t new_height)) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    window->on_resize = callback;
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_set_input_callback(
        window_handle_t handle,
        void (*callback)(window_t* window, const input_event_t* event)) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    window->on_input = callback;
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_get_surface(window_handle_t handle, graphics_surface_t** surface) {
    window_t* window = find_window_by_handle(handle);
    if (!window || !surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *surface = window->surface;
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_invalidate(window_handle_t handle) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    window->dirty = true;
    wm_state.needs_redraw = true;
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_invalidate_rect(window_handle_t handle, const graphics_rect_t* rect) {
    (void)rect;
    return window_invalidate(handle);
}

graphics_result_t window_present(window_handle_t handle) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    wm_state.needs_redraw = true;
    return compositor_update();
}

graphics_result_t compositor_update(void) {
    if (!wm_state.initialized || !wm_state.needs_redraw) {
        return GRAPHICS_SUCCESS;
    }
    
    return composite_windows();
}

// Helper function implementations
static graphics_result_t create_window_surface(window_t* window) {
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Get current pixel format
    video_mode_t current_mode;
    if (graphics_get_current_mode(&current_mode) != GRAPHICS_SUCCESS) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Create window surface
    graphics_result_t result = graphics_create_surface(
        window->width, window->height, current_mode.format, &window->surface
    );
    if (result != GRAPHICS_SUCCESS) {
        return result;
    }
    
    // Clear window surface
    if (window->surface && window->surface->pixels) {
        memset(window->surface->pixels, 0, 
               window->surface->pitch * window->surface->height);
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t destroy_window_surface(window_t* window) {
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (window->surface) {
        graphics_destroy_surface(window->surface);
        window->surface = NULL;
    }
    
    if (window->back_buffer) {
        graphics_destroy_surface(window->back_buffer);
        window->back_buffer = NULL;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t composite_windows(void) {
    if (!wm_state.composition_buffer) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Clear composition buffer with desktop background
    graphics_color_t bg_color = COLOR_DARK_GRAY;
    uint32_t pixel_value = graphics_color_to_pixel(bg_color, wm_state.composition_buffer->format);
    
    // Simple clear - in a real implementation you'd blit the wallpaper
    if (wm_state.composition_buffer->pixels) {
        uint32_t* pixels = (uint32_t*)wm_state.composition_buffer->pixels;
        uint32_t total_pixels = (wm_state.composition_buffer->pitch / 4) * wm_state.composition_buffer->height;
        for (uint32_t i = 0; i < total_pixels; i++) {
            pixels[i] = pixel_value;
        }
    }
    
    // Composite windows from back to front (reverse z-order)
    window_t* windows[256]; // Max windows
    uint32_t window_count = 0;
    
    // Collect visible windows
    window_t* current = wm_state.window_list;
    while (current && window_count < 256) {
        if (current->visible && current->state != WINDOW_STATE_MINIMIZED) {
            windows[window_count++] = current;
        }
        current = current->next;
    }
    
    // Sort by z-order (simple bubble sort for now)
    for (uint32_t i = 0; i < window_count - 1; i++) {
        for (uint32_t j = 0; j < window_count - i - 1; j++) {
            if (windows[j]->z_order > windows[j + 1]->z_order) {
                window_t* temp = windows[j];
                windows[j] = windows[j + 1];
                windows[j + 1] = temp;
            }
        }
    }
    
    // Composite each window
    for (uint32_t i = 0; i < window_count; i++) {
        window_t* window = windows[i];
        
        // Draw window decorations if needed
        if (window->flags & WINDOW_FLAG_DECORATED) {
            draw_window_decorations(window);
        }
        
        // Call paint callback if window is dirty
        if (window->dirty && window->on_paint && window->surface) {
            window->on_paint(window, window->surface);
            window->dirty = false;
        }
        
        // Blit window surface to composition buffer
        if (window->surface && window->surface->pixels) {
            graphics_rect_t src_rect = {0, 0, window->width, window->height};
            graphics_blit_surface(window->surface, &src_rect, window->x, window->y);
        }
    }
    
    wm_state.needs_redraw = false;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t draw_window_decorations(window_t* window) {
    if (!window || !window->surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Draw title bar
    graphics_rect_t title_bar = {
        0, 0, window->width, wm_state.config.title_bar_height
    };
    
    // Fill title bar background
    graphics_color_t title_color = window->focused ? 
        wm_state.config.title_bar_color : COLOR_GRAY;
    
    // Simple title bar drawing - in a real implementation you'd use proper drawing APIs
    if (window->surface->pixels) {
        uint32_t* pixels = (uint32_t*)window->surface->pixels;
        uint32_t title_pixel = graphics_color_to_pixel(title_color, window->surface->format);
        
        for (uint32_t y = 0; y < wm_state.config.title_bar_height; y++) {
            for (uint32_t x = 0; x < window->width; x++) {
                if (y * (window->surface->pitch / 4) + x < 
                    (window->surface->pitch / 4) * window->surface->height) {
                    pixels[y * (window->surface->pitch / 4) + x] = title_pixel;
                }
            }
        }
    }
    
    // TODO: Draw window title text, buttons, etc.
    
    return GRAPHICS_SUCCESS;
}

static window_t* find_window_by_handle(window_handle_t handle) {
    window_t* current = wm_state.window_list;
    while (current) {
        if (current->handle == handle) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static graphics_result_t add_window_to_list(window_t* window) {
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    window->next = wm_state.window_list;
    wm_state.window_list = window;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t remove_window_from_list(window_t* window) {
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (wm_state.window_list == window) {
        wm_state.window_list = window->next;
    } else {
        window_t* current = wm_state.window_list;
        while (current && current->next != window) {
            current = current->next;
        }
        if (current) {
            current->next = window->next;
        }
    }
    
    window->next = NULL;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t update_z_order(void) {
    int32_t z = 0;
    window_t* current = wm_state.window_list;
    while (current) {
        current->z_order = z++;
        current = current->next;
    }
    return GRAPHICS_SUCCESS;
}

graphics_result_t window_bring_to_front(window_handle_t handle) {
    window_t* window = find_window_by_handle(handle);
    if (!window) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Find highest z-order
    int32_t max_z = 0;
    window_t* current = wm_state.window_list;
    while (current) {
        if (current->z_order > max_z) {
            max_z = current->z_order;
        }
        current = current->next;
    }
    
    window->z_order = max_z + 1;
    wm_state.needs_redraw = true;
    
    return GRAPHICS_SUCCESS;
}
