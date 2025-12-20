#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/graphics/graphics_manager.h"
#include "../../include/io_ports.h"
#include "../../include/debuglog.h"

// VGA text mode constants
#define VGA_TEXT_MEMORY     0xB8000
#define VGA_TEXT_WIDTH      80
#define VGA_TEXT_HEIGHT     25
#define VGA_TEXT_SIZE       (VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT * 2)

// VGA ports for cursor control
#define VGA_CRTC_INDEX      0x3D4
#define VGA_CRTC_DATA       0x3D5
#define VGA_CURSOR_HIGH     0x0E
#define VGA_CURSOR_LOW      0x0F

// Driver state
static struct {
    uint16_t* video_memory;
    int32_t cursor_x;
    int32_t cursor_y;
    uint8_t current_attr;
    bool initialized;
} vga_text_state = {
    .video_memory = (uint16_t*)VGA_TEXT_MEMORY,
    .cursor_x = 0,
    .cursor_y = 0,
    .current_attr = 0x07, // White on black
    .initialized = false
};

// Driver operation implementations
static graphics_result_t vga_text_initialize(graphics_device_t* device);
static graphics_result_t vga_text_shutdown(graphics_device_t* device);
static graphics_result_t vga_text_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t vga_text_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t vga_text_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t vga_text_write_char(graphics_device_t* device, int32_t x, int32_t y, char c, uint8_t attr);
static graphics_result_t vga_text_write_string(graphics_device_t* device, int32_t x, int32_t y, const char* str, uint8_t attr);
static graphics_result_t vga_text_scroll_screen(graphics_device_t* device, int32_t lines);
static graphics_result_t vga_text_set_cursor_pos(graphics_device_t* device, int32_t x, int32_t y);

// Helper functions
static void update_cursor(int32_t x, int32_t y);
static void scroll_up(int32_t lines);
static uint8_t color_to_vga_attr(graphics_color_t color);

// VGA text driver operations structure
static display_driver_ops_t vga_text_ops = {
    .name = "vga_text",
    .version = 1,
    .initialize = vga_text_initialize,
    .shutdown = vga_text_shutdown,
    .reset = NULL,
    .enumerate_modes = NULL,
    .set_mode = vga_text_set_mode,
    .get_current_mode = vga_text_get_current_mode,
    .map_framebuffer = NULL,
    .unmap_framebuffer = NULL,
    .clear_screen = vga_text_clear_screen,
    .draw_pixel = NULL,
    .draw_rect = NULL,
    .blit_surface = NULL,
    .set_cursor = NULL,
    .move_cursor = NULL,
    .show_cursor = NULL,
    .write_char = vga_text_write_char,
    .write_string = vga_text_write_string,
    .scroll_screen = vga_text_scroll_screen,
    .set_cursor_pos = vga_text_set_cursor_pos,
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
DECLARE_DISPLAY_DRIVER(vga_text, vga_text_ops);

static graphics_result_t vga_text_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing VGA text mode driver\n");
    
    // Map VGA text memory if not already mapped
    vga_text_state.video_memory = (uint16_t*)VGA_TEXT_MEMORY;
    
    // Clear screen
    vga_text_clear_screen(device, COLOR_BLACK);
    
    // Reset cursor position
    vga_text_state.cursor_x = 0;
    vga_text_state.cursor_y = 0;
    update_cursor(0, 0);
    
    // Set default attribute
    vga_text_state.current_attr = 0x07; // White on black
    
    vga_text_state.initialized = true;
    
    // Update device capabilities
    device->caps.supports_2d_accel = false;
    device->caps.supports_3d_accel = false;
    device->caps.supports_hw_cursor = true;
    device->caps.supports_page_flipping = false;
    device->caps.supports_vsync = false;
    device->caps.max_resolution_x = VGA_TEXT_WIDTH;
    device->caps.max_resolution_y = VGA_TEXT_HEIGHT;
    device->caps.video_memory_size = VGA_TEXT_SIZE;
    
    debuglog(DEBUG_INFO, "VGA text mode driver initialized successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_shutdown(graphics_device_t* device) {
    debuglog(DEBUG_INFO, "Shutting down VGA text mode driver\n");
    
    vga_text_state.initialized = false;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // VGA text mode only supports 80x25
    if (!mode->is_text_mode || mode->width != VGA_TEXT_WIDTH || mode->height != VGA_TEXT_HEIGHT) {
        debuglog(DEBUG_WARN, "VGA text driver only supports 80x25 text mode\n");
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Update device current mode
    device->current_mode = *mode;
    device->current_mode.format = PIXEL_FORMAT_TEXT_MODE;
    device->current_mode.bpp = 4;
    device->current_mode.pitch = VGA_TEXT_WIDTH * 2;
    
    debuglog(DEBUG_INFO, "VGA text mode set: %ux%u\n", mode->width, mode->height);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = VGA_TEXT_WIDTH;
    mode->height = VGA_TEXT_HEIGHT;
    mode->bpp = 4;
    mode->format = PIXEL_FORMAT_TEXT_MODE;
    mode->refresh_rate = 60;
    mode->is_text_mode = true;
    mode->pitch = VGA_TEXT_WIDTH * 2;
    mode->mode_number = 3; // Standard VGA text mode
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!vga_text_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    uint8_t attr = color_to_vga_attr(color);
    uint16_t clear_value = ((uint16_t)attr << 8) | ' ';
    
    // Clear entire screen
    for (int i = 0; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++) {
        vga_text_state.video_memory[i] = clear_value;
    }
    
    // Reset cursor position
    vga_text_state.cursor_x = 0;
    vga_text_state.cursor_y = 0;
    update_cursor(0, 0);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_write_char(graphics_device_t* device, int32_t x, int32_t y, char c, uint8_t attr) {
    if (!vga_text_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Bounds checking
    if (x < 0 || x >= VGA_TEXT_WIDTH || y < 0 || y >= VGA_TEXT_HEIGHT) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate position in video memory
    int offset = y * VGA_TEXT_WIDTH + x;
    uint16_t char_with_attr = ((uint16_t)attr << 8) | (uint8_t)c;
    
    vga_text_state.video_memory[offset] = char_with_attr;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_write_string(graphics_device_t* device, int32_t x, int32_t y, const char* str, uint8_t attr) {
    if (!str || !vga_text_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    int32_t current_x = x;
    int32_t current_y = y;
    
    for (const char* p = str; *p; p++) {
        if (*p == '\n') {
            current_y++;
            current_x = 0;
            
            // Handle screen overflow
            if (current_y >= VGA_TEXT_HEIGHT) {
                scroll_up(1);
                current_y = VGA_TEXT_HEIGHT - 1;
            }
        } else if (*p == '\r') {
            current_x = 0;
        } else if (*p == '\t') {
            // Tab to next 4-character boundary
            current_x = ((current_x + 4) / 4) * 4;
            if (current_x >= VGA_TEXT_WIDTH) {
                current_x = 0;
                current_y++;
                if (current_y >= VGA_TEXT_HEIGHT) {
                    scroll_up(1);
                    current_y = VGA_TEXT_HEIGHT - 1;
                }
            }
        } else {
            // Regular character
            if (current_x >= VGA_TEXT_WIDTH) {
                current_x = 0;
                current_y++;
                if (current_y >= VGA_TEXT_HEIGHT) {
                    scroll_up(1);
                    current_y = VGA_TEXT_HEIGHT - 1;
                }
            }
            
            graphics_result_t result = vga_text_write_char(device, current_x, current_y, *p, attr);
            if (result != GRAPHICS_SUCCESS) {
                return result;
            }
            
            current_x++;
        }
    }
    
    // Update cursor position
    vga_text_state.cursor_x = current_x;
    vga_text_state.cursor_y = current_y;
    update_cursor(current_x, current_y);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_scroll_screen(graphics_device_t* device, int32_t lines) {
    if (!vga_text_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    if (lines <= 0) {
        return GRAPHICS_SUCCESS;
    }
    
    scroll_up(lines);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vga_text_set_cursor_pos(graphics_device_t* device, int32_t x, int32_t y) {
    if (x < 0 || x >= VGA_TEXT_WIDTH || y < 0 || y >= VGA_TEXT_HEIGHT) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    vga_text_state.cursor_x = x;
    vga_text_state.cursor_y = y;
    update_cursor(x, y);
    
    return GRAPHICS_SUCCESS;
}

// Helper function implementations
static void update_cursor(int32_t x, int32_t y) {
    uint16_t position = y * VGA_TEXT_WIDTH + x;
    
    // Send position to VGA CRTC
    outportb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
    outportb(VGA_CRTC_DATA, (position >> 8) & 0xFF);
    outportb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
    outportb(VGA_CRTC_DATA, position & 0xFF);
}

static void scroll_up(int32_t lines) {
    if (lines >= VGA_TEXT_HEIGHT) {
        // Clear entire screen if scrolling more than screen height
        uint16_t clear_value = ((uint16_t)vga_text_state.current_attr << 8) | ' ';
        for (int i = 0; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++) {
            vga_text_state.video_memory[i] = clear_value;
        }
        return;
    }
    
    // Move lines up
    int chars_to_move = (VGA_TEXT_HEIGHT - lines) * VGA_TEXT_WIDTH;
    for (int i = 0; i < chars_to_move; i++) {
        vga_text_state.video_memory[i] = vga_text_state.video_memory[i + lines * VGA_TEXT_WIDTH];
    }
    
    // Clear bottom lines
    uint16_t clear_value = ((uint16_t)vga_text_state.current_attr << 8) | ' ';
    for (int i = chars_to_move; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++) {
        vga_text_state.video_memory[i] = clear_value;
    }
}

static uint8_t color_to_vga_attr(graphics_color_t color) {
    // Convert graphics color to VGA attribute
    // This is a simple mapping - black background, color based on RGB values
    uint8_t fg_color = 0x07; // Default to light gray
    
    // Simple color mapping based on dominant color component
    if (color.r > color.g && color.r > color.b) {
        fg_color = (color.r > 128) ? 0x0C : 0x04; // Light red or red
    } else if (color.g > color.r && color.g > color.b) {
        fg_color = (color.g > 128) ? 0x0A : 0x02; // Light green or green
    } else if (color.b > color.r && color.b > color.g) {
        fg_color = (color.b > 128) ? 0x09 : 0x01; // Light blue or blue
    } else if (color.r == color.g && color.g == color.b) {
        // Grayscale
        if (color.r < 64) fg_color = 0x00;      // Black
        else if (color.r < 128) fg_color = 0x08; // Dark gray
        else if (color.r < 192) fg_color = 0x07; // Light gray
        else fg_color = 0x0F;                    // White
    }
    
    return fg_color; // Black background (0x00)
}

// Driver initialization function
DRIVER_INIT_FUNCTION(vga_text) {
    debuglog(DEBUG_INFO, "Registering VGA text mode driver\n");
    
    // Set driver flags
    vga_text_driver.flags = DRIVER_FLAG_SUPPORTS_TEXT_MODE | 
                           DRIVER_FLAG_DEFAULT_DRIVER;
    
    return register_display_driver(&vga_text_driver);
}
