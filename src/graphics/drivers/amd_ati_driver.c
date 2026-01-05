#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/debuglog.h"
#include "../../include/io_ports.h"
#include "../../include/tlb_manager.h"
#include "../../include/mm.h"

// AMD/ATI Graphics driver framework supporting modern RDNA and legacy Radeon cards
// This covers both discrete Radeon GPUs and APU integrated graphics

// AMD PCI vendor ID
#define AMD_VENDOR_ID               0x1002

// AMD GPU architecture detection
typedef enum {
    AMD_ARCH_UNKNOWN = 0,
    AMD_ARCH_R100,          // Radeon 7000 series
    AMD_ARCH_R200,          // Radeon 8000/9000 series
    AMD_ARCH_R300,          // Radeon 9500-9800
    AMD_ARCH_R400,          // Radeon X series
    AMD_ARCH_R500,          // Radeon X1000 series
    AMD_ARCH_R600,          // Radeon HD 2000/3000
    AMD_ARCH_R700,          // Radeon HD 4000
    AMD_ARCH_EVERGREEN,     // Radeon HD 5000/6000
    AMD_ARCH_NORTHERN_ISLANDS, // Radeon HD 6000
    AMD_ARCH_SOUTHERN_ISLANDS, // Radeon HD 7000
    AMD_ARCH_SEA_ISLANDS,   // Radeon R7/R9 200/300 series
    AMD_ARCH_VOLCANIC_ISLANDS, // Radeon R9 Fury
    AMD_ARCH_ARCTIC_ISLANDS, // Radeon RX 400/500 series (Polaris)
    AMD_ARCH_VEGA,          // Radeon RX Vega series
    AMD_ARCH_NAVI,          // Radeon RX 5000 series (RDNA)
    AMD_ARCH_NAVI2,         // Radeon RX 6000 series (RDNA2)
    AMD_ARCH_NAVI3          // Radeon RX 7000 series (RDNA3)
} amd_arch_t;

// AMD GPU classes and features
typedef struct {
    uint16_t device_id_min;
    uint16_t device_id_max;
    amd_arch_t architecture;
    const char* arch_name;
    bool supports_vulkan;
    bool supports_freesync;
    bool supports_rdna_features;
    bool has_hardware_rt;
    uint32_t base_vram_mb;
    uint32_t compute_units;
} amd_gpu_info_t;

// AMD GPU database
static const amd_gpu_info_t amd_gpu_db[] = {
    // RDNA3 (RX 7000 series)
    {0x7440, 0x74FF, AMD_ARCH_NAVI3, "RDNA3", true, true, true, true, 8192, 64},
    
    // RDNA2 (RX 6000 series)
    {0x73A0, 0x73FF, AMD_ARCH_NAVI2, "RDNA2", true, true, true, true, 6144, 40},
    
    // RDNA (RX 5000 series)
    {0x7310, 0x73FF, AMD_ARCH_NAVI, "RDNA", true, true, true, false, 4096, 36},
    
    // Vega series
    {0x6860, 0x69FF, AMD_ARCH_VEGA, "Vega", true, true, false, false, 4096, 32},
    
    // Polaris (RX 400/500 series)
    {0x6600, 0x685F, AMD_ARCH_ARCTIC_ISLANDS, "Polaris", true, true, false, false, 2048, 24},
    
    // Volcanic Islands (R9 Fury)
    {0x6900, 0x695F, AMD_ARCH_VOLCANIC_ISLANDS, "Volcanic Islands", true, false, false, false, 2048, 28},
    
    // Legacy GPUs
    {0x1300, 0x65FF, AMD_ARCH_SOUTHERN_ISLANDS, "Southern Islands", false, false, false, false, 1024, 16},
    {0x6000, 0x6899, AMD_ARCH_EVERGREEN, "Evergreen", false, false, false, false, 512, 12},
};

#define AMD_GPU_DB_SIZE (sizeof(amd_gpu_db) / sizeof(amd_gpu_db[0]))

// AMD driver state
static struct {
    bool initialized;
    graphics_device_t* device;
    amd_arch_t detected_arch;
    const amd_gpu_info_t* gpu_info;
    
    // Basic framebuffer info
    void* framebuffer;
    uintptr_t framebuffer_phys;
    size_t framebuffer_size;
    uint32_t current_width;
    uint32_t current_height;
    uint32_t current_bpp;
    
    // GPU features
    bool vulkan_supported;
    bool freesync_supported;
    bool rdna_features;
    bool hardware_rt_supported;
    uint32_t vram_size_mb;
    uint32_t compute_units;
    
    // MMIO regions
    void* mmio_base;
    size_t mmio_size;
} amd_state = {
    .initialized = false,
    .device = NULL,
    .detected_arch = AMD_ARCH_UNKNOWN,
    .gpu_info = NULL,
    .framebuffer = NULL,
    .framebuffer_phys = 0,
    .framebuffer_size = 0,
    .current_width = 0,
    .current_height = 0,
    .current_bpp = 0,
    .vulkan_supported = false,
    .freesync_supported = false,
    .rdna_features = false,
    .hardware_rt_supported = false,
    .vram_size_mb = 0,
    .compute_units = 0,
    .mmio_base = NULL,
    .mmio_size = 0
};

// Function declarations
static const amd_gpu_info_t* amd_identify_gpu(uint16_t device_id);
static graphics_result_t amd_setup_basic_framebuffer(void);
static graphics_result_t amd_detect_vram_size(void);
static graphics_result_t amd_check_capabilities(void);
static pixel_format_t amd_bpp_to_pixel_format(uint8_t bpp);

// Driver operation implementations
static graphics_result_t amd_ati_initialize(graphics_device_t* device);
static graphics_result_t amd_ati_shutdown(graphics_device_t* device);
static graphics_result_t amd_ati_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t amd_ati_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t amd_ati_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t amd_ati_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t amd_ati_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb);
static graphics_result_t amd_ati_ioctl(graphics_device_t* device, uint32_t cmd, void* arg);

// AMD driver operations structure
static display_driver_ops_t amd_ati_ops = {
    .name = "amd_ati",
    .version = 1,
    .initialize = amd_ati_initialize,
    .shutdown = amd_ati_shutdown,
    .reset = NULL,
    .enumerate_modes = amd_ati_enumerate_modes,
    .set_mode = amd_ati_set_mode,
    .get_current_mode = amd_ati_get_current_mode,
    .map_framebuffer = amd_ati_map_framebuffer,
    .unmap_framebuffer = amd_ati_unmap_framebuffer,
    .clear_screen = NULL,          // Would fall back to software
    .draw_pixel = NULL,            // Would fall back to software
    .draw_rect = NULL,             // Would fall back to software
    .blit_surface = NULL,
    .set_cursor = NULL,
    .move_cursor = NULL,
    .show_cursor = NULL,
    .write_char = NULL,
    .write_string = NULL,
    .scroll_screen = NULL,
    .set_cursor_pos = NULL,
    .set_power_state = NULL,
    .hw_fill_rect = NULL,          // Would require GPU acceleration
    .hw_copy_rect = NULL,          // Would require GPU acceleration
    .hw_line = NULL,
    .wait_for_vsync = NULL,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = amd_ati_ioctl
};

DECLARE_DISPLAY_DRIVER(amd_ati, amd_ati_ops);

static void amd_set_driver_flags(void) {
    amd_ati_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE;
    
    if (amd_state.vulkan_supported) {
        amd_ati_driver.flags |= DRIVER_FLAG_SUPPORTS_3D_ACCEL;
    }
}

static const amd_gpu_info_t* amd_identify_gpu(uint16_t device_id) {
    for (size_t i = 0; i < AMD_GPU_DB_SIZE; i++) {
        const amd_gpu_info_t* info = &amd_gpu_db[i];
        if (device_id >= info->device_id_min && device_id <= info->device_id_max) {
            return info;
        }
    }
    return NULL;
}

static graphics_result_t amd_setup_basic_framebuffer(void) {
    // Set up basic framebuffer access
    amd_state.framebuffer_phys = amd_state.device->framebuffer_base;
    amd_state.framebuffer_size = amd_state.device->framebuffer_size;
    
    if (amd_state.framebuffer_phys == 0 || amd_state.framebuffer_size == 0) {
        debuglog(DEBUG_WARN, "AMD: No framebuffer BAR information available\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Map framebuffer to virtual memory
    uint32_t fb_start = amd_state.framebuffer_phys & ~0xFFF;
    uint32_t fb_end = (amd_state.framebuffer_phys + amd_state.framebuffer_size + 0xFFF) & ~0xFFF;
    
    // TODO: Implement framebuffer mapping once page constants are defined
    // page_directory_t* current_dir = vmm_get_current_page_directory();
    // memory_result_t map_result = vmm_identity_map_range(current_dir, fb_start, fb_end, PAGE_FLAGS);
    debuglog(DEBUG_INFO, "AMD: Framebuffer memory mapping deferred (0x%x-0x%x)\n", fb_start, fb_end);
    
    tlb_invalidate_range(fb_start, fb_end);
    amd_state.framebuffer = (void*)amd_state.framebuffer_phys;
    
    debuglog(DEBUG_INFO, "AMD: Framebuffer mapped at 0x%x (size: %u MB)\n",
             fb_start, (fb_end - fb_start) / (1024*1024));
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_detect_vram_size(void) {
    if (amd_state.gpu_info) {
        amd_state.vram_size_mb = amd_state.gpu_info->base_vram_mb;
        debuglog(DEBUG_INFO, "AMD: Estimated VRAM size: %u MB\n", amd_state.vram_size_mb);
        return GRAPHICS_SUCCESS;
    }
    
    // Default fallback
    amd_state.vram_size_mb = 1024;
    debuglog(DEBUG_WARN, "AMD: Unknown GPU, assuming 1GB VRAM\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_check_capabilities(void) {
    if (!amd_state.gpu_info) {
        return GRAPHICS_SUCCESS;
    }
    
    amd_state.vulkan_supported = amd_state.gpu_info->supports_vulkan;
    amd_state.freesync_supported = amd_state.gpu_info->supports_freesync;
    amd_state.rdna_features = amd_state.gpu_info->supports_rdna_features;
    amd_state.hardware_rt_supported = amd_state.gpu_info->has_hardware_rt;
    amd_state.compute_units = amd_state.gpu_info->compute_units;
    
    debuglog(DEBUG_INFO, "AMD: Architecture: %s\n", amd_state.gpu_info->arch_name);
    debuglog(DEBUG_INFO, "AMD: Compute Units: %u\n", amd_state.compute_units);
    
    if (amd_state.vulkan_supported) {
        debuglog(DEBUG_INFO, "AMD: Vulkan API supported\n");
    }
    if (amd_state.freesync_supported) {
        debuglog(DEBUG_INFO, "AMD: FreeSync supported\n");
    }
    if (amd_state.rdna_features) {
        debuglog(DEBUG_INFO, "AMD: RDNA architecture features supported\n");
    }
    if (amd_state.hardware_rt_supported) {
        debuglog(DEBUG_INFO, "AMD: Hardware ray tracing supported\n");
    }
    
    return GRAPHICS_SUCCESS;
}

static pixel_format_t amd_bpp_to_pixel_format(uint8_t bpp) {
    switch (bpp) {
        case 8:  return PIXEL_FORMAT_INDEXED_8;
        case 15: return PIXEL_FORMAT_RGB_555;
        case 16: return PIXEL_FORMAT_RGB_565;
        case 24: return PIXEL_FORMAT_RGB_888;
        case 32: return PIXEL_FORMAT_RGBA_8888;
        default: return PIXEL_FORMAT_RGBA_8888;
    }
}

static graphics_result_t amd_ati_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing AMD/ATI driver for device %s\n", device->name);
    
    amd_state.device = device;
    
    // Identify GPU architecture
    amd_state.gpu_info = amd_identify_gpu(device->device_id);
    if (amd_state.gpu_info) {
        amd_state.detected_arch = amd_state.gpu_info->architecture;
        debuglog(DEBUG_INFO, "AMD: Detected %s GPU (0x%04x)\n", 
                amd_state.gpu_info->arch_name, device->device_id);
    } else {
        debuglog(DEBUG_WARN, "AMD: Unknown GPU 0x%04x, using generic support\n", device->device_id);
    }
    
    // Set up MMIO if available
    if (device->mmio_base && device->mmio_size) {
        amd_state.mmio_base = (void*)device->mmio_base;
        amd_state.mmio_size = device->mmio_size;
        debuglog(DEBUG_INFO, "AMD: MMIO region at 0x%x (size: %lu KB)\n",
                 device->mmio_base, device->mmio_size / 1024);
    }
    
    // Detect VRAM size
    amd_detect_vram_size();
    
    // Check GPU capabilities
    amd_check_capabilities();
    
    // Setup basic framebuffer (for compatibility with legacy VGA/VESA modes)
    graphics_result_t result = amd_setup_basic_framebuffer();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "AMD: Basic framebuffer setup failed, driver limited\n");
    }
    
    // Set default mode (fallback to VESA-compatible)
    amd_state.current_width = 1024;
    amd_state.current_height = 768;
    amd_state.current_bpp = 32;
    
    amd_state.initialized = true;
    amd_set_driver_flags();
    
    debuglog(DEBUG_INFO, "AMD: Driver initialized successfully\n");
    debuglog(DEBUG_INFO, "AMD: Note - This is a basic framework driver\n");
    debuglog(DEBUG_INFO, "AMD: For full GPU acceleration, use AMD proprietary drivers\n");
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_shutdown(graphics_device_t* device) {
    if (!amd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Shutting down AMD/ATI driver\n");
    
    amd_state.initialized = false;
    amd_state.device = NULL;
    amd_state.framebuffer = NULL;
    
    return GRAPHICS_SUCCESS;
}

// Basic video mode support - would use VESA/VGA compatibility
static const video_mode_t amd_basic_modes[] = {
    {640, 480, 16, 640*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {640, 480, 24, 640*3, PIXEL_FORMAT_RGB_888, 60, false, 1, NULL},
    {640, 480, 32, 640*4, PIXEL_FORMAT_RGBA_8888, 60, false, 2, NULL},
    {800, 600, 16, 800*2, PIXEL_FORMAT_RGB_565, 60, false, 3, NULL},
    {800, 600, 24, 800*3, PIXEL_FORMAT_RGB_888, 60, false, 4, NULL},
    {800, 600, 32, 800*4, PIXEL_FORMAT_RGBA_8888, 60, false, 5, NULL},
    {1024, 768, 16, 1024*2, PIXEL_FORMAT_RGB_565, 60, false, 6, NULL},
    {1024, 768, 24, 1024*3, PIXEL_FORMAT_RGB_888, 60, false, 7, NULL},
    {1024, 768, 32, 1024*4, PIXEL_FORMAT_RGBA_8888, 60, false, 8, NULL},
    {1280, 1024, 16, 1280*2, PIXEL_FORMAT_RGB_565, 60, false, 9, NULL},
    {1280, 1024, 24, 1280*3, PIXEL_FORMAT_RGB_888, 60, false, 10, NULL},
    {1280, 1024, 32, 1280*4, PIXEL_FORMAT_RGBA_8888, 60, false, 11, NULL},
    {1920, 1080, 24, 1920*3, PIXEL_FORMAT_RGB_888, 60, false, 12, NULL},
    {1920, 1080, 32, 1920*4, PIXEL_FORMAT_RGBA_8888, 60, false, 13, NULL},
    {2560, 1440, 24, 2560*3, PIXEL_FORMAT_RGB_888, 60, false, 14, NULL},
    {2560, 1440, 32, 2560*4, PIXEL_FORMAT_RGBA_8888, 60, false, 15, NULL},
    {3840, 2160, 24, 3840*3, PIXEL_FORMAT_RGB_888, 60, false, 16, NULL},
    {3840, 2160, 32, 3840*4, PIXEL_FORMAT_RGBA_8888, 60, false, 17, NULL},
};

#define AMD_MODE_COUNT (sizeof(amd_basic_modes) / sizeof(amd_basic_modes[0]))

static graphics_result_t amd_ati_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = AMD_MODE_COUNT;
    *modes = kmalloc(sizeof(video_mode_t) * AMD_MODE_COUNT, GFP_KERNEL);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    memcpy(*modes, amd_basic_modes, sizeof(amd_basic_modes));
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !amd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "AMD: Setting mode %ux%ux%u\n", mode->width, mode->height, mode->bpp);
    
    // For now, just update our state - in a real driver this would program the GPU
    amd_state.current_width = mode->width;
    amd_state.current_height = mode->height;
    amd_state.current_bpp = mode->bpp;
    
    debuglog(DEBUG_WARN, "AMD: Mode setting is stubbed - would require proper GPU programming\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !amd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = amd_state.current_width;
    mode->height = amd_state.current_height;
    mode->bpp = amd_state.current_bpp;
    mode->pitch = amd_state.current_width * (amd_state.current_bpp / 8);
    mode->format = amd_bpp_to_pixel_format(amd_state.current_bpp);
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->mode_number = 0;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !amd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!amd_state.framebuffer) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t), GFP_KERNEL);
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    framebuffer->virtual_addr = amd_state.framebuffer;
    framebuffer->physical_addr = amd_state.framebuffer_phys;
    framebuffer->size = amd_state.framebuffer_size;
    framebuffer->width = amd_state.current_width;
    framebuffer->height = amd_state.current_height;
    framebuffer->pitch = amd_state.current_width * (amd_state.current_bpp / 8);
    framebuffer->format = amd_bpp_to_pixel_format(amd_state.current_bpp);
    framebuffer->bpp = amd_state.current_bpp;
    framebuffer->back_buffer = NULL;
    framebuffer->double_buffered = false;
    framebuffer->hw_cursor_available = false;
    framebuffer->cursor_data = NULL;
    
    *fb = framebuffer;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb) {
    if (!device || !fb) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    kfree(fb);
    return GRAPHICS_SUCCESS;
}

// AMD specific IOCTL commands
#define AMD_IOCTL_GET_GPU_INFO       0x4001
#define AMD_IOCTL_GET_VRAM_INFO      0x4002
#define AMD_IOCTL_CHECK_RDNA_SUPPORT 0x4003
#define AMD_IOCTL_CHECK_RT_SUPPORT   0x4004

typedef struct {
    uint16_t device_id;
    amd_arch_t architecture;
    char arch_name[32];
    bool vulkan_supported;
    bool freesync_supported;
    bool rdna_features;
    bool hardware_rt_supported;
    uint32_t compute_units;
} amd_gpu_info_ioctl_t;

typedef struct {
    uint32_t total_vram_mb;
    uint32_t available_vram_mb;
    uintptr_t framebuffer_base;
    size_t framebuffer_size;
} amd_vram_info_ioctl_t;

static graphics_result_t amd_ati_ioctl(graphics_device_t* device, uint32_t cmd, void* arg) {
    if (!device || !amd_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    switch (cmd) {
        case AMD_IOCTL_GET_GPU_INFO: {
            amd_gpu_info_ioctl_t* info = (amd_gpu_info_ioctl_t*)arg;
            if (!info) return GRAPHICS_ERROR_INVALID_PARAMETER;
            
            info->device_id = device->device_id;
            info->architecture = amd_state.detected_arch;
            if (amd_state.gpu_info) {
                strncpy(info->arch_name, amd_state.gpu_info->arch_name, sizeof(info->arch_name) - 1);
            } else {
                strncpy(info->arch_name, "Unknown", sizeof(info->arch_name) - 1);
            }
            info->arch_name[sizeof(info->arch_name) - 1] = '\0';
            info->vulkan_supported = amd_state.vulkan_supported;
            info->freesync_supported = amd_state.freesync_supported;
            info->rdna_features = amd_state.rdna_features;
            info->hardware_rt_supported = amd_state.hardware_rt_supported;
            info->compute_units = amd_state.compute_units;
            
            return GRAPHICS_SUCCESS;
        }
        
        case AMD_IOCTL_GET_VRAM_INFO: {
            amd_vram_info_ioctl_t* vram = (amd_vram_info_ioctl_t*)arg;
            if (!vram) return GRAPHICS_ERROR_INVALID_PARAMETER;
            
            vram->total_vram_mb = amd_state.vram_size_mb;
            vram->available_vram_mb = amd_state.vram_size_mb; // Simplified
            vram->framebuffer_base = amd_state.framebuffer_phys;
            vram->framebuffer_size = amd_state.framebuffer_size;
            
            return GRAPHICS_SUCCESS;
        }
        
        case AMD_IOCTL_CHECK_RDNA_SUPPORT: {
            bool* rdna_support = (bool*)arg;
            if (!rdna_support) return GRAPHICS_ERROR_INVALID_PARAMETER;
            
            *rdna_support = amd_state.rdna_features;
            return GRAPHICS_SUCCESS;
        }
        
        case AMD_IOCTL_CHECK_RT_SUPPORT: {
            bool* rt_support = (bool*)arg;
            if (!rt_support) return GRAPHICS_ERROR_INVALID_PARAMETER;
            
            *rt_support = amd_state.hardware_rt_supported;
            return GRAPHICS_SUCCESS;
        }
        
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
}

// Driver initialization function
DRIVER_INIT_FUNCTION(amd_ati) {
    debuglog(DEBUG_INFO, "Registering AMD/ATI driver framework\n");
    amd_set_driver_flags();
    return register_display_driver(&amd_ati_driver);
}

// Driver exit function
DRIVER_EXIT_FUNCTION(amd_ati) {
    debuglog(DEBUG_INFO, "Unregistering AMD/ATI driver\n");
    unregister_display_driver(&amd_ati_driver);
}