#ifndef GRAPHICS_INIT_H
#define GRAPHICS_INIT_H

#include "graphics/graphics_types.h"
#include "graphics/window_manager.h"

// Graphics subsystem initialization and shutdown (V2 system)
graphics_result_t initialize_graphics_subsystem(void);
graphics_result_t shutdown_graphics_subsystem(void);

// Graphics functionality testing
graphics_result_t test_graphics_functionality(void);

// Window manager testing
graphics_result_t test_window_manager(void);

// V2 compatibility layer functions
bool graphics_is_initialized_v2_compat(void);
framebuffer_t* graphics_get_framebuffer_v2_compat(void);
graphics_device_t* graphics_get_primary_device_v2_compat(void);
graphics_result_t graphics_set_mode_v2_compat(uint32_t width, uint32_t height, 
                                             uint32_t bpp, uint32_t refresh_rate);
graphics_result_t graphics_clear_screen_v2_compat(graphics_color_t color);

#endif // GRAPHICS_INIT_H