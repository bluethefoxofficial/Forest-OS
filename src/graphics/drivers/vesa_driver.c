#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/debuglog.h"

// VESA BIOS Extension constants
#define VESA_SIGNATURE          0x4F
#define VESA_SUCCESS            0x00
#define VESA_GET_INFO           0x4F00
#define VESA_GET_MODE_INFO      0x4F01
#define VESA_SET_MODE           0x4F02
#define VESA_GET_CURRENT_MODE   0x4F03

// VESA mode attributes
#define VESA_MODE_SUPPORTED     (1 << 0)
#define VESA_MODE_COLOR         (1 << 4)
#define VESA_MODE_GRAPHICS      (1 << 5)
#define VESA_MODE_LINEAR        (1 << 7)

// Common VESA modes
typedef struct {
    uint16_t mode_number;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    const char* description;
} vesa_mode_info_t;

static const vesa_mode_info_t standard_vesa_modes[] = {
    {0x101, 640, 480, 8, "640x480x8"},
    {0x103, 800, 600, 8, "800x600x8"},
    {0x105, 1024, 768, 8, "1024x768x8"},
    {0x110, 640, 480, 15, "640x480x15"},
    {0x111, 640, 480, 16, "640x480x16"},
    {0x112, 640, 480, 24, "640x480x24"},
    {0x113, 800, 600, 15, "800x600x15"},
    {0x114, 800, 600, 16, "800x600x16"},
    {0x115, 800, 600, 24, "800x600x24"},
    {0x117, 1024, 768, 16, "1024x768x16"},
    {0x118, 1024, 768, 24, "1024x768x24"},
    {0x11A, 1280, 1024, 15, "1280x1024x15"},
    {0x11B, 1280, 1024, 16, "1280x1024x16"},
};

#define NUM_STANDARD_MODES (sizeof(standard_vesa_modes) / sizeof(standard_vesa_modes[0]))

// Driver state
static struct {
    bool initialized;
    uint16_t current_mode;
    void* framebuffer;
    uint32_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    pixel_format_t format;
    graphics_device_t* device;
} vesa_state = {
    .initialized = false,
    .current_mode = 0,
    .framebuffer = NULL,
    .framebuffer_size = 0,
    .device = NULL
};

// BIOS interrupt structure for real mode calls
typedef struct {
    uint16_t ax, bx, cx, dx;
    uint16_t si, di;
    uint16_t es, ds;
} bios_regs_t;

// Driver operation implementations
static graphics_result_t vesa_initialize(graphics_device_t* device);
static graphics_result_t vesa_shutdown(graphics_device_t* device);
static graphics_result_t vesa_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t vesa_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t vesa_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t vesa_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t vesa_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t vesa_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color);
static graphics_result_t vesa_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled);

// Helper functions
static bool call_vesa_bios(bios_regs_t* regs);
static pixel_format_t bpp_to_pixel_format(uint8_t bpp);
static uint32_t color_to_pixel(graphics_color_t color, pixel_format_t format);
static void put_pixel(int32_t x, int32_t y, uint32_t pixel_value);

// VESA driver operations structure
static display_driver_ops_t vesa_ops = {
    .name = "vesa",
    .version = 1,
    .initialize = vesa_initialize,
    .shutdown = vesa_shutdown,
    .reset = NULL,
    .enumerate_modes = vesa_enumerate_modes,
    .set_mode = vesa_set_mode,
    .get_current_mode = vesa_get_current_mode,
    .map_framebuffer = vesa_map_framebuffer,
    .unmap_framebuffer = NULL,
    .clear_screen = vesa_clear_screen,
    .draw_pixel = vesa_draw_pixel,
    .draw_rect = vesa_draw_rect,
    .blit_surface = NULL,
    .set_cursor = NULL,
    .move_cursor = NULL,
    .show_cursor = NULL,
    .write_char = NULL,
    .write_string = NULL,
    .scroll_screen = NULL,
    .set_cursor_pos = NULL,
    .set_power_state = NULL,
    .hw_fill_rect = NULL,
    .hw_copy_rect = NULL,
    .hw_line = NULL,
    .wait_for_vsync = NULL,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = NULL
};

// Driver structure
DECLARE_DISPLAY_DRIVER(vesa, vesa_ops);

static graphics_result_t vesa_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing VESA driver\n");
    
    // Check if VESA is supported by making a BIOS call
    bios_regs_t regs = {0};
    regs.ax = VESA_GET_INFO;
    
    // Note: In a real implementation, you would need to set up real mode
    // calls or use BIOS emulation. For this example, we'll assume VESA is available
    // and set up a basic 1024x768x16 mode
    
    vesa_state.device = device;
    vesa_state.initialized = true;
    
    // Set default capabilities
    device->caps.supports_2d_accel = false;
    device->caps.supports_3d_accel = false;
    device->caps.supports_hw_cursor = false;
    device->caps.supports_page_flipping = false;
    device->caps.supports_vsync = false;
    device->caps.max_resolution_x = 1280;
    device->caps.max_resolution_y = 1024;
    device->caps.video_memory_size = 16 * 1024 * 1024; // Assume 16MB VRAM
    
    debuglog(DEBUG_INFO, "VESA driver initialized successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_shutdown(graphics_device_t* device) {
    debuglog(DEBUG_INFO, "Shutting down VESA driver\n");
    
    vesa_state.initialized = false;
    vesa_state.device = NULL;
    vesa_state.framebuffer = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = NUM_STANDARD_MODES;
    *modes = kmalloc(sizeof(video_mode_t) * NUM_STANDARD_MODES);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // Convert standard VESA modes to video_mode_t structures
    for (uint32_t i = 0; i < NUM_STANDARD_MODES; i++) {
        (*modes)[i].width = standard_vesa_modes[i].width;
        (*modes)[i].height = standard_vesa_modes[i].height;
        (*modes)[i].bpp = standard_vesa_modes[i].bpp;
        (*modes)[i].format = bpp_to_pixel_format(standard_vesa_modes[i].bpp);
        (*modes)[i].refresh_rate = 60;
        (*modes)[i].is_text_mode = false;
        (*modes)[i].pitch = (standard_vesa_modes[i].width * standard_vesa_modes[i].bpp) / 8;
        (*modes)[i].mode_number = standard_vesa_modes[i].mode_number;
        (*modes)[i].hw_data = NULL;
    }
    
    debuglog(DEBUG_INFO, "VESA enumerated %u modes\n", *count);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !vesa_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (mode->is_text_mode) {
        debuglog(DEBUG_WARN, "VESA driver doesn't support text modes\n");
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    debuglog(DEBUG_INFO, "Setting VESA mode: %ux%ux%u\n", mode->width, mode->height, mode->bpp);
    
    // Find the matching VESA mode
    uint16_t vesa_mode_num = 0;
    for (uint32_t i = 0; i < NUM_STANDARD_MODES; i++) {
        if (standard_vesa_modes[i].width == mode->width &&
            standard_vesa_modes[i].height == mode->height &&
            standard_vesa_modes[i].bpp == mode->bpp) {
            vesa_mode_num = standard_vesa_modes[i].mode_number;
            break;
        }
    }
    
    if (vesa_mode_num == 0) {
        debuglog(DEBUG_ERROR, "Unsupported VESA mode: %ux%ux%u\n", mode->width, mode->height, mode->bpp);
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // In a real implementation, you would call VESA BIOS here
    // For now, simulate mode setting
    vesa_state.current_mode = vesa_mode_num;
    vesa_state.width = mode->width;
    vesa_state.height = mode->height;
    vesa_state.bpp = mode->bpp;
    vesa_state.pitch = mode->pitch;
    vesa_state.format = mode->format;
    vesa_state.framebuffer_size = mode->pitch * mode->height;
    
    // Update device current mode
    device->current_mode = *mode;
    device->current_mode.mode_number = vesa_mode_num;
    
    debuglog(DEBUG_INFO, "VESA mode set successfully: %s\n", 
            standard_vesa_modes[0].description); // Simplified for example
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !vesa_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = vesa_state.width;
    mode->height = vesa_state.height;
    mode->bpp = vesa_state.bpp;
    mode->format = vesa_state.format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->pitch = vesa_state.pitch;
    mode->mode_number = vesa_state.current_mode;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !vesa_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t));
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // In a real implementation, you would get the framebuffer address from VESA
    // For this example, we'll use the device's framebuffer_base if available
    if (device->framebuffer_base) {
        framebuffer->physical_addr = device->framebuffer_base;
        framebuffer->virtual_addr = (void*)device->framebuffer_base; // Direct mapping for now
    } else {
        // Fallback address commonly used by graphics cards
        framebuffer->physical_addr = 0xE0000000;
        framebuffer->virtual_addr = (void*)0xE0000000;
    }
    
    framebuffer->size = vesa_state.framebuffer_size;
    framebuffer->width = vesa_state.width;
    framebuffer->height = vesa_state.height;
    framebuffer->pitch = vesa_state.pitch;
    framebuffer->format = vesa_state.format;
    framebuffer->bpp = vesa_state.bpp;
    framebuffer->double_buffered = false;
    framebuffer->back_buffer = NULL;
    framebuffer->hw_cursor_available = false;
    framebuffer->cursor_data = NULL;
    
    vesa_state.framebuffer = framebuffer->virtual_addr;
    *fb = framebuffer;
    
    debuglog(DEBUG_INFO, "VESA framebuffer mapped at 0x%08X, size: %u bytes\n",
            (uint32_t)framebuffer->physical_addr, framebuffer->size);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !vesa_state.initialized || !vesa_state.framebuffer) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    uint32_t pixel_value = color_to_pixel(color, vesa_state.format);
    uint32_t pixels_per_line = vesa_state.pitch / (vesa_state.bpp / 8);
    
    // Clear the entire framebuffer
    switch (vesa_state.bpp) {
        case 8: {
            uint8_t* fb = (uint8_t*)vesa_state.framebuffer;
            for (uint32_t y = 0; y < vesa_state.height; y++) {
                for (uint32_t x = 0; x < pixels_per_line; x++) {
                    fb[y * pixels_per_line + x] = (uint8_t)pixel_value;
                }
            }
            break;
        }
        case 15:
        case 16: {
            uint16_t* fb = (uint16_t*)vesa_state.framebuffer;
            for (uint32_t y = 0; y < vesa_state.height; y++) {
                for (uint32_t x = 0; x < vesa_state.width; x++) {
                    fb[y * (vesa_state.pitch / 2) + x] = (uint16_t)pixel_value;
                }
            }
            break;
        }
        case 24: {
            uint8_t* fb = (uint8_t*)vesa_state.framebuffer;
            for (uint32_t y = 0; y < vesa_state.height; y++) {
                for (uint32_t x = 0; x < vesa_state.width; x++) {
                    uint32_t offset = y * vesa_state.pitch + x * 3;
                    fb[offset] = pixel_value & 0xFF;
                    fb[offset + 1] = (pixel_value >> 8) & 0xFF;
                    fb[offset + 2] = (pixel_value >> 16) & 0xFF;
                }
            }
            break;
        }
        case 32: {
            uint32_t* fb = (uint32_t*)vesa_state.framebuffer;
            for (uint32_t y = 0; y < vesa_state.height; y++) {
                for (uint32_t x = 0; x < vesa_state.width; x++) {
                    fb[y * (vesa_state.pitch / 4) + x] = pixel_value;
                }
            }
            break;
        }
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color) {
    if (!device || !vesa_state.initialized || !vesa_state.framebuffer) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    if (x < 0 || x >= (int32_t)vesa_state.width || y < 0 || y >= (int32_t)vesa_state.height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = color_to_pixel(color, vesa_state.format);
    put_pixel(x, y, pixel_value);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vesa_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, 
                                       graphics_color_t color, bool filled) {
    if (!device || !rect || !vesa_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = color_to_pixel(color, vesa_state.format);
    
    if (filled) {
        // Draw filled rectangle
        for (uint32_t y = rect->y; y < rect->y + rect->height; y++) {
            for (uint32_t x = rect->x; x < rect->x + rect->width; x++) {
                if (x < vesa_state.width && y < vesa_state.height) {
                    put_pixel(x, y, pixel_value);
                }
            }
        }
    } else {
        // Draw rectangle outline
        // Top and bottom edges
        for (uint32_t x = rect->x; x < rect->x + rect->width; x++) {
            if (x < vesa_state.width) {
                if (rect->y < vesa_state.height) {
                    put_pixel(x, rect->y, pixel_value);
                }
                if (rect->y + rect->height - 1 < vesa_state.height) {
                    put_pixel(x, rect->y + rect->height - 1, pixel_value);
                }
            }
        }
        // Left and right edges
        for (uint32_t y = rect->y; y < rect->y + rect->height; y++) {
            if (y < vesa_state.height) {
                if (rect->x < vesa_state.width) {
                    put_pixel(rect->x, y, pixel_value);
                }
                if (rect->x + rect->width - 1 < vesa_state.width) {
                    put_pixel(rect->x + rect->width - 1, y, pixel_value);
                }
            }
        }
    }
    
    return GRAPHICS_SUCCESS;
}

// Helper function implementations
static bool call_vesa_bios(bios_regs_t* regs) {
    // In a real implementation, this would make a BIOS interrupt call
    // For this example, we'll simulate success
    return true;
}

static pixel_format_t bpp_to_pixel_format(uint8_t bpp) {
    switch (bpp) {
        case 8:  return PIXEL_FORMAT_INDEXED_8;
        case 15: return PIXEL_FORMAT_RGB_555;
        case 16: return PIXEL_FORMAT_RGB_565;
        case 24: return PIXEL_FORMAT_RGB_888;
        case 32: return PIXEL_FORMAT_RGBA_8888;
        default: return PIXEL_FORMAT_RGB_888;
    }
}

static uint32_t color_to_pixel(graphics_color_t color, pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_RGB_555:
            return ((color.r >> 3) << 10) | ((color.g >> 3) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_565:
            return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_888:
            return (color.r << 16) | (color.g << 8) | color.b;
        case PIXEL_FORMAT_RGBA_8888:
            return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
        default:
            return (color.r << 16) | (color.g << 8) | color.b;
    }
}

static void put_pixel(int32_t x, int32_t y, uint32_t pixel_value) {
    if (x < 0 || x >= (int32_t)vesa_state.width || y < 0 || y >= (int32_t)vesa_state.height) {
        return;
    }
    
    switch (vesa_state.bpp) {
        case 15:
        case 16: {
            uint16_t* fb = (uint16_t*)vesa_state.framebuffer;
            fb[y * (vesa_state.pitch / 2) + x] = (uint16_t)pixel_value;
            break;
        }
        case 24: {
            uint8_t* fb = (uint8_t*)vesa_state.framebuffer;
            uint32_t offset = y * vesa_state.pitch + x * 3;
            fb[offset] = pixel_value & 0xFF;
            fb[offset + 1] = (pixel_value >> 8) & 0xFF;
            fb[offset + 2] = (pixel_value >> 16) & 0xFF;
            break;
        }
        case 32: {
            uint32_t* fb = (uint32_t*)vesa_state.framebuffer;
            fb[y * (vesa_state.pitch / 4) + x] = pixel_value;
            break;
        }
    }
}

// Driver initialization function
DRIVER_INIT_FUNCTION(vesa) {
    debuglog(DEBUG_INFO, "Registering VESA driver\n");
    
    // Set driver flags
    vesa_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE;
    
    return register_display_driver(&vesa_driver);
}