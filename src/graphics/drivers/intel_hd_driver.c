#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/graphics/graphics_manager.h"
#include "../../include/debuglog.h"

// Intel HD Graphics register offsets (simplified subset)
#define INTEL_GMCH_CTRL         0x50    // Graphics Mode Control
#define INTEL_DSPBASE           0x70184 // Display Base Address
#define INTEL_DSPSTRIDE         0x70188 // Display Stride
#define INTEL_DSPSIZE           0x70190 // Display Size
#define INTEL_DSPPOS            0x70188 // Display Position
#define INTEL_DSPSURF           0x7019C // Display Surface
#define INTEL_DSPCNTR           0x70180 // Display Control

// Intel display control register bits
#define INTEL_DSPCNTR_ENABLE    (1 << 31)
#define INTEL_DSPCNTR_GAMMA     (1 << 30)
#define INTEL_DSPCNTR_FORMAT_MASK   (0xF << 26)
#define INTEL_DSPCNTR_RGB565    (0x5 << 26)
#define INTEL_DSPCNTR_RGB888    (0x6 << 26)
#define INTEL_DSPCNTR_ARGB8888  (0x7 << 26)

// Intel graphics memory layout
#define INTEL_GTT_BASE          0x80000000  // Graphics Translation Table base
#define INTEL_STOLEN_MEMORY     0xFE000000  // Stolen memory base (typical)
#define INTEL_MMIO_SIZE         0x80000     // 512KB MMIO space

// Driver state for Intel HD Graphics
static struct {
    bool initialized;
    graphics_device_t* device;
    void* mmio_base;            // Memory-mapped I/O registers
    void* gtt_base;             // Graphics Translation Table
    uintptr_t stolen_memory;    // Stolen system memory for graphics
    size_t stolen_size;
    
    // Current display configuration
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_bpp;
    pixel_format_t display_format;
    void* framebuffer;
    size_t framebuffer_size;
    
    // Hardware capabilities
    bool has_3d_engine;
    bool has_video_decode;
    bool supports_hdmi;
    uint32_t generation;        // Intel GPU generation (6=Sandy Bridge, 7=Ivy Bridge, etc.)
} intel_hd_state = {
    .initialized = false,
    .device = NULL,
    .mmio_base = NULL,
    .generation = 6  // Default to Gen 6 (Sandy Bridge)
};

// Common Intel HD Graphics modes
static const video_mode_t intel_hd_modes[] = {
    {640, 480, 16, 640*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {640, 480, 24, 640*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {800, 600, 16, 800*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {800, 600, 24, 800*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1024, 768, 16, 1024*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {1024, 768, 24, 1024*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1280, 1024, 16, 1280*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {1280, 1024, 24, 1280*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1366, 768, 24, 1366*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1920, 1080, 24, 1920*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
};

#define INTEL_HD_MODE_COUNT (sizeof(intel_hd_modes) / sizeof(intel_hd_modes[0]))

// Driver operation implementations
static graphics_result_t intel_hd_initialize(graphics_device_t* device);
static graphics_result_t intel_hd_shutdown(graphics_device_t* device);
static graphics_result_t intel_hd_reset(graphics_device_t* device);
static graphics_result_t intel_hd_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t intel_hd_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t intel_hd_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t intel_hd_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t intel_hd_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t intel_hd_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color);
static graphics_result_t intel_hd_wait_for_vsync(graphics_device_t* device);

// Helper functions
static graphics_result_t intel_detect_generation(graphics_device_t* device);
static graphics_result_t intel_setup_stolen_memory(void);
static graphics_result_t intel_setup_gtt(void);
static graphics_result_t intel_program_display_registers(const video_mode_t* mode);
static uint32_t intel_read_reg(uint32_t offset);
static void intel_write_reg(uint32_t offset, uint32_t value);
static graphics_result_t intel_enable_display_plane(void);
static graphics_result_t intel_disable_display_plane(void);

// Intel HD Graphics driver operations structure
static display_driver_ops_t intel_hd_ops = {
    .name = "intel_hd",
    .version = 1,
    .initialize = intel_hd_initialize,
    .shutdown = intel_hd_shutdown,
    .reset = intel_hd_reset,
    .enumerate_modes = intel_hd_enumerate_modes,
    .set_mode = intel_hd_set_mode,
    .get_current_mode = intel_hd_get_current_mode,
    .map_framebuffer = intel_hd_map_framebuffer,
    .unmap_framebuffer = NULL,
    .clear_screen = intel_hd_clear_screen,
    .draw_pixel = NULL,  // Use software fallback
    .draw_rect = NULL,   // Use software fallback
    .blit_surface = NULL,
    .set_cursor = NULL,
    .move_cursor = NULL,
    .show_cursor = NULL,
    .write_char = NULL,
    .write_string = NULL,
    .scroll_screen = NULL,
    .set_cursor_pos = NULL,
    .set_power_state = NULL,
    .hw_fill_rect = intel_hd_hw_fill_rect,
    .hw_copy_rect = NULL,
    .hw_line = NULL,
    .wait_for_vsync = intel_hd_wait_for_vsync,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = NULL
};

// Driver structure
DECLARE_DISPLAY_DRIVER(intel_hd, intel_hd_ops);

static graphics_result_t intel_hd_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing Intel HD Graphics driver for device %04x:%04x\n",
            device->vendor_id, device->device_id);
    
    intel_hd_state.device = device;
    
    // Detect Intel GPU generation
    graphics_result_t result = intel_detect_generation(device);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Could not detect Intel GPU generation, using default\n");
    }
    
    // Map MMIO space
    if (device->mmio_base && device->mmio_size) {
        intel_hd_state.mmio_base = (void*)device->mmio_base;
        debuglog(DEBUG_INFO, "Intel HD MMIO mapped at 0x%08X (size: %u KB)\n",
                (uint32_t)device->mmio_base, device->mmio_size / 1024);
    } else {
        debuglog(DEBUG_ERROR, "Intel HD Graphics: No MMIO region found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Setup stolen memory (graphics memory carved out from system RAM)
    result = intel_setup_stolen_memory();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to setup stolen memory, using framebuffer BAR\n");
        intel_hd_state.stolen_memory = device->framebuffer_base;
        intel_hd_state.stolen_size = device->framebuffer_size;
    }
    
    // Initialize Graphics Translation Table
    result = intel_setup_gtt();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Failed to setup GTT\n");
    }
    
    // Reset display hardware
    intel_hd_reset(device);
    
    // Set device capabilities
    device->caps.supports_2d_accel = true;
    device->caps.supports_3d_accel = intel_hd_state.has_3d_engine;
    device->caps.supports_hw_cursor = true;
    device->caps.supports_page_flipping = true;
    device->caps.supports_vsync = true;
    device->caps.max_resolution_x = 1920;
    device->caps.max_resolution_y = 1200;
    device->caps.video_memory_size = intel_hd_state.stolen_size;
    
    intel_hd_state.initialized = true;
    debuglog(DEBUG_INFO, "Intel HD Graphics driver initialized successfully (Gen %u)\n",
            intel_hd_state.generation);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_shutdown(graphics_device_t* device) {
    debuglog(DEBUG_INFO, "Shutting down Intel HD Graphics driver\n");
    
    if (intel_hd_state.initialized) {
        // Disable display plane
        intel_disable_display_plane();
        
        intel_hd_state.initialized = false;
        intel_hd_state.device = NULL;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_reset(graphics_device_t* device) {
    if (!device || !intel_hd_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Resetting Intel HD Graphics hardware\n");
    
    // Disable display plane
    intel_disable_display_plane();
    
    // Reset display registers to safe defaults
    intel_write_reg(INTEL_DSPBASE, 0);
    intel_write_reg(INTEL_DSPSTRIDE, 0);
    intel_write_reg(INTEL_DSPSIZE, 0);
    intel_write_reg(INTEL_DSPPOS, 0);
    intel_write_reg(INTEL_DSPSURF, 0);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = INTEL_HD_MODE_COUNT;
    *modes = kmalloc(sizeof(video_mode_t) * INTEL_HD_MODE_COUNT);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // Copy supported modes
    memcpy(*modes, intel_hd_modes, sizeof(video_mode_t) * INTEL_HD_MODE_COUNT);
    
    debuglog(DEBUG_INFO, "Intel HD Graphics enumerated %u display modes\n", *count);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !intel_hd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (mode->is_text_mode) {
        debuglog(DEBUG_WARN, "Intel HD Graphics doesn't support text modes\n");
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    debuglog(DEBUG_INFO, "Intel HD Graphics setting mode: %ux%ux%u\n",
            mode->width, mode->height, mode->bpp);
    
    // Validate mode
    bool mode_supported = false;
    for (uint32_t i = 0; i < INTEL_HD_MODE_COUNT; i++) {
        if (intel_hd_modes[i].width == mode->width &&
            intel_hd_modes[i].height == mode->height &&
            intel_hd_modes[i].bpp == mode->bpp) {
            mode_supported = true;
            break;
        }
    }
    
    if (!mode_supported) {
        debuglog(DEBUG_ERROR, "Unsupported mode: %ux%ux%u\n", mode->width, mode->height, mode->bpp);
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Disable display plane before mode change
    intel_disable_display_plane();
    
    // Program display registers
    graphics_result_t result = intel_program_display_registers(mode);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to program Intel display registers\n");
        return result;
    }
    
    // Update state
    intel_hd_state.display_width = mode->width;
    intel_hd_state.display_height = mode->height;
    intel_hd_state.display_bpp = mode->bpp;
    intel_hd_state.display_format = mode->format;
    intel_hd_state.framebuffer_size = mode->pitch * mode->height;
    
    // Update device current mode
    device->current_mode = *mode;
    
    // Enable display plane
    intel_enable_display_plane();
    
    debuglog(DEBUG_INFO, "Intel HD Graphics mode set successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !intel_hd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = intel_hd_state.display_width;
    mode->height = intel_hd_state.display_height;
    mode->bpp = intel_hd_state.display_bpp;
    mode->format = intel_hd_state.display_format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->pitch = (intel_hd_state.display_width * intel_hd_state.display_bpp) / 8;
    mode->mode_number = 0;  // Intel doesn't use mode numbers
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !intel_hd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t));
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // Use stolen memory for framebuffer
    framebuffer->physical_addr = intel_hd_state.stolen_memory;
    framebuffer->virtual_addr = (void*)intel_hd_state.stolen_memory; // Direct mapping
    framebuffer->size = intel_hd_state.framebuffer_size;
    framebuffer->width = intel_hd_state.display_width;
    framebuffer->height = intel_hd_state.display_height;
    framebuffer->pitch = (intel_hd_state.display_width * intel_hd_state.display_bpp) / 8;
    framebuffer->format = intel_hd_state.display_format;
    framebuffer->bpp = intel_hd_state.display_bpp;
    framebuffer->double_buffered = false;
    framebuffer->back_buffer = NULL;
    framebuffer->hw_cursor_available = true;
    framebuffer->cursor_data = NULL;
    
    intel_hd_state.framebuffer = framebuffer->virtual_addr;
    *fb = framebuffer;
    
    debuglog(DEBUG_INFO, "Intel HD framebuffer mapped at 0x%08X, size: %u bytes\n",
            (uint32_t)framebuffer->physical_addr, framebuffer->size);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !intel_hd_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    graphics_rect_t full_screen = {
        0, 0, intel_hd_state.display_width, intel_hd_state.display_height
    };
    
    return intel_hd_hw_fill_rect(device, &full_screen, color);
}

static graphics_result_t intel_hd_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color) {
    if (!device || !rect || !intel_hd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // For now, use software implementation
    // In a full implementation, this would use Intel's 2D blitter engine
    if (!intel_hd_state.framebuffer) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    uint32_t pixel_value = graphics_color_to_pixel(color, intel_hd_state.display_format);
    
    // Simple software fill
    for (uint32_t y = rect->y; y < rect->y + rect->height && y < intel_hd_state.display_height; y++) {
        for (uint32_t x = rect->x; x < rect->x + rect->width && x < intel_hd_state.display_width; x++) {
            switch (intel_hd_state.display_bpp) {
                case 16: {
                    uint16_t* fb = (uint16_t*)intel_hd_state.framebuffer;
                    fb[y * intel_hd_state.display_width + x] = (uint16_t)pixel_value;
                    break;
                }
                case 24:
                case 32: {
                    uint32_t* fb = (uint32_t*)intel_hd_state.framebuffer;
                    fb[y * intel_hd_state.display_width + x] = pixel_value;
                    break;
                }
            }
        }
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_hd_wait_for_vsync(graphics_device_t* device) {
    if (!device || !intel_hd_state.initialized || !intel_hd_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Simple vsync wait - poll display status
    // In a real implementation, you'd wait for vsync interrupt
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        // Simple delay - in reality you'd check vsync status register
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return GRAPHICS_SUCCESS;
}

// Helper function implementations
static graphics_result_t intel_detect_generation(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Detect Intel GPU generation based on device ID
    uint16_t device_id = device->device_id;
    
    if (device_id >= 0x0100 && device_id <= 0x0116) {
        intel_hd_state.generation = 6; // Sandy Bridge
        intel_hd_state.has_3d_engine = true;
    } else if (device_id >= 0x0150 && device_id <= 0x0166) {
        intel_hd_state.generation = 7; // Ivy Bridge
        intel_hd_state.has_3d_engine = true;
        intel_hd_state.has_video_decode = true;
    } else if (device_id >= 0x0400 && device_id <= 0x0426) {
        intel_hd_state.generation = 8; // Haswell
        intel_hd_state.has_3d_engine = true;
        intel_hd_state.has_video_decode = true;
        intel_hd_state.supports_hdmi = true;
    } else {
        intel_hd_state.generation = 6; // Default
        intel_hd_state.has_3d_engine = false;
    }
    
    debuglog(DEBUG_INFO, "Detected Intel GPU Generation %u (Device ID: 0x%04X)\n",
            intel_hd_state.generation, device_id);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_setup_stolen_memory(void) {
    // Intel graphics uses "stolen" system memory
    // This would typically be configured by BIOS/UEFI
    
    // Read stolen memory configuration from GMCH control register
    uint32_t gmch_ctrl = intel_read_reg(INTEL_GMCH_CTRL);
    
    // Decode stolen memory size (simplified)
    uint32_t stolen_size_mb = 0;
    uint8_t size_field = (gmch_ctrl >> 4) & 0xF;
    
    switch (size_field) {
        case 1: stolen_size_mb = 1; break;
        case 2: stolen_size_mb = 4; break;
        case 3: stolen_size_mb = 8; break;
        case 4: stolen_size_mb = 16; break;
        case 5: stolen_size_mb = 32; break;
        case 6: stolen_size_mb = 48; break;
        case 7: stolen_size_mb = 64; break;
        case 8: stolen_size_mb = 128; break;
        case 9: stolen_size_mb = 256; break;
        case 10: stolen_size_mb = 96; break;
        case 11: stolen_size_mb = 160; break;
        case 12: stolen_size_mb = 224; break;
        case 13: stolen_size_mb = 352; break;
        default: stolen_size_mb = 32; break; // Default
    }
    
    intel_hd_state.stolen_size = stolen_size_mb * 1024 * 1024;
    intel_hd_state.stolen_memory = INTEL_STOLEN_MEMORY; // Typical location
    
    debuglog(DEBUG_INFO, "Intel stolen memory: %u MB at 0x%08X\n",
            stolen_size_mb, (uint32_t)intel_hd_state.stolen_memory);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_setup_gtt(void) {
    // Graphics Translation Table setup
    // This would map graphics addresses to physical memory
    intel_hd_state.gtt_base = (void*)INTEL_GTT_BASE;
    
    debuglog(DEBUG_INFO, "Intel GTT setup at 0x%08X\n", (uint32_t)intel_hd_state.gtt_base);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_program_display_registers(const video_mode_t* mode) {
    if (!mode || !intel_hd_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate register values
    uint32_t stride = mode->pitch;
    uint32_t size = ((mode->height - 1) << 16) | (mode->width - 1);
    uint32_t base_addr = (uint32_t)intel_hd_state.stolen_memory;
    
    // Determine pixel format
    uint32_t format_bits = 0;
    switch (mode->bpp) {
        case 16: format_bits = INTEL_DSPCNTR_RGB565; break;
        case 24: format_bits = INTEL_DSPCNTR_RGB888; break;
        case 32: format_bits = INTEL_DSPCNTR_ARGB8888; break;
        default: return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Program registers
    intel_write_reg(INTEL_DSPSTRIDE, stride);
    intel_write_reg(INTEL_DSPSIZE, size);
    intel_write_reg(INTEL_DSPPOS, 0);  // Position at 0,0
    intel_write_reg(INTEL_DSPBASE, base_addr);
    intel_write_reg(INTEL_DSPSURF, base_addr);
    
    // Configure display control (but don't enable yet)
    uint32_t dspcntr = format_bits | INTEL_DSPCNTR_GAMMA;
    intel_write_reg(INTEL_DSPCNTR, dspcntr);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_enable_display_plane(void) {
    if (!intel_hd_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Enable display plane
    uint32_t dspcntr = intel_read_reg(INTEL_DSPCNTR);
    dspcntr |= INTEL_DSPCNTR_ENABLE;
    intel_write_reg(INTEL_DSPCNTR, dspcntr);
    
    // Trigger surface update
    uint32_t surf = intel_read_reg(INTEL_DSPSURF);
    intel_write_reg(INTEL_DSPSURF, surf);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t intel_disable_display_plane(void) {
    if (!intel_hd_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Disable display plane
    uint32_t dspcntr = intel_read_reg(INTEL_DSPCNTR);
    dspcntr &= ~INTEL_DSPCNTR_ENABLE;
    intel_write_reg(INTEL_DSPCNTR, dspcntr);
    
    return GRAPHICS_SUCCESS;
}

static uint32_t intel_read_reg(uint32_t offset) {
    if (!intel_hd_state.mmio_base) {
        return 0;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)intel_hd_state.mmio_base + offset);
    return *reg;
}

static void intel_write_reg(uint32_t offset, uint32_t value) {
    if (!intel_hd_state.mmio_base) {
        return;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)intel_hd_state.mmio_base + offset);
    *reg = value;
}

// Driver initialization function
DRIVER_INIT_FUNCTION(intel_hd) {
    debuglog(DEBUG_INFO, "Registering Intel HD Graphics driver\n");
    
    // Set driver flags
    intel_hd_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE | 
                           DRIVER_FLAG_SUPPORTS_HW_CURSOR |
                           DRIVER_FLAG_SUPPORTS_VSYNC;
    
    return register_display_driver(&intel_hd_driver);
}
