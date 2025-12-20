#ifndef GRAPHICS_INIT_H
#define GRAPHICS_INIT_H

#include "graphics/graphics_types.h"
#include "graphics/window_manager.h"

// Graphics subsystem initialization and shutdown
graphics_result_t initialize_graphics_subsystem(void);
graphics_result_t shutdown_graphics_subsystem(void);

// Graphics functionality testing
graphics_result_t test_graphics_functionality(void);

// Window manager testing
graphics_result_t test_window_manager(void);

#endif // GRAPHICS_INIT_H