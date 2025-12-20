#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/debuglog.h"
#include "../../include/io_ports.h"

// Bochs VBE Extension constants from the documentation
#define VBE_DISPI_IOPORT_INDEX      0x01CE
#define VBE_DISPI_IOPORT_DATA       0x01CF

// VBE index registers
#define VBE_DISPI_INDEX_ID          0
#define VBE_DISPI_INDEX_XRES        1
#define VBE_DISPI_INDEX_YRES        2
#define VBE_DISPI_INDEX_BPP         3
#define VBE_DISPI_INDEX_ENABLE      4
#define VBE_DISPI_INDEX_BANK        5
#define VBE_DISPI_INDEX_VIRT_WIDTH  6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET    8
#define VBE_DISPI_INDEX_Y_OFFSET    9

// VBE ID values for different versions
#define VBE_DISPI_ID0               0xB0C0
#define VBE_DISPI_ID1               0xB0C1
#define VBE_DISPI_ID2               0xB0C2
#define VBE_DISPI_ID3               0xB0C3
#define VBE_DISPI_ID4               0xB0C4
#define VBE_DISPI_ID5               0xB0C5

// Enable flags
#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_GETCAPS           0x02
#define VBE_DISPI_8BIT_DAC          0x20
#define VBE_DISPI_LFB_ENABLED       0x40
#define VBE_DISPI_NOCLEARMEM        0x80

// Supported bit depths
#define VBE_DISPI_BPP_4             0x04
#define VBE_DISPI_BPP_8             0x08
#define VBE_DISPI_BPP_15            0x0F
#define VBE_DISPI_BPP_16            0x10
#define VBE_DISPI_BPP_24            0x18
#define VBE_DISPI_BPP_32            0x20

// Maximum resolutions
#define VBE_DISPI_MAX_XRES          1600
#define VBE_DISPI_MAX_YRES          1200

// Banked mode constants
#define VBE_DISPI_BANK_ADDRESS      0xA0000
#define VBE_DISPI_BANK_SIZE_KB      64

// Standard video modes supported by Bochs BGA
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    const char* description;
} bga_mode_info_t;

static const bga_mode_info_t standard_bga_modes[] = {
    {640, 480, 8, "640x480x8"},
    {640, 480, 15, "640x480x15"},
    {640, 480, 16, "640x480x16"},
    {640, 480, 24, "640x480x24"},
    {640, 480, 32, "640x480x32"},
    {800, 600, 8, "800x600x8"},
    {800, 600, 15, "800x600x15"},
    {800, 600, 16, "800x600x16"},
    {800, 600, 24, "800x600x24"},
    {800, 600, 32, "800x600x32"},
    {1024, 768, 8, "1024x768x8"},
    {1024, 768, 15, "1024x768x15"},
    {1024, 768, 16, "1024x768x16"},
    {1024, 768, 24, "1024x768x24"},
    {1024, 768, 32, "1024x768x32"},
    {1280, 1024, 8, "1280x1024x8"},
    {1280, 1024, 15, "1280x1024x15"},
    {1280, 1024, 16, "1280x1024x16"},
    {1280, 1024, 24, "1280x1024x24"},
    {1280, 1024, 32, "1280x1024x32"},
};

#define NUM_STANDARD_BGA_MODES (sizeof(standard_bga_modes) / sizeof(standard_bga_modes[0]))

// Driver state
static struct {
    bool initialized;
    uint16_t bga_version;
    void* framebuffer;
    uintptr_t framebuffer_phys;
    size_t framebuffer_size;
    uint32_t current_width;
    uint32_t current_height;
    uint32_t current_bpp;
    pixel_format_t current_format;
    bool linear_fb_enabled;
    graphics_device_t* device;
} bga_state = {
    .initialized = false,
    .bga_version = 0,
    .framebuffer = NULL,
    .framebuffer_phys = 0,
    .framebuffer_size = 0,
    .linear_fb_enabled = false,
    .device = NULL
};

// Helper functions for BGA register access
static void bga_write_register(uint16_t index, uint16_t data);
static uint16_t bga_read_register(uint16_t index);
static bool bga_is_available(void);
static graphics_result_t bga_set_video_mode(uint32_t width, uint32_t height, uint32_t bpp, bool use_lfb, bool clear_memory);
static pixel_format_t bga_bpp_to_pixel_format(uint8_t bpp);
static uint32_t bga_color_to_pixel(graphics_color_t color, pixel_format_t format);
static void bga_put_pixel(int32_t x, int32_t y, uint32_t pixel_value);

// Driver operation implementations
static graphics_result_t bga_initialize(graphics_device_t* device);
static graphics_result_t bga_shutdown(graphics_device_t* device);
static graphics_result_t bga_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t bga_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t bga_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t bga_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t bga_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb);
static graphics_result_t bga_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t bga_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color);
static graphics_result_t bga_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled);

// BGA driver operations structure
static display_driver_ops_t bga_ops = {
    .name = "bochs_bga",
    .version = 1,
    .initialize = bga_initialize,
    .shutdown = bga_shutdown,
    .reset = NULL,
    .enumerate_modes = bga_enumerate_modes,
    .set_mode = bga_set_mode,
    .get_current_mode = bga_get_current_mode,
    .map_framebuffer = bga_map_framebuffer,
    .unmap_framebuffer = bga_unmap_framebuffer,
    .clear_screen = bga_clear_screen,
    .draw_pixel = bga_draw_pixel,
    .draw_rect = bga_draw_rect,
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

// Declare the driver with appropriate flags
DECLARE_DISPLAY_DRIVER(bga, bga_ops);

// Set driver capabilities
static void bga_set_driver_flags(void) {
    bga_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE | DRIVER_FLAG_SUPPORTS_VSYNC;
}

// BGA register access functions
static void bga_write_register(uint16_t index, uint16_t data) {
    outportw(VBE_DISPI_IOPORT_INDEX, index);
    outportw(VBE_DISPI_IOPORT_DATA, data);
}

static uint16_t bga_read_register(uint16_t index) {
    outportw(VBE_DISPI_IOPORT_INDEX, index);
    return inportw(VBE_DISPI_IOPORT_DATA);
}

static bool bga_is_available(void) {
    uint16_t version = bga_read_register(VBE_DISPI_INDEX_ID);
    return (version >= VBE_DISPI_ID0 && version <= VBE_DISPI_ID5);
}

static graphics_result_t bga_set_video_mode(uint32_t width, uint32_t height, uint32_t bpp, bool use_lfb, bool clear_memory) {
    // Disable VBE extensions first
    bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    
    // Set resolution and bit depth
    bga_write_register(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bga_write_register(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bga_write_register(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
    
    // Enable with appropriate flags
    uint16_t enable_flags = VBE_DISPI_ENABLED;
    if (use_lfb) {
        enable_flags |= VBE_DISPI_LFB_ENABLED;
    }
    if (!clear_memory) {
        enable_flags |= VBE_DISPI_NOCLEARMEM;
    }
    
    bga_write_register(VBE_DISPI_INDEX_ENABLE, enable_flags);
    
    // Verify the mode was set correctly
    uint16_t actual_width = bga_read_register(VBE_DISPI_INDEX_XRES);
    uint16_t actual_height = bga_read_register(VBE_DISPI_INDEX_YRES);
    uint16_t actual_bpp = bga_read_register(VBE_DISPI_INDEX_BPP);
    
    if (actual_width != width || actual_height != height || actual_bpp != bpp) {
        debuglog(DEBUG_ERROR, "BGA: Failed to set mode %ux%ux%u (got %ux%ux%u)\n",
                width, height, bpp, actual_width, actual_height, actual_bpp);
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Update state
    bga_state.current_width = width;
    bga_state.current_height = height;
    bga_state.current_bpp = bpp;
    bga_state.current_format = bga_bpp_to_pixel_format(bpp);
    bga_state.linear_fb_enabled = use_lfb;
    
    return GRAPHICS_SUCCESS;
}

static pixel_format_t bga_bpp_to_pixel_format(uint8_t bpp) {
    switch (bpp) {
        case 8:
            return PIXEL_FORMAT_INDEXED_8;
        case 15:
            return PIXEL_FORMAT_RGB_555;
        case 16:
            return PIXEL_FORMAT_RGB_565;
        case 24:
            return PIXEL_FORMAT_RGB_888;
        case 32:
            return PIXEL_FORMAT_RGBA_8888;
        default:
            return PIXEL_FORMAT_RGB_888; // Default fallback
    }
}

static uint32_t bga_color_to_pixel(graphics_color_t color, pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_RGB_555:
            return ((color.r >> 3) << 10) | ((color.g >> 3) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_565:
            return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
        case PIXEL_FORMAT_RGB_888:
            return (color.r << 16) | (color.g << 8) | color.b;
        case PIXEL_FORMAT_RGBA_8888:
            return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
        case PIXEL_FORMAT_INDEXED_8:
            // For indexed color, use a simple grayscale approximation
            return (color.r * 77 + color.g * 150 + color.b * 29) >> 8;
        default:
            return 0;
    }
}

static void bga_put_pixel(int32_t x, int32_t y, uint32_t pixel_value) {
    if (!bga_state.framebuffer || x < 0 || y < 0 || 
        (uint32_t)x >= bga_state.current_width || 
        (uint32_t)y >= bga_state.current_height) {
        return;
    }
    
    uint32_t pitch = bga_state.current_width * (bga_state.current_bpp / 8);
    uint32_t offset = y * pitch + x * (bga_state.current_bpp / 8);
    
    uint8_t* fb = (uint8_t*)bga_state.framebuffer;
    
    switch (bga_state.current_bpp) {
        case 8:
            fb[offset] = (uint8_t)pixel_value;
            break;
        case 15:
        case 16:
            *(uint16_t*)(fb + offset) = (uint16_t)pixel_value;
            break;
        case 24:
            fb[offset] = pixel_value & 0xFF;
            fb[offset + 1] = (pixel_value >> 8) & 0xFF;
            fb[offset + 2] = (pixel_value >> 16) & 0xFF;
            break;
        case 32:
            *(uint32_t*)(fb + offset) = pixel_value;
            break;
    }
}

// Driver operation implementations
static graphics_result_t bga_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing Bochs BGA driver for device %s\n", device->name);
    
    // Check if BGA is available
    if (!bga_is_available()) {
        debuglog(DEBUG_ERROR, "BGA: Hardware not detected\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Get BGA version
    bga_state.bga_version = bga_read_register(VBE_DISPI_INDEX_ID);
    debuglog(DEBUG_INFO, "BGA: Detected version 0x%04x\n", bga_state.bga_version);
    
    // Get framebuffer address from PCI BAR0
    bga_state.framebuffer_phys = device->framebuffer_base;
    bga_state.framebuffer_size = device->framebuffer_size;
    
    if (bga_state.framebuffer_phys == 0) {
        debuglog(DEBUG_ERROR, "BGA: No framebuffer address found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Map framebuffer to virtual memory (identity mapping for now)
    bga_state.framebuffer = (void*)bga_state.framebuffer_phys;
    
    // Set initial mode (640x480x16)
    graphics_result_t result = bga_set_video_mode(640, 480, 16, true, true);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "BGA: Failed to set initial mode\n");
        return result;
    }
    
    bga_state.device = device;
    bga_state.initialized = true;
    
    debuglog(DEBUG_INFO, "BGA: Driver initialized successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_shutdown(graphics_device_t* device) {
    if (!bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Shutting down Bochs BGA driver\n");
    
    // Disable VBE extensions
    bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    
    // Clean up state
    bga_state.initialized = false;
    bga_state.framebuffer = NULL;
    bga_state.device = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = NUM_STANDARD_BGA_MODES;
    *modes = kmalloc(sizeof(video_mode_t) * NUM_STANDARD_BGA_MODES);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    for (uint32_t i = 0; i < NUM_STANDARD_BGA_MODES; i++) {
        video_mode_t* mode = &(*modes)[i];
        const bga_mode_info_t* bga_mode = &standard_bga_modes[i];
        
        mode->width = bga_mode->width;
        mode->height = bga_mode->height;
        mode->bpp = bga_mode->bpp;
        mode->pitch = bga_mode->width * (bga_mode->bpp / 8);
        mode->format = bga_bpp_to_pixel_format(bga_mode->bpp);
        mode->refresh_rate = 60; // Standard refresh rate
        mode->is_text_mode = false;
        mode->mode_number = i; // Use array index as mode number
        mode->hw_data = NULL;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "BGA: Setting mode %ux%ux%u\n", mode->width, mode->height, mode->bpp);
    
    return bga_set_video_mode(mode->width, mode->height, mode->bpp, true, false);
}

static graphics_result_t bga_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = bga_state.current_width;
    mode->height = bga_state.current_height;
    mode->bpp = bga_state.current_bpp;
    mode->pitch = bga_state.current_width * (bga_state.current_bpp / 8);
    mode->format = bga_state.current_format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->mode_number = 0;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t));
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    framebuffer->virtual_addr = bga_state.framebuffer;
    framebuffer->physical_addr = bga_state.framebuffer_phys;
    framebuffer->size = bga_state.framebuffer_size;
    framebuffer->width = bga_state.current_width;
    framebuffer->height = bga_state.current_height;
    framebuffer->pitch = bga_state.current_width * (bga_state.current_bpp / 8);
    framebuffer->format = bga_state.current_format;
    framebuffer->bpp = bga_state.current_bpp;
    framebuffer->back_buffer = NULL;
    framebuffer->double_buffered = false;
    framebuffer->hw_cursor_available = false;
    framebuffer->cursor_data = NULL;
    
    *fb = framebuffer;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb) {
    if (!device || !fb) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    kfree(fb);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = bga_color_to_pixel(color, bga_state.current_format);
    uint32_t pixel_size = bga_state.current_bpp / 8;
    uint32_t framebuffer_size_pixels = bga_state.current_width * bga_state.current_height;
    
    uint8_t* fb = (uint8_t*)bga_state.framebuffer;
    
    // Fast clear for common pixel sizes
    switch (pixel_size) {
        case 1:
            memset(fb, (uint8_t)pixel_value, framebuffer_size_pixels);
            break;
        case 2:
            for (uint32_t i = 0; i < framebuffer_size_pixels; i++) {
                *(uint16_t*)(fb + i * 2) = (uint16_t)pixel_value;
            }
            break;
        case 3:
            for (uint32_t i = 0; i < framebuffer_size_pixels; i++) {
                fb[i * 3] = pixel_value & 0xFF;
                fb[i * 3 + 1] = (pixel_value >> 8) & 0xFF;
                fb[i * 3 + 2] = (pixel_value >> 16) & 0xFF;
            }
            break;
        case 4:
            for (uint32_t i = 0; i < framebuffer_size_pixels; i++) {
                *(uint32_t*)(fb + i * 4) = pixel_value;
            }
            break;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color) {
    if (!device || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = bga_color_to_pixel(color, bga_state.current_format);
    bga_put_pixel(x, y, pixel_value);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t bga_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled) {
    if (!device || !rect || !bga_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t pixel_value = bga_color_to_pixel(color, bga_state.current_format);
    
    if (filled) {
        // Fill the rectangle
        for (int32_t y = rect->y; y < rect->y + (int32_t)rect->height; y++) {
            for (int32_t x = rect->x; x < rect->x + (int32_t)rect->width; x++) {
                bga_put_pixel(x, y, pixel_value);
            }
        }
    } else {
        // Draw rectangle outline
        // Top and bottom horizontal lines
        for (int32_t x = rect->x; x < rect->x + (int32_t)rect->width; x++) {
            bga_put_pixel(x, rect->y, pixel_value);
            bga_put_pixel(x, rect->y + (int32_t)rect->height - 1, pixel_value);
        }
        // Left and right vertical lines
        for (int32_t y = rect->y; y < rect->y + (int32_t)rect->height; y++) {
            bga_put_pixel(rect->x, y, pixel_value);
            bga_put_pixel(rect->x + (int32_t)rect->width - 1, y, pixel_value);
        }
    }
    
    return GRAPHICS_SUCCESS;
}

// Driver initialization function
DRIVER_INIT_FUNCTION(bga) {
    debuglog(DEBUG_INFO, "Registering Bochs BGA driver\n");
    bga_set_driver_flags();
    return register_display_driver(&bga_driver);
}

// Driver exit function
DRIVER_EXIT_FUNCTION(bga) {
    debuglog(DEBUG_INFO, "Unregistering Bochs BGA driver\n");
    unregister_display_driver(&bga_driver);
}