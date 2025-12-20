#ifndef GRAPHICS_TYPES_H
#define GRAPHICS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct display_driver display_driver_t;
typedef struct graphics_device graphics_device_t;
typedef struct framebuffer framebuffer_t;
typedef struct video_mode video_mode_t;
typedef struct font font_t;

// Graphics device types
typedef enum {
    GRAPHICS_DEVICE_UNKNOWN = 0,
    GRAPHICS_DEVICE_VGA,
    GRAPHICS_DEVICE_VESA,
    GRAPHICS_DEVICE_INTEL_HD,
    GRAPHICS_DEVICE_NVIDIA,
    GRAPHICS_DEVICE_AMD,
    GRAPHICS_DEVICE_VMWARE_SVGA,
    GRAPHICS_DEVICE_BOCHS_VBE
} graphics_device_type_t;

// Pixel format types
typedef enum {
    PIXEL_FORMAT_TEXT_MODE = 0,
    PIXEL_FORMAT_INDEXED_8,
    PIXEL_FORMAT_RGB_555,
    PIXEL_FORMAT_RGB_565,
    PIXEL_FORMAT_RGB_888,
    PIXEL_FORMAT_RGBA_8888,
    PIXEL_FORMAT_BGR_888,
    PIXEL_FORMAT_BGRA_8888
} pixel_format_t;

// Video mode structure
struct video_mode {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;               // Bits per pixel
    uint32_t pitch;             // Bytes per scanline
    pixel_format_t format;
    uint32_t refresh_rate;
    bool is_text_mode;
    
    // Hardware-specific data
    uint32_t mode_number;       // VESA mode number or hardware-specific
    void* hw_data;              // Pointer to hardware-specific mode data
};

// Framebuffer structure
struct framebuffer {
    void* virtual_addr;         // Mapped virtual address
    uintptr_t physical_addr;    // Physical framebuffer address
    size_t size;                // Framebuffer size in bytes
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    pixel_format_t format;
    uint32_t bpp;
    
    // Double buffering support
    void* back_buffer;
    bool double_buffered;
    
    // Hardware cursor support
    bool hw_cursor_available;
    void* cursor_data;
};

// Graphics capabilities
typedef struct {
    bool supports_2d_accel;
    bool supports_3d_accel;
    bool supports_hw_cursor;
    bool supports_page_flipping;
    bool supports_vsync;
    bool supports_multiple_heads;
    uint32_t max_resolution_x;
    uint32_t max_resolution_y;
    uint32_t video_memory_size;
    uint32_t num_video_modes;
} graphics_capabilities_t;

// Graphics device structure
struct graphics_device {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t revision;
    graphics_device_type_t type;
    char name[64];
    
    // PCI information
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    
    // Memory regions
    uintptr_t mmio_base;
    size_t mmio_size;
    uintptr_t framebuffer_base;
    size_t framebuffer_size;
    
    // Capabilities
    graphics_capabilities_t caps;
    
    // Current state
    framebuffer_t* current_fb;
    video_mode_t current_mode;
    bool is_active;
    
    // Driver associated with this device
    display_driver_t* driver;
};

// Driver operation results
typedef enum {
    GRAPHICS_SUCCESS = 0,
    GRAPHICS_ERROR_GENERIC,
    GRAPHICS_ERROR_INVALID_MODE,
    GRAPHICS_ERROR_HARDWARE_FAULT,
    GRAPHICS_ERROR_OUT_OF_MEMORY,
    GRAPHICS_ERROR_NOT_SUPPORTED,
    GRAPHICS_ERROR_DEVICE_BUSY,
    GRAPHICS_ERROR_INVALID_PARAMETER
} graphics_result_t;

// Rectangle structure for drawing operations
typedef struct {
    int32_t x, y;
    uint32_t width, height;
} graphics_rect_t;

// Color structure
typedef struct {
    uint8_t r, g, b, a;
} graphics_color_t;

// Surface structure for rendering
typedef struct {
    void* pixels;
    uint32_t width, height;
    uint32_t pitch;
    pixel_format_t format;
    uint32_t bpp;
} graphics_surface_t;

// Font glyph structure
typedef struct {
    uint32_t codepoint;
    uint8_t width, height;
    int8_t bearing_x, bearing_y;
    uint8_t advance;
    uint8_t* bitmap;
} font_glyph_t;

// Input event types for graphics system
typedef enum {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_KEY_PRESS,
    INPUT_EVENT_KEY_RELEASE,
    INPUT_EVENT_MOUSE_MOVE,
    INPUT_EVENT_MOUSE_BUTTON_PRESS,
    INPUT_EVENT_MOUSE_BUTTON_RELEASE,
    INPUT_EVENT_MOUSE_WHEEL
} input_event_type_t;

// Input event structure
typedef struct {
    input_event_type_t type;
    uint32_t timestamp;
    union {
        struct {
            uint32_t keycode;
            uint32_t scancode;
            uint32_t modifiers;
        } key;
        struct {
            int32_t x, y;
            int32_t delta_x, delta_y;
        } mouse_move;
        struct {
            uint8_t button;
            int32_t x, y;
        } mouse_button;
        struct {
            int32_t delta;
            int32_t x, y;
        } mouse_wheel;
    };
} input_event_t;

#endif // GRAPHICS_TYPES_H
