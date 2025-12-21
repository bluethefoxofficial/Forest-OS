#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/graphics/display_driver.h"
#include "../include/graphics/font_renderer.h"
#include "../include/graphics_init.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"

// Graphics system state
static struct {
    bool initialized;
    graphics_device_t* primary_device;
    framebuffer_t* current_framebuffer;
    video_mode_t current_mode;
    bool double_buffering_enabled;
    void (*input_handler)(const input_event_t* event);
    int32_t cursor_x;
    int32_t cursor_y;
    bool framebuffer_console_active;
} graphics_state = {
    .initialized = false,
    .primary_device = NULL,
    .current_framebuffer = NULL,
    .double_buffering_enabled = false,
    .input_handler = NULL,
    .cursor_x = 0,
    .cursor_y = 0,
    .framebuffer_console_active = false
};

static uint32_t format_to_bpp(pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_TEXT_MODE:
            return 16;
        case PIXEL_FORMAT_INDEXED_8:
            return 8;
        case PIXEL_FORMAT_RGB_555:
        case PIXEL_FORMAT_RGB_565:
            return 16;
        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_BGR_888:
            return 24;
        case PIXEL_FORMAT_RGBA_8888:
        case PIXEL_FORMAT_BGRA_8888:
            return 32;
        default:
            return 32;
    }
}

static bool ensure_framebuffer_mapped(void) {
    if (graphics_state.current_framebuffer &&
        graphics_state.current_framebuffer->virtual_addr) {
        return true;
    }

    framebuffer_t* fb = NULL;
    graphics_result_t result = graphics_map_framebuffer(&fb);
    return (result == GRAPHICS_SUCCESS) && fb && fb->virtual_addr;
}

static void framebuffer_to_surface(framebuffer_t* fb, graphics_surface_t* surface) {
    if (!fb || !surface) {
        return;
    }

    surface->pixels = fb->virtual_addr;
    surface->width = fb->width;
    surface->height = fb->height;
    surface->pitch = fb->pitch;
    surface->format = fb->format;
    surface->bpp = fb->bpp;
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static graphics_color_t color_from_vga_index(uint8_t index) {
    static const graphics_color_t base_colors[8] = {
        {0,   0,   0,   255}, // Black
        {0,   0,   170, 255}, // Blue
        {0,   170, 0,   255}, // Green
        {0,   170, 170, 255}, // Cyan
        {170, 0,   0,   255}, // Red
        {170, 0,   170, 255}, // Magenta
        {170, 85,  0,   255}, // Brown/Yellowish
        {170, 170, 170, 255}  // Light gray
    };

    graphics_color_t color = base_colors[index & 0x07];
    if (index & 0x08) {
        color.r = min_u32(color.r + 85, 255);
        color.g = min_u32(color.g + 85, 255);
        color.b = min_u32(color.b + 85, 255);
    }
    return color;
}

static bool parse_bool_config(const char* value, bool default_value) {
    if (!value) {
        return default_value;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
        strcmp(value, "TRUE") == 0 || strcmp(value, "on") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "FALSE") == 0 || strcmp(value, "off") == 0) {
        return false;
    }
    return default_value;
}

static void copy_string(char* dest, size_t size, const char* src) {
    if (!dest || !src || size == 0) {
        return;
    }
    size_t i;
    for (i = 0; i + 1 < size && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static graphics_result_t ensure_font_ready(void);

static void select_minimum_console_dimensions(uint32_t cols, uint32_t rows,
                                              uint32_t* out_width,
                                              uint32_t* out_height) {
    uint32_t char_w = 8;
    uint32_t char_h = 16;

    if (ensure_font_ready() == GRAPHICS_SUCCESS) {
        font_t* sys_font = NULL;
        if (font_get_system_font(&sys_font) == GRAPHICS_SUCCESS && sys_font) {
            if (sys_font->fixed_width > 0) {
                char_w = sys_font->fixed_width;
            }
            if (sys_font->metrics.height > 0) {
                char_h = sys_font->metrics.height;
            }
        }
    }

    uint32_t min_width = (cols > 0) ? cols * char_w : 0;
    uint32_t min_height = (rows > 0) ? rows * char_h : 0;

    if (min_width < 800) {
        min_width = 800;
    }
    if (min_height < 600) {
        min_height = 600;
    }

    if (out_width) {
        *out_width = min_width;
    }
    if (out_height) {
        *out_height = min_height;
    }
}

static graphics_result_t pick_framebuffer_console_mode(uint32_t min_width,
                                                       uint32_t min_height,
                                                       video_mode_t* out_mode) {
    if (!out_mode) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    video_mode_t* modes = NULL;
    uint32_t count = 0;
    graphics_result_t result = graphics_enumerate_modes(&modes, &count);

    if (result != GRAPHICS_SUCCESS || !modes || count == 0) {
        out_mode->width = min_width;
        out_mode->height = min_height;
        out_mode->bpp = 32;
        out_mode->format = PIXEL_FORMAT_RGBA_8888;
        out_mode->refresh_rate = 60;
        out_mode->pitch = min_width * 4;
        out_mode->is_text_mode = false;
        out_mode->mode_number = 0;
        out_mode->hw_data = NULL;
        return GRAPHICS_SUCCESS;
    }

    bool found = false;
    video_mode_t best = {0};
    uint64_t best_score = 0;

    for (uint32_t i = 0; i < count; i++) {
        video_mode_t* candidate = &modes[i];
        if (candidate->is_text_mode) {
            continue;
        }

        uint64_t area = (uint64_t)candidate->width * (uint64_t)candidate->height;
        uint64_t score = ((uint64_t)candidate->bpp) * area;

        if (candidate->width < min_width || candidate->height < min_height) {
            score /= 2;
        }

        if (!found || score > best_score) {
            best = *candidate;
            best_score = score;
            found = true;
        }
    }

    if (!found) {
        best.width = min_width;
        best.height = min_height;
        best.bpp = 32;
        best.format = PIXEL_FORMAT_RGBA_8888;
        best.refresh_rate = 60;
        best.pitch = min_width * 4;
        best.is_text_mode = false;
        best.mode_number = 0;
        best.hw_data = NULL;
    }

    *out_mode = best;
    kfree(modes);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t activate_framebuffer_console(uint32_t cols, uint32_t rows) {
    uint32_t min_width = 0;
    uint32_t min_height = 0;
    select_minimum_console_dimensions(cols, rows, &min_width, &min_height);

    video_mode_t preferred_mode;
    graphics_result_t result = pick_framebuffer_console_mode(min_width, min_height, &preferred_mode);
    if (result != GRAPHICS_SUCCESS) {
        preferred_mode.width = min_width;
        preferred_mode.height = min_height;
        preferred_mode.bpp = 32;
        preferred_mode.format = PIXEL_FORMAT_RGBA_8888;
        preferred_mode.refresh_rate = 60;
        preferred_mode.pitch = min_width * 4;
        preferred_mode.is_text_mode = false;
        preferred_mode.mode_number = 0;
        preferred_mode.hw_data = NULL;
    }

    if (preferred_mode.refresh_rate == 0) {
        preferred_mode.refresh_rate = 60;
    }

    result = graphics_set_mode(preferred_mode.width,
                               preferred_mode.height,
                               preferred_mode.bpp,
                               preferred_mode.refresh_rate);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR,
                 "Graphics: framebuffer console mode %ux%u@%u failed (%d)\n",
                 preferred_mode.width, preferred_mode.height,
                 preferred_mode.bpp, result);
        return result;
    }

    if (!ensure_framebuffer_mapped()) {
        debuglog(DEBUG_ERROR, "Graphics: unable to map framebuffer for console\n");
        return GRAPHICS_ERROR_GENERIC;
    }

    if (ensure_font_ready() != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Graphics: system font unavailable for framebuffer console\n");
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    graphics_state.framebuffer_console_active = true;
    debuglog(DEBUG_INFO,
             "Graphics: enabled framebuffer text console at %ux%u (%u bpp)\n",
             graphics_state.current_mode.width,
             graphics_state.current_mode.height,
             graphics_state.current_mode.bpp);
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_init(void) {
    debuglog(DEBUG_INFO, "Initializing graphics subsystem...\n");
    
    if (graphics_state.initialized) {
        debuglog(DEBUG_WARN, "Graphics subsystem already initialized\n");
        return GRAPHICS_SUCCESS;
    }
    
    // Initialize drivers first  
    debuglog(DEBUG_INFO, "Registering graphics drivers for framebuffer TTY...\n");
    extern graphics_result_t vesa_init(void);
    extern graphics_result_t bga_init(void);
    
    // Register Bochs BGA driver for emulated environments
    graphics_result_t bga_result = bga_init();
    if (bga_result == GRAPHICS_SUCCESS) {
        debuglog(DEBUG_INFO, "Successfully registered Bochs BGA driver\n");
    } else {
        debuglog(DEBUG_ERROR, "Failed to register Bochs BGA driver\n");
    }
    
    // Register VESA driver as fallback
    graphics_result_t vesa_result = vesa_init();
    if (vesa_result == GRAPHICS_SUCCESS) {
        debuglog(DEBUG_INFO, "Successfully registered VESA driver\n");
    } else {
        debuglog(DEBUG_ERROR, "Failed to register VESA driver\n");
    }
    
    // Detect graphics hardware
    graphics_result_t result = detect_graphics_hardware();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Graphics hardware detection failed\n");
        return result;
    }
    
    // Get primary graphics device
    graphics_state.primary_device = graphics_hw_state.primary_device;
    if (!graphics_state.primary_device) {
        debuglog(DEBUG_ERROR, "No primary graphics device found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Load driver for primary device
    result = load_driver_for_device(graphics_state.primary_device);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to load specific driver, trying fallbacks\n");
        
        // Try VESA fallback
        result = fallback_to_vesa_driver(graphics_state.primary_device);
        if (result != GRAPHICS_SUCCESS) {
            // Ultimate fallback to text mode
            result = fallback_to_text_driver(graphics_state.primary_device);
            if (result != GRAPHICS_SUCCESS) {
                debuglog(DEBUG_ERROR, "All driver loading attempts failed\n");
                return result;
            }
        }
    }
    
    // Initialize the loaded driver
    if (graphics_state.primary_device->driver && 
        graphics_state.primary_device->driver->ops->initialize) {
        result = graphics_state.primary_device->driver->ops->initialize(graphics_state.primary_device);
        if (result != GRAPHICS_SUCCESS) {
            debuglog(DEBUG_ERROR, "Driver initialization failed\n");
            return result;
        }
    }
    
    // Set default text mode (80x25)
    result = graphics_set_text_mode(80, 25);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to set default text mode\n");
    }
    
    graphics_state.initialized = true;
    debuglog(DEBUG_INFO, "Graphics subsystem initialized successfully\n");
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_shutdown(void) {
    if (!graphics_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "Shutting down graphics subsystem...\n");
    
    // Shutdown primary device driver
    if (graphics_state.primary_device && 
        graphics_state.primary_device->driver &&
        graphics_state.primary_device->driver->ops->shutdown) {
        graphics_state.primary_device->driver->ops->shutdown(graphics_state.primary_device);
    }
    
    // Clean up framebuffer
    if (graphics_state.current_framebuffer) {
        graphics_unmap_framebuffer(graphics_state.current_framebuffer);
        graphics_state.current_framebuffer = NULL;
    }
    
    graphics_state.initialized = false;
    graphics_state.primary_device = NULL;
    graphics_state.input_handler = NULL;
    graphics_state.cursor_x = 0;
    graphics_state.cursor_y = 0;
    graphics_state.framebuffer_console_active = false;
    
    debuglog(DEBUG_INFO, "Graphics subsystem shutdown complete\n");
    return GRAPHICS_SUCCESS;
}

bool graphics_is_initialized(void) {
    return graphics_state.initialized;
}

graphics_device_t* graphics_get_primary_device(void) {
    return graphics_state.primary_device;
}

graphics_result_t graphics_get_capabilities(graphics_capabilities_t* caps) {
    if (!caps || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    *caps = graphics_state.primary_device->caps;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_set_primary_device(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    graphics_state.primary_device = device;
    return GRAPHICS_SUCCESS;
}

graphics_device_t* graphics_get_device(uint32_t index) {
    if (index >= graphics_hw_state.num_devices) {
        return NULL;
    }
    
    // For simplicity, assume devices array (in practice use proper indexing)
    return &graphics_hw_state.devices[index];
}

uint32_t graphics_get_device_count(void) {
    return graphics_hw_state.num_devices;
}

graphics_result_t graphics_set_mode(uint32_t width, uint32_t height, 
                                  uint32_t bpp, uint32_t refresh_rate) {
    if (!graphics_state.initialized || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->set_mode) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Create mode structure
    video_mode_t mode = {
        .width = width,
        .height = height,
        .bpp = bpp,
        .refresh_rate = refresh_rate,
        .is_text_mode = false
    };
    
    // Set pixel format based on bpp
    switch (bpp) {
        case 8:  mode.format = PIXEL_FORMAT_INDEXED_8; break;
        case 15: mode.format = PIXEL_FORMAT_RGB_555; break;
        case 16: mode.format = PIXEL_FORMAT_RGB_565; break;
        case 24: mode.format = PIXEL_FORMAT_RGB_888; break;
        case 32: mode.format = PIXEL_FORMAT_RGBA_8888; break;
        default: return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    mode.pitch = (width * bpp) / 8;
    
    graphics_result_t result = driver->ops->set_mode(graphics_state.primary_device, &mode);
    if (result == GRAPHICS_SUCCESS) {
        graphics_state.current_mode = mode;
    }
    
    return result;
}

graphics_result_t graphics_set_text_mode(uint32_t cols, uint32_t rows) {
    if (!graphics_state.initialized || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    bool driver_supports_text = driver && (driver->flags & DRIVER_FLAG_SUPPORTS_TEXT_MODE) &&
                                driver->ops && driver->ops->set_mode;

    if (driver_supports_text) {
        video_mode_t mode = {
            .width = cols,
            .height = rows,
            .bpp = 4, // 4 bits per character attribute
            .format = PIXEL_FORMAT_TEXT_MODE,
            .refresh_rate = 60,
            .is_text_mode = true,
            .pitch = cols * 2 // 2 bytes per character (char + attribute)
        };

        graphics_result_t result = driver->ops->set_mode(graphics_state.primary_device, &mode);
        if (result == GRAPHICS_SUCCESS) {
            graphics_state.current_mode = mode;
            graphics_state.framebuffer_console_active = false;
            return GRAPHICS_SUCCESS;
        }

        debuglog(DEBUG_WARN,
                 "Graphics: hardware text mode rejected (%d), falling back to framebuffer console\n",
                 result);
    } else {
        debuglog(DEBUG_INFO, "Graphics: driver lacks hardware text mode, using framebuffer console\n");
    }

    return activate_framebuffer_console(cols, rows);
}

graphics_result_t graphics_get_current_mode(video_mode_t* mode) {
    if (!mode) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!graphics_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    *mode = graphics_state.current_mode;
    return GRAPHICS_SUCCESS;
}

framebuffer_t* graphics_get_framebuffer(void) {
    return graphics_state.current_framebuffer;
}

graphics_result_t graphics_enumerate_modes(video_mode_t** modes, uint32_t* count) {
    if (!modes || !count || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->enumerate_modes) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    return driver->ops->enumerate_modes(graphics_state.primary_device, modes, count);
}

graphics_result_t graphics_map_framebuffer(framebuffer_t** fb) {
    if (!fb || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->map_framebuffer) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->ops->map_framebuffer(graphics_state.primary_device, fb);
    if (result == GRAPHICS_SUCCESS) {
        graphics_state.current_framebuffer = *fb;
    }
    
    return result;
}

graphics_result_t graphics_unmap_framebuffer(framebuffer_t* fb) {
    if (!fb || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->unmap_framebuffer) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    graphics_result_t result = driver->ops->unmap_framebuffer(graphics_state.primary_device, fb);
    if (result == GRAPHICS_SUCCESS && graphics_state.current_framebuffer == fb) {
        graphics_state.current_framebuffer = NULL;
    }
    return result;
}

graphics_result_t graphics_clear_screen(graphics_color_t color) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->clear_screen) {
        if (!ensure_framebuffer_mapped()) {
            return GRAPHICS_ERROR_NOT_SUPPORTED;
        }

        framebuffer_t* fb = graphics_state.current_framebuffer;
        if (!fb || !fb->virtual_addr) {
            return GRAPHICS_ERROR_GENERIC;
        }

        uint32_t pixel = graphics_color_to_pixel(color, fb->format);
        uint32_t bytes_per_pixel = fb->bpp / 8;
        uint8_t* row = (uint8_t*)fb->virtual_addr;
        for (uint32_t y = 0; y < fb->height; y++) {
            uint8_t* dst = row + y * fb->pitch;
            for (uint32_t x = 0; x < fb->width; x++) {
                memcpy(dst + x * bytes_per_pixel, &pixel, bytes_per_pixel);
            }
        }
        return GRAPHICS_SUCCESS;
    }

    return driver->ops->clear_screen(graphics_state.primary_device, color);
}

graphics_result_t graphics_write_char(int32_t x, int32_t y, char c, uint8_t attr) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->write_char) {
        if (!ensure_framebuffer_mapped()) {
            return GRAPHICS_ERROR_NOT_SUPPORTED;
        }

        framebuffer_t* fb = graphics_state.current_framebuffer;
        if (!fb || !fb->virtual_addr) {
            return GRAPHICS_ERROR_GENERIC;
        }

        // Basic fallback: write character bitmap using font renderer
        graphics_surface_t surface;
        framebuffer_to_surface(fb, &surface);
        if (ensure_font_ready() != GRAPHICS_SUCCESS) {
            return GRAPHICS_ERROR_NOT_SUPPORTED;
        }
        font_t* system_font = NULL;
        if (font_get_system_font(&system_font) != GRAPHICS_SUCCESS || !system_font) {
            return GRAPHICS_ERROR_NOT_SUPPORTED;
        }

        text_style_t style = DEFAULT_TEXT_STYLE;
        style.foreground = color_from_vga_index(attr & 0x0F);
        style.background = color_from_vga_index((attr >> 4) & 0x07);
        style.has_background = true;
        return font_render_char(system_font, &surface, x * system_font->fixed_width,
                                y * system_font->metrics.height, (uint32_t)c, &style);
    }

    return driver->ops->write_char(graphics_state.primary_device, x, y, c, attr);
}

graphics_result_t graphics_write_string(int32_t x, int32_t y, 
                                       const char* str, uint8_t attr) {
    if (!str || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    if (driver && driver->ops->write_string) {
        return driver->ops->write_string(graphics_state.primary_device, x, y, str, attr);
    }
    
    // Fallback: write character by character
    font_t* system_font = NULL;
        if (!driver || !driver->ops->write_char) {
            if (ensure_font_ready() != GRAPHICS_SUCCESS ||
                font_get_system_font(&system_font) != GRAPHICS_SUCCESS ||
                !system_font) {
                return GRAPHICS_ERROR_NOT_SUPPORTED;
            }
        }
    
    int32_t current_x = x;
    int32_t current_y = y;
    
    for (const char* p = str; *p; p++) {
        if (*p == '\n') {
            current_y++;
            current_x = x;
        } else if (*p == '\r') {
            current_x = x;
        } else {
            graphics_result_t result;
            if (driver && driver->ops->write_char) {
                result = driver->ops->write_char(
                    graphics_state.primary_device, current_x, current_y, *p, attr);
            } else {
                if (!ensure_framebuffer_mapped()) {
                    return GRAPHICS_ERROR_GENERIC;
                }

                graphics_surface_t surface;
                framebuffer_to_surface(graphics_state.current_framebuffer, &surface);
                text_style_t style = DEFAULT_TEXT_STYLE;
                style.foreground = color_from_vga_index(attr & 0x0F);
                style.background = color_from_vga_index((attr >> 4) & 0x07);
                style.has_background = true;
                int32_t px = current_x * system_font->fixed_width;
                int32_t py = current_y * system_font->metrics.height;
                result = font_render_char(system_font, &surface, px, py, (uint32_t)*p, &style);
            }
            if (result != GRAPHICS_SUCCESS) {
                return result;
            }
            current_x++;
        }
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_printf(int32_t x, int32_t y, uint8_t attr, 
                                 const char* format, ...) {
    // Simple implementation - in a full OS you'd use proper printf formatting
    return graphics_write_string(x, y, format, attr);
}

graphics_result_t graphics_draw_pixel(int32_t x, int32_t y, graphics_color_t color) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->draw_pixel) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    return driver->ops->draw_pixel(graphics_state.primary_device, x, y, color);
}

graphics_result_t graphics_draw_rect(const graphics_rect_t* rect, 
                                   graphics_color_t color, bool filled) {
    if (!rect || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (driver && driver->ops->draw_rect) {
        return driver->ops->draw_rect(graphics_state.primary_device, rect, color, filled);
    }

    // Fallback: draw using pixels
    if (!driver || !driver->ops->draw_pixel) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    for (uint32_t y = 0; y < rect->height; y++) {
        for (uint32_t x = 0; x < rect->width; x++) {
            if (!filled && y > 0 && y < rect->height - 1 &&
                x > 0 && x < rect->width - 1) {
                continue;
            }
            driver->ops->draw_pixel(graphics_state.primary_device,
                rect->x + x, rect->y + y, color);
        }
    }

    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_draw_line(int32_t x1, int32_t y1, 
                                   int32_t x2, int32_t y2, 
                                   graphics_color_t color) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->draw_pixel) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    // Bresenham algorithm
    int32_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = (dx > dy ? dx : -dy) / 2;

    while (true) {
        driver->ops->draw_pixel(graphics_state.primary_device, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int32_t e2 = err;
        if (e2 > -dx) { err -= dy; x1 += sx; }
        if (e2 < dy) { err += dx; y1 += sy; }
    }

    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_blit_surface(const graphics_surface_t* surface,
                                      const graphics_rect_t* src_rect,
                                      int32_t dst_x, int32_t dst_y) {
    if (!surface || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (driver && driver->ops->blit_surface) {
        return driver->ops->blit_surface(graphics_state.primary_device, surface,
                                         src_rect, dst_x, dst_y);
    }

    if (!ensure_framebuffer_mapped()) {
        return GRAPHICS_ERROR_GENERIC;
    }

    framebuffer_t* fb = graphics_state.current_framebuffer;
    graphics_surface_t dest;
    framebuffer_to_surface(fb, &dest);

    if (surface->format != dest.format || surface->bpp != dest.bpp) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    uint32_t start_x = src_rect ? src_rect->x : 0;
    uint32_t start_y = src_rect ? src_rect->y : 0;
    uint32_t copy_w = src_rect ? src_rect->width : surface->width;
    uint32_t copy_h = src_rect ? src_rect->height : surface->height;

    if (start_x >= surface->width || start_y >= surface->height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    copy_w = min_u32(copy_w, surface->width - start_x);
    copy_h = min_u32(copy_h, surface->height - start_y);

    if (dst_x < 0) {
        uint32_t offset = min_u32((uint32_t)(-dst_x), copy_w);
        start_x += offset;
        copy_w -= offset;
        dst_x = 0;
    }
    if (dst_y < 0) {
        uint32_t offset = min_u32((uint32_t)(-dst_y), copy_h);
        start_y += offset;
        copy_h -= offset;
        dst_y = 0;
    }
    if (copy_w == 0 || copy_h == 0) {
        return GRAPHICS_SUCCESS;
    }

    uint32_t bytes_per_pixel = surface->bpp / 8;
    copy_w = min_u32(copy_w, dest.width > (uint32_t)dst_x ? dest.width - (uint32_t)dst_x : 0);
    copy_h = min_u32(copy_h, dest.height > (uint32_t)dst_y ? dest.height - (uint32_t)dst_y : 0);
    if (copy_w == 0 || copy_h == 0) {
        return GRAPHICS_SUCCESS;
    }

    for (uint32_t y = 0; y < copy_h; y++) {
        uint8_t* src_row = (uint8_t*)surface->pixels + (start_y + y) * surface->pitch + start_x * bytes_per_pixel;
        uint8_t* dst_row = (uint8_t*)dest.pixels + (dst_y + y) * dest.pitch + dst_x * bytes_per_pixel;
        memcpy(dst_row, src_row, copy_w * bytes_per_pixel);
    }

    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_create_surface(uint32_t width, uint32_t height,
                                         pixel_format_t format,
                                         graphics_surface_t** surface) {
    if (!surface || width == 0 || height == 0) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    graphics_surface_t* surf = kmalloc(sizeof(graphics_surface_t));
    if (!surf) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    memset(surf, 0, sizeof(graphics_surface_t));

    uint32_t bpp = format_to_bpp(format);
    if (bpp == 0) {
        kfree(surf);
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    uint32_t bytes_per_pixel = bpp / 8;
    uint32_t pitch = width * bytes_per_pixel;
    size_t buffer_size = (size_t)pitch * height;

    void* pixels = kmalloc(buffer_size);
    if (!pixels) {
        kfree(surf);
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    memset(pixels, 0, buffer_size);

    surf->pixels = pixels;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->format = format;
    surf->bpp = bpp;

    *surface = surf;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_destroy_surface(graphics_surface_t* surface) {
    if (!surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    if (surface->pixels) {
        kfree(surface->pixels);
        surface->pixels = NULL;
    }
    kfree(surface);
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_copy_surface(const graphics_surface_t* src,
                                        graphics_surface_t* dst) {
    if (!src || !dst) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    if (src->format != dst->format) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    uint32_t rows = min_u32(src->height, dst->height);
    uint32_t bytes_per_row = min_u32(src->pitch, dst->pitch);
    for (uint32_t y = 0; y < rows; y++) {
        const uint8_t* src_row = (const uint8_t*)src->pixels + y * src->pitch;
        uint8_t* dst_row = (uint8_t*)dst->pixels + y * dst->pitch;
        memcpy(dst_row, src_row, bytes_per_row);
    }

    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_enable_double_buffering(bool enable) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    if (graphics_state.double_buffering_enabled == enable) {
        return GRAPHICS_SUCCESS;
    }

    if (!ensure_framebuffer_mapped()) {
        return GRAPHICS_ERROR_GENERIC;
    }

    framebuffer_t* fb = graphics_state.current_framebuffer;
    if (!fb) {
        return GRAPHICS_ERROR_GENERIC;
    }

    if (enable) {
        if (!fb->back_buffer) {
            size_t buffer_size = fb->size ? fb->size : (size_t)fb->pitch * fb->height;
            fb->back_buffer = kmalloc(buffer_size);
            if (!fb->back_buffer) {
                return GRAPHICS_ERROR_OUT_OF_MEMORY;
            }
            memset(fb->back_buffer, 0, buffer_size);
        }
        fb->double_buffered = true;
        graphics_state.double_buffering_enabled = true;
        return GRAPHICS_SUCCESS;
    }

    if (fb->back_buffer) {
        kfree(fb->back_buffer);
        fb->back_buffer = NULL;
    }
    fb->double_buffered = false;
    graphics_state.double_buffering_enabled = false;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_swap_buffers(void) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    if (!graphics_state.double_buffering_enabled) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    if (!ensure_framebuffer_mapped()) {
        return GRAPHICS_ERROR_GENERIC;
    }

    framebuffer_t* fb = graphics_state.current_framebuffer;
    if (!fb || !fb->back_buffer) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (driver && driver->ops->page_flip) {
        return driver->ops->page_flip(graphics_state.primary_device, fb);
    }

    size_t buffer_size = fb->size ? fb->size : (size_t)fb->pitch * fb->height;
    memcpy(fb->virtual_addr, fb->back_buffer, buffer_size);
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_wait_for_vsync(void) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->wait_for_vsync) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    return driver->ops->wait_for_vsync(graphics_state.primary_device);
}

graphics_result_t graphics_set_config(const char* key, const char* value) {
    if (!key) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    if (strcmp(key, "double_buffering") == 0) {
        bool enable = parse_bool_config(value, true);
        return graphics_enable_double_buffering(enable);
    }

    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_get_config(const char* key, char* value, size_t size) {
    if (!key || !value || size == 0) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    if (strcmp(key, "double_buffering") == 0) {
        copy_string(value, size, graphics_state.double_buffering_enabled ? "true" : "false");
        return GRAPHICS_SUCCESS;
    }

    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_set_cursor_pos(int32_t x, int32_t y) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->set_cursor_pos) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->ops->set_cursor_pos(graphics_state.primary_device, x, y);
    if (result == GRAPHICS_SUCCESS) {
        graphics_state.cursor_x = x;
        graphics_state.cursor_y = y;
    }
    return result;
}

graphics_result_t graphics_scroll_screen(int32_t lines) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->scroll_screen) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    return driver->ops->scroll_screen(graphics_state.primary_device, lines);
}

graphics_color_t graphics_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    graphics_color_t color = {r, g, b, a};
    return color;
}

uint32_t graphics_color_to_pixel(graphics_color_t color, pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_RGB_555:
            return ((color.r >> 3) << 10) | ((color.g >> 3) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_565:
            return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_BGR_888:
            return (color.r << 16) | (color.g << 8) | color.b;
        case PIXEL_FORMAT_RGBA_8888:
        case PIXEL_FORMAT_BGRA_8888:
            return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
        default:
            return (color.r << 16) | (color.g << 8) | color.b;
    }
}

graphics_color_t graphics_pixel_to_color(uint32_t pixel, pixel_format_t format) {
    graphics_color_t color = {0, 0, 0, 255};
    switch (format) {
        case PIXEL_FORMAT_RGB_555:
            color.r = ((pixel >> 10) & 0x1F) << 3;
            color.g = ((pixel >> 5) & 0x1F) << 3;
            color.b = (pixel & 0x1F) << 3;
            break;
        case PIXEL_FORMAT_RGB_565:
            color.r = ((pixel >> 11) & 0x1F) << 3;
            color.g = ((pixel >> 5) & 0x3F) << 2;
            color.b = (pixel & 0x1F) << 3;
            break;
        case PIXEL_FORMAT_RGB_888:
        case PIXEL_FORMAT_BGR_888:
            color.r = (pixel >> 16) & 0xFF;
            color.g = (pixel >> 8) & 0xFF;
            color.b = pixel & 0xFF;
            break;
        case PIXEL_FORMAT_RGBA_8888:
        case PIXEL_FORMAT_BGRA_8888:
            color.a = (pixel >> 24) & 0xFF;
            color.r = (pixel >> 16) & 0xFF;
            color.g = (pixel >> 8) & 0xFF;
            color.b = pixel & 0xFF;
            break;
        default:
            color.r = (pixel >> 16) & 0xFF;
            color.g = (pixel >> 8) & 0xFF;
            color.b = pixel & 0xFF;
            break;
    }
    return color;
}

static graphics_result_t ensure_font_ready(void) {
    if (font_renderer_is_initialized()) {
        return GRAPHICS_SUCCESS;
    }
    return font_renderer_init();
}

graphics_result_t graphics_load_font(const char* name, uint8_t size, font_t** font) {
    if (!font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    graphics_result_t result = ensure_font_ready();
    if (result != GRAPHICS_SUCCESS) {
        return result;
    }

    if (!name) {
        return font_get_system_font(font);
    }

    result = font_load_builtin(name, size, font);
    if (result == GRAPHICS_SUCCESS) {
        return GRAPHICS_SUCCESS;
    }

    return font_get_system_font(font);
}

graphics_result_t graphics_unload_font(font_t* font) {
    if (!font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    return font_unload(font);
}

graphics_result_t graphics_draw_text(int32_t x, int32_t y, const char* text,
                                     font_t* font, graphics_color_t color) {
    if (!text) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    graphics_result_t result = ensure_font_ready();
    if (result != GRAPHICS_SUCCESS) {
        return result;
    }

    font_t* use_font = font;
    if (!use_font) {
        result = font_get_system_font(&use_font);
        if (result != GRAPHICS_SUCCESS || !use_font) {
            return result;
        }
    }

    if (!ensure_framebuffer_mapped()) {
        return GRAPHICS_ERROR_GENERIC;
    }

    graphics_surface_t surface;
    framebuffer_to_surface(graphics_state.current_framebuffer, &surface);

    text_style_t style = DEFAULT_TEXT_STYLE;
    style.foreground = color;
    style.has_background = false;

    return font_render_text(use_font, &surface, x, y, text, &style);
}

graphics_result_t graphics_get_text_bounds(const char* text, font_t* font,
                                           uint32_t* width, uint32_t* height) {
    if (!text || !width || !height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    graphics_result_t result = ensure_font_ready();
    if (result != GRAPHICS_SUCCESS) {
        return result;
    }

    font_t* use_font = font;
    if (!use_font) {
        result = font_get_system_font(&use_font);
        if (result != GRAPHICS_SUCCESS || !use_font) {
            return result;
        }
    }

    return font_measure_text(use_font, text, width, height);
}

graphics_result_t graphics_register_input_handler(
    void (*handler)(const input_event_t* event)) {
    
    if (!handler) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    graphics_state.input_handler = handler;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_set_cursor(const graphics_surface_t* cursor_surface,
                                     int32_t hotspot_x, int32_t hotspot_y) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->set_cursor) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    return driver->ops->set_cursor(graphics_state.primary_device, cursor_surface,
                                   hotspot_x, hotspot_y);
}

graphics_result_t graphics_move_cursor(int32_t x, int32_t y) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->move_cursor) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    graphics_result_t result = driver->ops->move_cursor(graphics_state.primary_device, x, y);
    if (result == GRAPHICS_SUCCESS) {
        graphics_state.cursor_x = x;
        graphics_state.cursor_y = y;
    }
    return result;
}

graphics_result_t graphics_show_cursor(bool show) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    display_driver_t* driver = graphics_state.primary_device->driver;
    if (!driver || !driver->ops->show_cursor) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    return driver->ops->show_cursor(graphics_state.primary_device, show);
}

graphics_result_t graphics_get_cursor_pos(int32_t* x, int32_t* y) {
    if (!x || !y || !graphics_state.primary_device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    *x = graphics_state.cursor_x;
    *y = graphics_state.cursor_y;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_unregister_input_handler(void) {
    graphics_state.input_handler = NULL;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_inject_input_event(const input_event_t* event) {
    if (!event) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (graphics_state.input_handler) {
        graphics_state.input_handler(event);
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_dump_device_info(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    debuglog(DEBUG_INFO, "[Graphics] Device %s (vendor=%04x device=%04x) bus=%u slot=%u func=%u\n",
             device->name, device->vendor_id, device->device_id,
             device->bus, device->slot, device->function);
    debuglog(DEBUG_INFO, "[Graphics] Resolution capability %ux%u, video memory %u KB\n",
             device->caps.max_resolution_x, device->caps.max_resolution_y,
             device->caps.video_memory_size / 1024);
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_run_diagnostics(void) {
    if (!graphics_state.primary_device) {
        return GRAPHICS_ERROR_GENERIC;
    }

    graphics_dump_device_info(graphics_state.primary_device);

    video_mode_t mode;
    if (graphics_get_current_mode(&mode) == GRAPHICS_SUCCESS) {
        debuglog(DEBUG_INFO, "[Graphics] Current mode: %ux%u %u bpp%s\n",
                 mode.width, mode.height, mode.bpp,
                 mode.is_text_mode ? " text" : "");
    }

    // Simple smoke test: clear screen if possible
    graphics_color_t test_color = COLOR_DARK_GRAY;
    graphics_clear_screen(test_color);
    return GRAPHICS_SUCCESS;
}

const char* graphics_get_error_string(graphics_result_t error) {
    switch (error) {
        case GRAPHICS_SUCCESS:
            return "Success";
        case GRAPHICS_ERROR_GENERIC:
            return "Generic error";
        case GRAPHICS_ERROR_INVALID_MODE:
            return "Invalid video mode";
        case GRAPHICS_ERROR_HARDWARE_FAULT:
            return "Hardware fault";
        case GRAPHICS_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case GRAPHICS_ERROR_NOT_SUPPORTED:
            return "Operation not supported";
        case GRAPHICS_ERROR_DEVICE_BUSY:
            return "Device busy";
        case GRAPHICS_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        default:
            return "Unknown error";
    }
}
