#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "graphics_types.h"

// Display driver interface - all graphics drivers must implement this
typedef struct display_driver_ops {
    // Driver identification
    const char* name;
    uint32_t version;
    
    // Driver lifecycle
    graphics_result_t (*initialize)(graphics_device_t* device);
    graphics_result_t (*shutdown)(graphics_device_t* device);
    graphics_result_t (*reset)(graphics_device_t* device);
    
    // Mode management
    graphics_result_t (*enumerate_modes)(graphics_device_t* device, 
                                       video_mode_t** modes, 
                                       uint32_t* count);
    graphics_result_t (*set_mode)(graphics_device_t* device, 
                                const video_mode_t* mode);
    graphics_result_t (*get_current_mode)(graphics_device_t* device, 
                                        video_mode_t* mode);
    
    // Framebuffer management
    graphics_result_t (*map_framebuffer)(graphics_device_t* device, 
                                       framebuffer_t** fb);
    graphics_result_t (*unmap_framebuffer)(graphics_device_t* device, 
                                         framebuffer_t* fb);
    
    // Basic drawing operations (software fallback)
    graphics_result_t (*clear_screen)(graphics_device_t* device, 
                                    graphics_color_t color);
    graphics_result_t (*draw_pixel)(graphics_device_t* device, 
                                  int32_t x, int32_t y, 
                                  graphics_color_t color);
    graphics_result_t (*draw_rect)(graphics_device_t* device, 
                                 const graphics_rect_t* rect, 
                                 graphics_color_t color, 
                                 bool filled);
    graphics_result_t (*blit_surface)(graphics_device_t* device,
                                    const graphics_surface_t* src,
                                    const graphics_rect_t* src_rect,
                                    int32_t dst_x, int32_t dst_y);
    
    // Hardware cursor (optional)
    graphics_result_t (*set_cursor)(graphics_device_t* device,
                                  const graphics_surface_t* cursor,
                                  int32_t hotspot_x, int32_t hotspot_y);
    graphics_result_t (*move_cursor)(graphics_device_t* device,
                                   int32_t x, int32_t y);
    graphics_result_t (*show_cursor)(graphics_device_t* device, bool show);
    
    // Text mode operations
    graphics_result_t (*write_char)(graphics_device_t* device,
                                  int32_t x, int32_t y,
                                  char c, uint8_t attr);
    graphics_result_t (*write_string)(graphics_device_t* device,
                                    int32_t x, int32_t y,
                                    const char* str, uint8_t attr);
    graphics_result_t (*scroll_screen)(graphics_device_t* device,
                                     int32_t lines);
    graphics_result_t (*set_cursor_pos)(graphics_device_t* device,
                                      int32_t x, int32_t y);
    
    // Power management
    graphics_result_t (*set_power_state)(graphics_device_t* device,
                                       uint32_t state);
    
    // Hardware acceleration (optional)
    graphics_result_t (*hw_fill_rect)(graphics_device_t* device,
                                    const graphics_rect_t* rect,
                                    graphics_color_t color);
    graphics_result_t (*hw_copy_rect)(graphics_device_t* device,
                                    const graphics_rect_t* src,
                                    int32_t dst_x, int32_t dst_y);
    graphics_result_t (*hw_line)(graphics_device_t* device,
                               int32_t x1, int32_t y1,
                               int32_t x2, int32_t y2,
                               graphics_color_t color);
    
    // Synchronization
    graphics_result_t (*wait_for_vsync)(graphics_device_t* device);
    graphics_result_t (*page_flip)(graphics_device_t* device,
                                 framebuffer_t* front_buffer);
    
    // EDID and monitor detection
    graphics_result_t (*read_edid)(graphics_device_t* device,
                                 uint8_t* edid_data,
                                 size_t* size);
    
    // Driver-specific operations
    graphics_result_t (*ioctl)(graphics_device_t* device,
                             uint32_t cmd,
                             void* arg);
} display_driver_ops_t;

// Display driver structure
struct display_driver {
    display_driver_ops_t* ops;
    void* private_data;
    uint32_t flags;
    
    // Driver registration info
    struct display_driver* next;
    bool is_loaded;
};

// Driver registration and management
graphics_result_t register_display_driver(display_driver_t* driver);
graphics_result_t unregister_display_driver(display_driver_t* driver);
display_driver_t* find_driver_for_device(graphics_device_t* device);

// Standard driver flags
#define DRIVER_FLAG_SUPPORTS_TEXT_MODE      (1 << 0)
#define DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE  (1 << 1)
#define DRIVER_FLAG_SUPPORTS_3D_ACCEL       (1 << 2)
#define DRIVER_FLAG_SUPPORTS_HW_CURSOR      (1 << 3)
#define DRIVER_FLAG_SUPPORTS_VSYNC          (1 << 4)
#define DRIVER_FLAG_REQUIRES_KERNEL_MODE    (1 << 5)
#define DRIVER_FLAG_DEFAULT_DRIVER          (1 << 6)

// Helper macros for driver development
#define DECLARE_DISPLAY_DRIVER(name, driver_ops) \
    display_driver_t name##_driver = { \
        .ops = &driver_ops, \
        .private_data = NULL, \
        .flags = 0, \
        .next = NULL, \
        .is_loaded = false \
    }

#define DRIVER_INIT_FUNCTION(driver_name) \
    graphics_result_t driver_name##_init(void)

#define DRIVER_EXIT_FUNCTION(driver_name) \
    void driver_name##_exit(void)

#endif // DISPLAY_DRIVER_H
