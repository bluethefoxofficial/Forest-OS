#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/display_driver.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/graphics/window_manager.h"
#include "../include/graphics/font_renderer.h"
#include "../include/graphics/app_graphics.h"
#include "../include/string.h"
#include "../include/libc/stdio.h"
#include "../include/debuglog.h"

// Driver initialization functions - framebuffer only
extern graphics_result_t vesa_init(void);
extern graphics_result_t bga_init(void);

// Graphics subsystem initialization table - framebuffer only
static struct {
    const char* name;
    graphics_result_t (*init_func)(void);
    bool required;
} graphics_drivers[] = {
    // Bochs BGA driver for emulated environments
    {"Bochs BGA", bga_init, false},
    
    // VESA driver for enhanced graphics
    {"VESA", vesa_init, false},
    
    // Additional drivers can be added here
    // {"VMware SVGA", vmware_svga_init, false},
};

static const size_t num_graphics_drivers = sizeof(graphics_drivers) / sizeof(graphics_drivers[0]);

graphics_result_t initialize_graphics_subsystem(void) {
    debuglog(DEBUG_INFO, "Initializing Forest-OS graphics subsystem...\n");
    
    graphics_result_t overall_result = GRAPHICS_SUCCESS;
    bool has_driver = false;
    
    // Register all available drivers
    debuglog(DEBUG_INFO, "Registering graphics drivers for framebuffer TTY...\n");
    for (size_t i = 0; i < num_graphics_drivers; i++) {
        debuglog(DEBUG_INFO, "Initializing driver: %s\n", graphics_drivers[i].name);
        
        graphics_result_t result = graphics_drivers[i].init_func();
        if (result == GRAPHICS_SUCCESS) {
            debuglog(DEBUG_INFO, "Successfully registered %s\n", graphics_drivers[i].name);
            has_driver = true;
        } else {
            debuglog(DEBUG_ERROR, "Failed to register %s: %s\n", 
                    graphics_drivers[i].name, graphics_get_error_string(result));
            
            if (graphics_drivers[i].required) {
                debuglog(DEBUG_FATAL, "Required driver %s failed to load!\n", 
                        graphics_drivers[i].name);
                overall_result = result;
            }
        }
    }
    
    if (!has_driver) {
        debuglog(DEBUG_FATAL, "No graphics drivers loaded! Framebuffer TTY requires graphics support.\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Initialize the graphics manager
    debuglog(DEBUG_INFO, "Initializing graphics manager...\n");
    graphics_result_t result = graphics_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Graphics manager initialization failed: %s\n", 
                graphics_get_error_string(result));
        return result;
    }
    
    // Initialize the window manager
    debuglog(DEBUG_INFO, "Initializing window manager...\n");
    result = window_manager_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Window manager initialization failed: %s\n", 
                graphics_get_error_string(result));
        return result;
    }
    
    // Initialize the font renderer
    debuglog(DEBUG_INFO, "Initializing font renderer...\n");
    result = font_renderer_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Font renderer initialization failed: %s\n", 
                graphics_get_error_string(result));
        return result;
    }
    
    // Initialize the application graphics API
    debuglog(DEBUG_INFO, "Initializing application graphics API...\n");
    result = app_graphics_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Application graphics API initialization failed: %s\n", 
                graphics_get_error_string(result));
        return result;
    }
    
    // Print initialization summary
    debuglog(DEBUG_INFO, "Graphics subsystem initialization complete!\n");
    debuglog(DEBUG_INFO, "Primary device: %s\n", 
            graphics_get_primary_device() ? graphics_get_primary_device()->name : "None");
    debuglog(DEBUG_INFO, "Total graphics devices: %u\n", graphics_get_device_count());
    
    // Test basic functionality with fallback
    graphics_result_t test_result = graphics_clear_screen(COLOR_BLACK);
    if (test_result == GRAPHICS_SUCCESS) {
        graphics_write_string(0, 0, "Forest-OS Graphics System Initialized", TEXT_ATTR_WHITE);
        graphics_write_string(0, 1, "Graphics mode driver active", TEXT_ATTR_LIGHT_GREEN);
    } else {
        // Graphics mode failed, fall back to direct VGA text mode
        volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;
        const char* message = "Forest-OS fallback text mode";
        for (int i = 0; message[i] != '\0'; i++) {
            vga_buffer[i] = (uint16_t)message[i] | 0x0F00; // White on black
        }
    }
    
    return overall_result;
}

graphics_result_t shutdown_graphics_subsystem(void) {
    debuglog(DEBUG_INFO, "Shutting down graphics subsystem...\n");
    
    // Shutdown application graphics API first
    graphics_result_t app_result = app_graphics_shutdown();
    if (app_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Application graphics API shutdown failed: %s\n", 
                graphics_get_error_string(app_result));
    }
    
    // Shutdown font renderer
    graphics_result_t font_result = font_renderer_shutdown();
    if (font_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Font renderer shutdown failed: %s\n", 
                graphics_get_error_string(font_result));
    }
    
    // Shutdown window manager
    graphics_result_t wm_result = window_manager_shutdown();
    if (wm_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Window manager shutdown failed: %s\n", 
                graphics_get_error_string(wm_result));
    }
    
    graphics_result_t result = graphics_shutdown();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Graphics shutdown failed: %s\n", 
                graphics_get_error_string(result));
    }
    
    debuglog(DEBUG_INFO, "Graphics subsystem shutdown complete\n");
    return result;
}

// Simple graphics test function
graphics_result_t test_graphics_functionality(void) {
    debuglog(DEBUG_INFO, "Running graphics functionality test...\n");
    
    if (!graphics_is_initialized()) {
        debuglog(DEBUG_ERROR, "Graphics not initialized for test\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Test text output with different colors
    graphics_clear_screen(COLOR_BLACK);
    
    // Test header
    graphics_write_string(0, 0, "Forest-OS Graphics Test", TEXT_ATTR_WHITE);
    graphics_write_string(0, 1, "=====================", TEXT_ATTR_WHITE);
    
    // Test colors
    graphics_write_string(0, 3, "Text Color Test:", TEXT_ATTR_WHITE);
    graphics_write_string(0, 4, "Red text", TEXT_ATTR_RED);
    graphics_write_string(10, 4, "Green text", TEXT_ATTR_GREEN);
    graphics_write_string(22, 4, "Blue text", TEXT_ATTR_BLUE);
    graphics_write_string(32, 4, "Yellow text", TEXT_ATTR_YELLOW);
    
    // Test cursor positioning
    graphics_write_string(0, 6, "Cursor positioning test:", TEXT_ATTR_WHITE);
    for (int i = 0; i < 10; i++) {
        graphics_write_char(i * 2, 7, '0' + i, TEXT_ATTR_CYAN);
    }
    
    // Test basic drawing functionality if in graphics mode
    video_mode_t current_mode;
    if (graphics_get_current_mode(&current_mode) == GRAPHICS_SUCCESS) {
        if (!current_mode.is_text_mode) {
            debuglog(DEBUG_INFO, "Testing graphics mode drawing...\n");
            
            // Test pixel drawing
            for (int x = 0; x < 50; x++) {
                graphics_draw_pixel(100 + x, 100, COLOR_RED);
            }
            
            // Test rectangle drawing
            graphics_rect_t test_rect = {200, 100, 100, 50};
            graphics_draw_rect(&test_rect, COLOR_BLUE, false);
            
            graphics_rect_t filled_rect = {350, 100, 80, 40};
            graphics_draw_rect(&filled_rect, COLOR_GREEN, true);
        }
    }
    
    // Test scrolling (if supported)
    graphics_write_string(0, 9, "Testing basic functionality...", TEXT_ATTR_LIGHT_GRAY);
    graphics_write_string(0, 10, "Graphics subsystem test complete!", TEXT_ATTR_LIGHT_GREEN);
    
    // Get and display current mode info
    if (graphics_get_current_mode(&current_mode) == GRAPHICS_SUCCESS) {
        char mode_info[80];
        snprintf(mode_info, sizeof(mode_info), 
                "Current mode: %ux%u, %ubpp, %s", 
                current_mode.width, current_mode.height, current_mode.bpp,
                current_mode.is_text_mode ? "Text" : "Graphics");
        graphics_write_string(0, 12, mode_info, TEXT_ATTR_LIGHT_CYAN);
    }
    
    debuglog(DEBUG_INFO, "Graphics functionality test completed\n");
    return GRAPHICS_SUCCESS;
}

// Window manager test function
graphics_result_t test_window_manager(void) {
    debuglog(DEBUG_INFO, "Running window manager test...\n");
    
    if (!window_manager_is_initialized()) {
        debuglog(DEBUG_ERROR, "Window manager not initialized for test\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Test window creation
    window_handle_t main_window = window_create(100, 100, 400, 300, "Test Window 1", WINDOW_FLAGS_DEFAULT);
    if (main_window == INVALID_WINDOW_HANDLE) {
        debuglog(DEBUG_ERROR, "Failed to create main test window\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    window_handle_t second_window = window_create(200, 150, 300, 200, "Test Window 2", WINDOW_FLAGS_DEFAULT);
    if (second_window == INVALID_WINDOW_HANDLE) {
        debuglog(DEBUG_ERROR, "Failed to create second test window\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Test window properties
    graphics_result_t result = window_set_title(main_window, "Updated Test Window");
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to update window title\n");
    }
    
    // Test window positioning
    result = window_set_position(second_window, 250, 200);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to reposition window\n");
    }
    
    // Test focus management
    result = window_focus(main_window);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to focus window\n");
    }
    
    window_handle_t focused = window_get_focused();
    if (focused != main_window) {
        debuglog(DEBUG_WARN, "Window focus test failed\n");
    }
    
    // Update compositor
    result = compositor_update();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Compositor update failed\n");
    }
    
    // Test window surface access
    graphics_surface_t* surface;
    result = window_get_surface(main_window, &surface);
    if (result == GRAPHICS_SUCCESS && surface) {
        debuglog(DEBUG_INFO, "Successfully accessed window surface (%ux%u)\n", 
                surface->width, surface->height);
        
        // Clear window to a test color
        if (surface->pixels) {
            uint32_t* pixels = (uint32_t*)surface->pixels;
            uint32_t test_color = graphics_color_to_pixel(COLOR_BLUE, surface->format);
            uint32_t pixel_count = (surface->pitch / 4) * surface->height;
            for (uint32_t i = 0; i < pixel_count; i++) {
                pixels[i] = test_color;
            }
        }
    }
    
    // Display window manager status
    debuglog(DEBUG_INFO, "Window manager test results:\n");
    debuglog(DEBUG_INFO, "- Created 2 test windows\n");
    debuglog(DEBUG_INFO, "- Focused window: %u\n", focused);
    debuglog(DEBUG_INFO, "- Window operations completed\n");
    
    // Clean up - destroy test windows
    window_destroy(main_window);
    window_destroy(second_window);
    
    debuglog(DEBUG_INFO, "Window manager test completed\n");
    return GRAPHICS_SUCCESS;
}
