#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/graphics/graphics_manager.h"
#include "../../include/debuglog.h"

// NVIDIA GPU register offsets (based on open documentation)
// These are simplified registers for basic operations
#define NVIDIA_PMC_BOOT_0           0x000000  // Boot/identification register
#define NVIDIA_PMC_ENABLE           0x000200  // Master enable register
#define NVIDIA_PFB_CFG              0x100200  // Frame buffer configuration
#define NVIDIA_PFB_CSTATUS          0x10020C  // Frame buffer controller status
#define NVIDIA_PRAMDAC_NVPLL_COEFF  0x680500  // PLL coefficients for pixel clock

// Display controller registers (simplified)
#define NVIDIA_PCRTC_CONFIG         0x600800  // CRTC configuration
#define NVIDIA_PCRTC_START          0x600804  // CRTC start address
#define NVIDIA_PCRTC_SIZE           0x600808  // CRTC size register
#define NVIDIA_PCRTC_CURSOR_CONFIG  0x600810  // Hardware cursor config

// GPU Control registers
#define NVIDIA_PGRAPH_DEBUG_0       0x400080  // Graphics debug register
#define NVIDIA_PGRAPH_DEBUG_1       0x400084  // Graphics debug register
#define NVIDIA_PGRAPH_INTR_0        0x400100  // Graphics interrupt register
#define NVIDIA_PGRAPH_FIFO          0x4006A4  // Graphics FIFO control

// Memory controller registers
#define NVIDIA_PFB_CONFIG_0         0x100200  // Memory configuration
#define NVIDIA_PFB_CONFIG_1         0x100204  // Memory configuration
#define NVIDIA_PFB_DEBUG_0          0x100080  // Memory debug register

// NVIDIA GPU families and generations
typedef enum {
    NVIDIA_ARCH_UNKNOWN = 0,
    NVIDIA_ARCH_FERMI,      // GeForce GTX 400/500 series
    NVIDIA_ARCH_KEPLER,     // GeForce GTX 600/700 series
    NVIDIA_ARCH_MAXWELL,    // GeForce GTX 900/GTX 10 series
    NVIDIA_ARCH_PASCAL,     // GeForce GTX 10/16 series  
    NVIDIA_ARCH_TURING,     // GeForce RTX 20/GTX 16 series
    NVIDIA_ARCH_AMPERE,     // GeForce RTX 30 series
    NVIDIA_ARCH_ADA_LOVELACE, // GeForce RTX 40 series
    NVIDIA_ARCH_HOPPER,     // H100, enterprise
    NVIDIA_ARCH_BLACKWELL   // Future architecture
} nvidia_gpu_arch_t;

// GPU capabilities structure
typedef struct {
    nvidia_gpu_arch_t architecture;
    bool has_unified_memory;
    bool has_gpu_boost;
    bool has_display_engine;
    bool has_nvenc;
    bool has_nvdec;
    bool has_rt_cores;
    bool has_tensor_cores;
    uint32_t cuda_cores;
    uint32_t compute_capability_major;
    uint32_t compute_capability_minor;
    uint32_t max_threads_per_block;
    uint32_t max_blocks_per_sm;
} nvidia_gpu_caps_t;

// Driver state for NVIDIA Graphics
static struct {
    bool initialized;
    graphics_device_t* device;
    void* mmio_base;            // Memory-mapped I/O registers
    size_t mmio_size;
    void* framebuffer_base;     // Framebuffer memory
    size_t vram_size;
    
    // GPU information  
    nvidia_gpu_arch_t architecture;
    nvidia_gpu_caps_t capabilities;
    uint32_t gpu_id;            // GPU identification
    
    // Current display configuration
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_bpp;
    pixel_format_t display_format;
    uint32_t display_pitch;
    
    // Hardware state
    bool display_enabled;
    bool gpu_initialized;
    
    // Memory management
    void* gpu_memory_pool;
    size_t gpu_memory_pool_size;
    
} nvidia_state = {
    .initialized = false,
    .device = NULL,
    .mmio_base = NULL,
    .architecture = NVIDIA_ARCH_UNKNOWN,
    .display_enabled = false,
    .gpu_initialized = false
};

// Common NVIDIA display modes
static const video_mode_t nvidia_modes[] = {
    {640, 480, 16, 640*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {640, 480, 24, 640*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {640, 480, 32, 640*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {800, 600, 16, 800*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {800, 600, 24, 800*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {800, 600, 32, 800*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {1024, 768, 16, 1024*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {1024, 768, 24, 1024*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1024, 768, 32, 1024*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {1280, 720, 24, 1280*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1280, 1024, 24, 1280*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1366, 768, 24, 1366*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1600, 900, 24, 1600*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1920, 1080, 24, 1920*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1920, 1080, 32, 1920*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {2560, 1440, 24, 2560*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {3840, 2160, 24, 3840*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
};

#define NVIDIA_MODE_COUNT (sizeof(nvidia_modes) / sizeof(nvidia_modes[0]))

// Driver operation implementations
static graphics_result_t nvidia_initialize(graphics_device_t* device);
static graphics_result_t nvidia_shutdown(graphics_device_t* device);
static graphics_result_t nvidia_reset(graphics_device_t* device);
static graphics_result_t nvidia_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t nvidia_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t nvidia_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t nvidia_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t nvidia_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t nvidia_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color);
static graphics_result_t nvidia_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y);
static graphics_result_t nvidia_wait_for_vsync(graphics_device_t* device);

// Helper functions
static graphics_result_t nvidia_detect_architecture(graphics_device_t* device);
static graphics_result_t nvidia_init_gpu(void);
static graphics_result_t nvidia_setup_display_engine(const video_mode_t* mode);
static graphics_result_t nvidia_enable_display(void);
static graphics_result_t nvidia_disable_display(void);
static graphics_result_t nvidia_wait_for_idle(void);
static uint32_t nvidia_read_reg(uint32_t offset);
static void nvidia_write_reg(uint32_t offset, uint32_t value);
static const char* nvidia_arch_to_string(nvidia_gpu_arch_t arch);

// NVIDIA driver operations structure
static display_driver_ops_t nvidia_ops = {
    .name = "nvidia",
    .version = 1,
    .initialize = nvidia_initialize,
    .shutdown = nvidia_shutdown,
    .reset = nvidia_reset,
    .enumerate_modes = nvidia_enumerate_modes,
    .set_mode = nvidia_set_mode,
    .get_current_mode = nvidia_get_current_mode,
    .map_framebuffer = nvidia_map_framebuffer,
    .unmap_framebuffer = NULL,
    .clear_screen = nvidia_clear_screen,
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
    .hw_fill_rect = nvidia_hw_fill_rect,
    .hw_copy_rect = nvidia_hw_copy_rect,
    .hw_line = NULL,
    .wait_for_vsync = nvidia_wait_for_vsync,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = NULL
};

// Driver structure
DECLARE_DISPLAY_DRIVER(nvidia, nvidia_ops);

static graphics_result_t nvidia_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing NVIDIA Graphics driver for device %04x:%04x\n",
            device->vendor_id, device->device_id);
    
    nvidia_state.device = device;
    
    // Detect GPU architecture
    graphics_result_t result = nvidia_detect_architecture(device);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Could not detect NVIDIA GPU architecture\n");
        nvidia_state.architecture = NVIDIA_ARCH_UNKNOWN;
    }
    
    // Map MMIO space
    if (device->mmio_base && device->mmio_size) {
        nvidia_state.mmio_base = (void*)device->mmio_base;
        nvidia_state.mmio_size = device->mmio_size;
        debuglog(DEBUG_INFO, "NVIDIA MMIO mapped at 0x%08X (size: %u KB)\n",
                (uint32_t)device->mmio_base, device->mmio_size / 1024);
    } else {
        debuglog(DEBUG_ERROR, "NVIDIA Graphics: No MMIO region found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Map framebuffer
    if (device->framebuffer_base && device->framebuffer_size) {
        nvidia_state.framebuffer_base = (void*)device->framebuffer_base;
        nvidia_state.vram_size = device->framebuffer_size;
        debuglog(DEBUG_INFO, "NVIDIA framebuffer at 0x%08X (size: %u MB)\n",
                (uint32_t)device->framebuffer_base, device->framebuffer_size / (1024*1024));
    } else {
        debuglog(DEBUG_ERROR, "NVIDIA Graphics: No framebuffer region found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Reset and initialize GPU
    nvidia_reset(device);
    
    // Initialize GPU subsystems
    result = nvidia_init_gpu();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to initialize NVIDIA GPU\n");
        return result;
    }
    
    // Set device capabilities based on detected architecture
    device->caps.supports_2d_accel = true;
    device->caps.supports_3d_accel = (nvidia_state.architecture >= NVIDIA_ARCH_FERMI);
    device->caps.supports_hw_cursor = true;
    device->caps.supports_page_flipping = true;
    device->caps.supports_vsync = true;
    device->caps.supports_multiple_heads = (nvidia_state.architecture >= NVIDIA_ARCH_KEPLER);
    device->caps.max_resolution_x = 3840;
    device->caps.max_resolution_y = 2160;
    device->caps.video_memory_size = nvidia_state.vram_size;
    
    nvidia_state.initialized = true;
    debuglog(DEBUG_INFO, "NVIDIA Graphics driver initialized successfully (%s architecture)\n",
            nvidia_arch_to_string(nvidia_state.architecture));
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_shutdown(graphics_device_t* device) {
    debuglog(DEBUG_INFO, "Shutting down NVIDIA Graphics driver\n");
    
    if (nvidia_state.initialized) {
        // Disable display
        if (nvidia_state.display_enabled) {
            nvidia_disable_display();
        }
        
        nvidia_state.initialized = false;
        nvidia_state.device = NULL;
        nvidia_state.gpu_initialized = false;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_reset(graphics_device_t* device) {
    if (!device || !nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Resetting NVIDIA Graphics hardware\n");
    
    // Disable display
    nvidia_disable_display();
    
    // Reset GPU state (simplified)
    nvidia_write_reg(NVIDIA_PCRTC_CONFIG, 0);
    nvidia_write_reg(NVIDIA_PCRTC_START, 0);
    nvidia_write_reg(NVIDIA_PCRTC_SIZE, 0);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = NVIDIA_MODE_COUNT;
    *modes = kmalloc(sizeof(video_mode_t) * NVIDIA_MODE_COUNT);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // Copy supported modes
    memcpy(*modes, nvidia_modes, sizeof(video_mode_t) * NVIDIA_MODE_COUNT);
    
    debuglog(DEBUG_INFO, "NVIDIA Graphics enumerated %u display modes\n", *count);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (mode->is_text_mode) {
        debuglog(DEBUG_WARN, "NVIDIA Graphics doesn't support text modes\n");
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    debuglog(DEBUG_INFO, "NVIDIA Graphics setting mode: %ux%ux%u\n",
            mode->width, mode->height, mode->bpp);
    
    // Validate mode
    bool mode_supported = false;
    for (uint32_t i = 0; i < NVIDIA_MODE_COUNT; i++) {
        if (nvidia_modes[i].width == mode->width &&
            nvidia_modes[i].height == mode->height &&
            nvidia_modes[i].bpp == mode->bpp) {
            mode_supported = true;
            break;
        }
    }
    
    if (!mode_supported) {
        debuglog(DEBUG_ERROR, "Unsupported mode: %ux%ux%u\n", mode->width, mode->height, mode->bpp);
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Disable display before mode change
    nvidia_disable_display();
    
    // Setup display engine for new mode
    graphics_result_t result = nvidia_setup_display_engine(mode);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to setup NVIDIA display engine\n");
        return result;
    }
    
    // Update state
    nvidia_state.display_width = mode->width;
    nvidia_state.display_height = mode->height;
    nvidia_state.display_bpp = mode->bpp;
    nvidia_state.display_format = mode->format;
    nvidia_state.display_pitch = mode->pitch;
    
    // Update device current mode
    device->current_mode = *mode;
    
    // Enable display
    result = nvidia_enable_display();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to enable NVIDIA display\n");
        return result;
    }
    
    debuglog(DEBUG_INFO, "NVIDIA Graphics mode set successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = nvidia_state.display_width;
    mode->height = nvidia_state.display_height;
    mode->bpp = nvidia_state.display_bpp;
    mode->format = nvidia_state.display_format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->pitch = nvidia_state.display_pitch;
    mode->mode_number = 0;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t));
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    framebuffer->physical_addr = (uintptr_t)nvidia_state.framebuffer_base;
    framebuffer->virtual_addr = nvidia_state.framebuffer_base;
    framebuffer->size = nvidia_state.display_pitch * nvidia_state.display_height;
    framebuffer->width = nvidia_state.display_width;
    framebuffer->height = nvidia_state.display_height;
    framebuffer->pitch = nvidia_state.display_pitch;
    framebuffer->format = nvidia_state.display_format;
    framebuffer->bpp = nvidia_state.display_bpp;
    framebuffer->double_buffered = false;
    framebuffer->back_buffer = NULL;
    framebuffer->hw_cursor_available = true;
    framebuffer->cursor_data = NULL;
    
    *fb = framebuffer;
    
    debuglog(DEBUG_INFO, "NVIDIA framebuffer mapped at 0x%08X, size: %u bytes\n",
            (uint32_t)framebuffer->physical_addr, framebuffer->size);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    graphics_rect_t full_screen = {
        0, 0, nvidia_state.display_width, nvidia_state.display_height
    };
    
    return nvidia_hw_fill_rect(device, &full_screen, color);
}

static graphics_result_t nvidia_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color) {
    if (!device || !rect || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // For modern NVIDIA GPUs, we would use the 2D blitter engine
    // For this basic implementation, we'll use software rendering as fallback
    if (!nvidia_state.framebuffer_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Wait for GPU to be idle
    nvidia_wait_for_idle();
    
    uint32_t pixel_value = graphics_color_to_pixel(color, nvidia_state.display_format);
    
    // Simple software fill
    for (uint32_t y = rect->y; y < rect->y + rect->height && y < nvidia_state.display_height; y++) {
        for (uint32_t x = rect->x; x < rect->x + rect->width && x < nvidia_state.display_width; x++) {
            switch (nvidia_state.display_bpp) {
                case 16: {
                    uint16_t* fb = (uint16_t*)nvidia_state.framebuffer_base;
                    fb[y * nvidia_state.display_width + x] = (uint16_t)pixel_value;
                    break;
                }
                case 24:
                case 32: {
                    uint32_t* fb = (uint32_t*)nvidia_state.framebuffer_base;
                    fb[y * nvidia_state.display_width + x] = pixel_value;
                    break;
                }
            }
        }
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y) {
    if (!device || !src || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Wait for GPU to be idle
    nvidia_wait_for_idle();
    
    // For this basic implementation, we'll use software copy
    // In a full implementation, this would use NVIDIA's copy engine
    
    return GRAPHICS_ERROR_NOT_SUPPORTED; // Placeholder
}

static graphics_result_t nvidia_wait_for_vsync(graphics_device_t* device) {
    if (!device || !nvidia_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Simple vsync wait implementation
    // In a real implementation, you'd wait for vsync interrupt
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        // Simple delay
        for (volatile int i = 0; i < 1000; i++);
    }
    
    return GRAPHICS_SUCCESS;
}

// Helper function implementations
static graphics_result_t nvidia_detect_architecture(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint16_t device_id = device->device_id;
    
    // Detect GPU architecture based on device ID ranges
    // This is a simplified detection based on known device IDs
    if (device_id >= 0x0400 && device_id <= 0x06FF) {
        nvidia_state.architecture = NVIDIA_ARCH_FERMI;
    } else if (device_id >= 0x0FC0 && device_id <= 0x0FFF) {
        nvidia_state.architecture = NVIDIA_ARCH_KEPLER;
    } else if (device_id >= 0x1040 && device_id <= 0x17FF) {
        nvidia_state.architecture = NVIDIA_ARCH_MAXWELL;
    } else if (device_id >= 0x1B00 && device_id <= 0x1D80) {
        nvidia_state.architecture = NVIDIA_ARCH_PASCAL;
    } else if (device_id >= 0x1E00 && device_id <= 0x2080) {
        nvidia_state.architecture = NVIDIA_ARCH_TURING;
    } else if (device_id >= 0x2200 && device_id <= 0x24FF) {
        nvidia_state.architecture = NVIDIA_ARCH_AMPERE;
    } else if (device_id >= 0x2600 && device_id <= 0x28FF) {
        nvidia_state.architecture = NVIDIA_ARCH_ADA_LOVELACE;
    } else {
        nvidia_state.architecture = NVIDIA_ARCH_UNKNOWN;
    }
    
    debuglog(DEBUG_INFO, "Detected NVIDIA GPU architecture: %s (Device ID: 0x%04X)\n",
            nvidia_arch_to_string(nvidia_state.architecture), device_id);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_init_gpu(void) {
    if (!nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Read GPU identification
    uint32_t boot_0 = nvidia_read_reg(NVIDIA_PMC_BOOT_0);
    nvidia_state.gpu_id = boot_0;
    
    debuglog(DEBUG_INFO, "NVIDIA GPU ID: 0x%08X\n", nvidia_state.gpu_id);
    
    // Enable GPU master control
    nvidia_write_reg(NVIDIA_PMC_ENABLE, 0xFFFFFFFF);
    
    // Initialize memory controller
    nvidia_write_reg(NVIDIA_PFB_CONFIG_0, 0x00000001);
    
    nvidia_state.gpu_initialized = true;
    debuglog(DEBUG_INFO, "NVIDIA GPU initialized successfully\n");
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_setup_display_engine(const video_mode_t* mode) {
    if (!mode || !nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate display parameters
    uint32_t config = 0;
    uint32_t start_addr = (uint32_t)nvidia_state.framebuffer_base;
    uint32_t size_reg = ((mode->height - 1) << 16) | (mode->width - 1);
    
    // Configure pixel format
    switch (mode->bpp) {
        case 16:
            config |= 0x00000001; // 16-bit RGB565
            break;
        case 24:
            config |= 0x00000002; // 24-bit RGB888
            break;
        case 32:
            config |= 0x00000003; // 32-bit RGBA8888
            break;
        default:
            return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Program display controller
    nvidia_write_reg(NVIDIA_PCRTC_START, start_addr);
    nvidia_write_reg(NVIDIA_PCRTC_SIZE, size_reg);
    nvidia_write_reg(NVIDIA_PCRTC_CONFIG, config);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_enable_display(void) {
    if (!nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Enable display output
    uint32_t config = nvidia_read_reg(NVIDIA_PCRTC_CONFIG);
    config |= 0x80000000; // Enable bit
    nvidia_write_reg(NVIDIA_PCRTC_CONFIG, config);
    
    nvidia_state.display_enabled = true;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_disable_display(void) {
    if (!nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Disable display output
    uint32_t config = nvidia_read_reg(NVIDIA_PCRTC_CONFIG);
    config &= ~0x80000000; // Clear enable bit
    nvidia_write_reg(NVIDIA_PCRTC_CONFIG, config);
    
    nvidia_state.display_enabled = false;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t nvidia_wait_for_idle(void) {
    if (!nvidia_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Wait for graphics engine to be idle
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        // In a real implementation, you'd read a status register
        // For now, just add a delay
        for (volatile int i = 0; i < 100; i++);
    }
    
    return GRAPHICS_SUCCESS;
}

static uint32_t nvidia_read_reg(uint32_t offset) {
    if (!nvidia_state.mmio_base) {
        return 0;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)nvidia_state.mmio_base + offset);
    return *reg;
}

static void nvidia_write_reg(uint32_t offset, uint32_t value) {
    if (!nvidia_state.mmio_base) {
        return;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)nvidia_state.mmio_base + offset);
    *reg = value;
}

static const char* nvidia_arch_to_string(nvidia_gpu_arch_t arch) {
    switch (arch) {
        case NVIDIA_ARCH_FERMI: return "Fermi";
        case NVIDIA_ARCH_KEPLER: return "Kepler";
        case NVIDIA_ARCH_MAXWELL: return "Maxwell";
        case NVIDIA_ARCH_PASCAL: return "Pascal";
        case NVIDIA_ARCH_TURING: return "Turing";
        case NVIDIA_ARCH_AMPERE: return "Ampere";
        case NVIDIA_ARCH_ADA_LOVELACE: return "Ada Lovelace";
        case NVIDIA_ARCH_HOPPER: return "Hopper";
        case NVIDIA_ARCH_BLACKWELL: return "Blackwell";
        default: return "Unknown";
    }
}

// Driver initialization function
DRIVER_INIT_FUNCTION(nvidia) {
    debuglog(DEBUG_INFO, "Registering NVIDIA Graphics driver\n");
    
    // Set driver flags
    nvidia_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE | 
                         DRIVER_FLAG_SUPPORTS_3D_ACCEL |
                         DRIVER_FLAG_SUPPORTS_HW_CURSOR |
                         DRIVER_FLAG_SUPPORTS_VSYNC;
    
    return register_display_driver(&nvidia_driver);
}
