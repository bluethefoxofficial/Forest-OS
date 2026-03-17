/**
 * Forest OS - Graphics Subsystem Initialization (V2)
 * 
 * This file initializes the V2 graphics driver system and provides
 * the interface between the old API and the new driver architecture.
 */

#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/graphics_driver_v2.h"
#include "../include/graphics/display_driver.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/graphics/window_manager.h"
#include "../include/graphics/font_renderer.h"
#include "../include/graphics/app_graphics.h"
#include "../include/string.h"
#include "../include/libc/stdio.h"
#include "../include/debuglog.h"
#include "../include/memory.h"

/* External V2 driver initialization functions */
extern gfx_result_t bga_driver_init(void);
extern gfx_result_t svga_driver_init(void);
extern gfx_result_t vesa_driver_init(void);
extern gfx_result_t vga_text_driver_init(void);
extern gfx_result_t intel_driver_init(void);
extern gfx_result_t amd_driver_init(void);
extern gfx_result_t nv_driver_init(void);

/* External V2 graphics manager functions */
extern gfx_result_t gfx_init(void);
extern gfx_result_t gfx_shutdown(void);
extern gfx_result_t gfx_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
extern gfx_result_t gfx_get_framebuffer(gfx_framebuffer_t** fb);
extern gfx_result_t gfx_clear_screen(gfx_color_t color);
extern bool gfx_is_initialized(void);
extern void* gfx_get_fb_addr(void);
extern uint32_t gfx_get_fb_width(void);
extern uint32_t gfx_get_fb_height(void);
extern uint32_t gfx_get_fb_pitch(void);
extern uint32_t gfx_get_fb_bpp(void);
extern void gfx_print_status(void);

/* Global state for compatibility bridge */
static bool g_graphics_v2_initialized = false;
static framebuffer_t g_compat_framebuffer = {0};
static graphics_device_t g_compat_device = {0};

/**
 * Convert V2 result code to legacy result code
 */
static graphics_result_t convert_v2_result(gfx_result_t result) {
    switch (result) {
        case GFX_OK: return GRAPHICS_SUCCESS;
        case GFX_ERR_INVALID_PARAM: return GRAPHICS_ERROR_INVALID_PARAMETER;
        case GFX_ERR_NO_MEMORY: return GRAPHICS_ERROR_OUT_OF_MEMORY;
        case GFX_ERR_NOT_SUPPORTED: return GRAPHICS_ERROR_NOT_SUPPORTED;
        case GFX_ERR_HARDWARE: return GRAPHICS_ERROR_HARDWARE_FAULT;
        case GFX_ERR_MODE_NOT_FOUND: return GRAPHICS_ERROR_INVALID_MODE;
        case GFX_ERR_DEVICE_BUSY: return GRAPHICS_ERROR_DEVICE_BUSY;
        case GFX_ERR_NO_DRIVER: return GRAPHICS_ERROR_GENERIC;
        case GFX_ERR_INIT_FAILED: return GRAPHICS_ERROR_GENERIC;
        case GFX_ERR_MAPPING_FAILED: return GRAPHICS_ERROR_OUT_OF_MEMORY;
        default: return GRAPHICS_ERROR_GENERIC;
    }
}

/**
 * Update the compatibility framebuffer from V2 system
 */
static void update_compat_framebuffer(void) {
    gfx_framebuffer_t* v2_fb = NULL;
    
    if (gfx_get_framebuffer(&v2_fb) == GFX_OK && v2_fb) {
        g_compat_framebuffer.virtual_addr = (uintptr_t)v2_fb->virt_addr;
        g_compat_framebuffer.physical_addr = v2_fb->phys_addr;
        g_compat_framebuffer.width = v2_fb->width;
        g_compat_framebuffer.height = v2_fb->height;
        g_compat_framebuffer.pitch = v2_fb->pitch;
        g_compat_framebuffer.bpp = v2_fb->bpp;
        g_compat_framebuffer.size = v2_fb->size;
        
        /* Convert pixel format */
        switch (v2_fb->format) {
            case GFX_FORMAT_BGRX8888:
            case GFX_FORMAT_BGRA8888:
                g_compat_framebuffer.format = PIXEL_FORMAT_BGRA_8888;
                break;
            case GFX_FORMAT_RGBX8888:
            case GFX_FORMAT_RGBA8888:
                g_compat_framebuffer.format = PIXEL_FORMAT_RGBA_8888;
                break;
            case GFX_FORMAT_BGR565:
            case GFX_FORMAT_RGB565:
                g_compat_framebuffer.format = PIXEL_FORMAT_RGB_565;
                break;
            case GFX_FORMAT_BGR555:
            case GFX_FORMAT_RGB555:
                g_compat_framebuffer.format = PIXEL_FORMAT_RGB_555;
                break;
            default:
                g_compat_framebuffer.format = PIXEL_FORMAT_BGRA_8888;
                break;
        }
        
        debuglog(DEBUG_INFO, "[GFXINIT] Framebuffer updated: %ux%ux%u @ 0x%lx\n",
                 g_compat_framebuffer.width, g_compat_framebuffer.height,
                 g_compat_framebuffer.bpp, (unsigned long)g_compat_framebuffer.virtual_addr);
    }
}

/**
 * Initialize the graphics subsystem using V2 drivers
 */
graphics_result_t initialize_graphics_subsystem(void) {
    debuglog(DEBUG_INFO, "[GFXINIT] Initializing Forest-OS graphics subsystem (V2)...\n");
    
    /* Initialize the V2 graphics system */
    gfx_result_t v2_result = gfx_init();
    if (v2_result != GFX_OK) {
        debuglog(DEBUG_ERROR, "[GFXINIT] V2 graphics init failed: %d\n", v2_result);
        return convert_v2_result(v2_result);
    }
    
    g_graphics_v2_initialized = true;
    
    /* Update compatibility layer */
    update_compat_framebuffer();
    
    /* Debug: Show what framebuffer we got from V2 system */
    debuglog(DEBUG_INFO, "[GFXINIT] Compat framebuffer after V2 init: %ux%u %ubpp virt=0x%08x\n",
            g_compat_framebuffer.width, g_compat_framebuffer.height,
            g_compat_framebuffer.bpp, (uint32_t)g_compat_framebuffer.virtual_addr);
    
    /* Set up compatibility device info */
    g_compat_device.type = GRAPHICS_DEVICE_VESA;  /* Default */
    strncpy(g_compat_device.name, "V2 Graphics Device", sizeof(g_compat_device.name) - 1);
    g_compat_device.is_active = true;
    
    /* Print V2 system status */
    gfx_print_status();
    
    /* CRITICAL: Initialize the legacy graphics manager before window manager!
     * This sets graphics_state.initialized which is required for 
     * graphics_get_current_mode() to work in window_manager_init() */
    debuglog(DEBUG_INFO, "[GFXINIT] Initializing legacy graphics manager bridge...\n");
    graphics_result_t legacy_result = graphics_init();
    if (legacy_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "[GFXINIT] Legacy graphics manager init failed: %s\n",
                graphics_get_error_string(legacy_result));
        /* Continue anyway - try to make window manager work */
    } else {
        debuglog(DEBUG_INFO, "[GFXINIT] Legacy graphics manager initialized successfully\n");
    }
    
    /* Skip window manager initialization during early boot to prevent red screen */
    debuglog(DEBUG_INFO, "[GFXINIT] Skipping window manager init during early boot\n");
    graphics_result_t result = GRAPHICS_SUCCESS;
    
    /* Initialize the font renderer */
    debuglog(DEBUG_INFO, "[GFXINIT] Initializing font renderer...\n");
    result = font_renderer_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "[GFXINIT] Font renderer initialization failed: %s\n", 
                graphics_get_error_string(result));
        /* Continue anyway */
    }
    
    /* Initialize the application graphics API */
    debuglog(DEBUG_INFO, "[GFXINIT] Initializing application graphics API...\n");
    result = app_graphics_init();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "[GFXINIT] Application graphics API initialization failed: %s\n", 
                graphics_get_error_string(result));
        /* Continue anyway */
    }
    
    /* Print initialization summary */
    debuglog(DEBUG_INFO, "[GFXINIT] Graphics subsystem initialization complete!\n");
    debuglog(DEBUG_INFO, "[GFXINIT] Framebuffer: %ux%u %ubpp, pitch=%u\n",
            g_compat_framebuffer.width, g_compat_framebuffer.height,
            g_compat_framebuffer.bpp, g_compat_framebuffer.pitch);
    
    /* Skip clear screen test - framebuffer may not be accessible */
    debuglog(DEBUG_INFO, "[GFXINIT] Skipping screen clear test\n");
    
    return GRAPHICS_SUCCESS;
}

/**
 * Shutdown the graphics subsystem
 */
graphics_result_t shutdown_graphics_subsystem(void) {
    debuglog(DEBUG_INFO, "[GFXINIT] Shutting down graphics subsystem...\n");
    
    /* Shutdown application graphics API first */
    graphics_result_t app_result = app_graphics_shutdown();
    if (app_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Application graphics API shutdown failed: %s\n", 
                graphics_get_error_string(app_result));
    }
    
    /* Shutdown font renderer */
    graphics_result_t font_result = font_renderer_shutdown();
    if (font_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Font renderer shutdown failed: %s\n", 
                graphics_get_error_string(font_result));
    }
    
    /* Shutdown window manager */
    graphics_result_t wm_result = window_manager_shutdown();
    if (wm_result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Window manager shutdown failed: %s\n", 
                graphics_get_error_string(wm_result));
    }
    
    /* Shutdown V2 graphics system */
    if (g_graphics_v2_initialized) {
        gfx_shutdown();
        g_graphics_v2_initialized = false;
    }
    
    debuglog(DEBUG_INFO, "[GFXINIT] Graphics subsystem shutdown complete\n");
    return GRAPHICS_SUCCESS;
}

/**
 * Test graphics functionality
 */
graphics_result_t test_graphics_functionality(void) {
    debuglog(DEBUG_INFO, "[GFXINIT] Running graphics functionality test...\n");
    
    if (!g_graphics_v2_initialized) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Graphics not initialized for test\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    /* Test screen clear with different colors */
    gfx_color_t colors[] = {
        {255, 0, 0, 255},    /* Red */
        {0, 255, 0, 255},    /* Green */
        {0, 0, 255, 255},    /* Blue */
        {0, 0, 0, 255},      /* Black */
    };
    
    for (int i = 0; i < 4; i++) {
        gfx_result_t result = gfx_clear_screen(colors[i]);
        if (result != GFX_OK) {
            debuglog(DEBUG_ERROR, "[GFXINIT] Clear screen test %d failed\n", i);
            return GRAPHICS_ERROR_GENERIC;
        }
        /* Brief delay to see colors */
        for (volatile int j = 0; j < 1000000; j++);
    }
    
    debuglog(DEBUG_INFO, "[GFXINIT] Graphics functionality test completed\n");
    return GRAPHICS_SUCCESS;
}

/**
 * Test window manager
 */
graphics_result_t test_window_manager(void) {
    debuglog(DEBUG_INFO, "[GFXINIT] Running window manager test...\n");
    
    if (!window_manager_is_initialized()) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Window manager not initialized for test\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    /* Test window creation */
    window_handle_t test_window = window_create(100, 100, 400, 300, "Test Window", WINDOW_FLAGS_DEFAULT);
    if (test_window == INVALID_WINDOW_HANDLE) {
        debuglog(DEBUG_ERROR, "[GFXINIT] Failed to create test window\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    /* Test window operations */
    window_set_title(test_window, "Updated Test Window");
    window_focus(test_window);
    compositor_update();
    
    /* Clean up */
    window_destroy(test_window);
    
    debuglog(DEBUG_INFO, "[GFXINIT] Window manager test completed\n");
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Compatibility functions for the old graphics API
 * These bridge the old API to the new V2 system
 * ============================================================================ */

/**
 * Check if graphics is initialized (compatibility)
 */
bool graphics_is_initialized_v2_compat(void) {
    return g_graphics_v2_initialized;
}

/**
 * Get framebuffer (compatibility)
 */
framebuffer_t* graphics_get_framebuffer_v2_compat(void) {
    if (!g_graphics_v2_initialized) {
        return NULL;
    }
    
    update_compat_framebuffer();
    return &g_compat_framebuffer;
}

/**
 * Get primary device (compatibility)
 */
graphics_device_t* graphics_get_primary_device_v2_compat(void) {
    if (!g_graphics_v2_initialized) {
        return NULL;
    }
    return &g_compat_device;
}

/**
 * Set video mode (compatibility)
 */
graphics_result_t graphics_set_mode_v2_compat(uint32_t width, uint32_t height, 
                                             uint32_t bpp, uint32_t refresh_rate) {
    (void)refresh_rate;  /* V2 doesn't use refresh rate directly */
    
    gfx_result_t result = gfx_set_mode(width, height, bpp);
    if (result == GFX_OK) {
        update_compat_framebuffer();
    }
    return convert_v2_result(result);
}

/**
 * Clear screen (compatibility)
 */
graphics_result_t graphics_clear_screen_v2_compat(graphics_color_t color) {
    gfx_color_t v2_color = {color.r, color.g, color.b, color.a};
    return convert_v2_result(gfx_clear_screen(v2_color));
}
