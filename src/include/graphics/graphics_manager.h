#ifndef GRAPHICS_MANAGER_H
#define GRAPHICS_MANAGER_H

#include "graphics_types.h"
#include "display_driver.h"

// Graphics subsystem initialization
graphics_result_t graphics_init(void);
graphics_result_t graphics_shutdown(void);
bool graphics_is_initialized(void);

// Device management
graphics_device_t* graphics_get_primary_device(void);
graphics_result_t graphics_set_primary_device(graphics_device_t* device);
graphics_device_t* graphics_get_device(uint32_t index);
uint32_t graphics_get_device_count(void);

// Mode management
graphics_result_t graphics_set_mode(uint32_t width, uint32_t height, 
                                  uint32_t bpp, uint32_t refresh_rate);
graphics_result_t graphics_set_text_mode(uint32_t cols, uint32_t rows);
graphics_result_t graphics_get_current_mode(video_mode_t* mode);
graphics_result_t graphics_enumerate_modes(video_mode_t** modes, uint32_t* count);
graphics_result_t graphics_get_capabilities(graphics_capabilities_t* caps);

// Framebuffer access
framebuffer_t* graphics_get_framebuffer(void);
graphics_result_t graphics_map_framebuffer(framebuffer_t** fb);
graphics_result_t graphics_unmap_framebuffer(framebuffer_t* fb);

// Drawing operations
graphics_result_t graphics_clear_screen(graphics_color_t color);
graphics_result_t graphics_draw_pixel(int32_t x, int32_t y, graphics_color_t color);
graphics_result_t graphics_draw_rect(const graphics_rect_t* rect, 
                                   graphics_color_t color, bool filled);
graphics_result_t graphics_draw_line(int32_t x1, int32_t y1, 
                                   int32_t x2, int32_t y2, 
                                   graphics_color_t color);
graphics_result_t graphics_blit_surface(const graphics_surface_t* surface,
                                      const graphics_rect_t* src_rect,
                                      int32_t dst_x, int32_t dst_y);

// Text mode operations
graphics_result_t graphics_write_char(int32_t x, int32_t y, char c, uint8_t attr);
graphics_result_t graphics_write_string(int32_t x, int32_t y, 
                                       const char* str, uint8_t attr);
graphics_result_t graphics_printf(int32_t x, int32_t y, uint8_t attr, 
                                 const char* format, ...);
graphics_result_t graphics_scroll_screen(int32_t lines);
graphics_result_t graphics_set_cursor_pos(int32_t x, int32_t y);
graphics_result_t graphics_get_cursor_pos(int32_t* x, int32_t* y);

// Cursor management
graphics_result_t graphics_set_cursor(const graphics_surface_t* cursor_surface,
                                     int32_t hotspot_x, int32_t hotspot_y);
graphics_result_t graphics_move_cursor(int32_t x, int32_t y);
graphics_result_t graphics_show_cursor(bool show);

// Surface management
graphics_result_t graphics_create_surface(uint32_t width, uint32_t height,
                                        pixel_format_t format,
                                        graphics_surface_t** surface);
graphics_result_t graphics_destroy_surface(graphics_surface_t* surface);
graphics_result_t graphics_copy_surface(const graphics_surface_t* src,
                                       graphics_surface_t* dst);

// Color utilities
graphics_color_t graphics_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
uint32_t graphics_color_to_pixel(graphics_color_t color, pixel_format_t format);
graphics_color_t graphics_pixel_to_color(uint32_t pixel, pixel_format_t format);

// Font management
graphics_result_t graphics_load_font(const char* name, uint8_t size, font_t** font);
graphics_result_t graphics_unload_font(font_t* font);
graphics_result_t graphics_draw_text(int32_t x, int32_t y, const char* text,
                                    font_t* font, graphics_color_t color);
graphics_result_t graphics_get_text_bounds(const char* text, font_t* font,
                                         uint32_t* width, uint32_t* height);

// Double buffering
graphics_result_t graphics_enable_double_buffering(bool enable);
graphics_result_t graphics_swap_buffers(void);
graphics_result_t graphics_wait_for_vsync(void);

// Input handling for graphics system
graphics_result_t graphics_register_input_handler(
    void (*handler)(const input_event_t* event));
graphics_result_t graphics_unregister_input_handler(void);
graphics_result_t graphics_inject_input_event(const input_event_t* event);

// Configuration and capabilities
graphics_result_t graphics_set_config(const char* key, const char* value);
graphics_result_t graphics_get_config(const char* key, char* value, size_t size);

// Debug and diagnostics
graphics_result_t graphics_dump_device_info(graphics_device_t* device);
graphics_result_t graphics_run_diagnostics(void);
const char* graphics_get_error_string(graphics_result_t error);

// Standard colors
#define COLOR_BLACK        ((graphics_color_t){0,   0,   0,   255})
#define COLOR_WHITE        ((graphics_color_t){255, 255, 255, 255})
#define COLOR_RED          ((graphics_color_t){255, 0,   0,   255})
#define COLOR_GREEN        ((graphics_color_t){0,   255, 0,   255})
#define COLOR_BLUE         ((graphics_color_t){0,   0,   255, 255})
#define COLOR_CYAN         ((graphics_color_t){0,   255, 255, 255})
#define COLOR_MAGENTA      ((graphics_color_t){255, 0,   255, 255})
#define COLOR_YELLOW       ((graphics_color_t){255, 255, 0,   255})
#define COLOR_GRAY         ((graphics_color_t){128, 128, 128, 255})
#define COLOR_LIGHT_GRAY   ((graphics_color_t){192, 192, 192, 255})
#define COLOR_DARK_GRAY    ((graphics_color_t){64,  64,  64,  255})
#define COLOR_TRANSPARENT  ((graphics_color_t){0,   0,   0,   0})

// Standard text mode attributes
#define TEXT_ATTR_BLACK         0x00
#define TEXT_ATTR_BLUE          0x01
#define TEXT_ATTR_GREEN         0x02
#define TEXT_ATTR_CYAN          0x03
#define TEXT_ATTR_RED           0x04
#define TEXT_ATTR_MAGENTA       0x05
#define TEXT_ATTR_BROWN         0x06
#define TEXT_ATTR_LIGHT_GRAY    0x07
#define TEXT_ATTR_DARK_GRAY     0x08
#define TEXT_ATTR_LIGHT_BLUE    0x09
#define TEXT_ATTR_LIGHT_GREEN   0x0A
#define TEXT_ATTR_LIGHT_CYAN    0x0B
#define TEXT_ATTR_LIGHT_RED     0x0C
#define TEXT_ATTR_LIGHT_MAGENTA 0x0D
#define TEXT_ATTR_YELLOW        0x0E
#define TEXT_ATTR_WHITE         0x0F

#define TEXT_ATTR_BLINK         0x80
#define TEXT_ATTR_BRIGHT        0x08

#define MAKE_TEXT_ATTR(fg, bg) ((fg) | ((bg) << 4))

#endif // GRAPHICS_MANAGER_H
