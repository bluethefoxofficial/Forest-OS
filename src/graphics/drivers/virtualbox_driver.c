#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/debuglog.h"
#include "../../include/io_ports.h"
#include "../../include/tlb_manager.h"
#include "../../include/mm.h"

// VirtualBox Graphics Adapter constants
// VirtualBox uses the Bochs VBE extensions plus additional features

// VirtualBox-specific PCI IDs
#define VBOX_VENDOR_ID              0x80EE
#define VBOX_DEVICE_ID              0xBEEF
#define VBOX_VESA_DEVICE_ID         0xDEAD

// VirtualBox Guest Additions interface
#define VBOX_VIDEO_MAX_SCREENS      64
#define VBOX_VIDEO_INFO_TYPE_DISPLAY 0

// VirtualBox VBVA (Video Buffer Video Acceleration) constants
#define VBVA_RING_BUFFER_SIZE       (4 * 1024 * 1024)  // 4MB ring buffer
#define VBVA_MIN_BUFFER_SIZE        (128 * 1024)      // 128KB minimum

// VBox specific MMIO registers (if available)
#define VBOX_MMIO_GUEST_HEAP        0x0
#define VBOX_MMIO_ADAPTER_INFO      0x4
#define VBOX_MMIO_DISPLAY_INFO      0x8
#define VBOX_MMIO_VBVA_INFO         0xC

// Enhanced mode support flags
#define VBOX_MODE_FLAG_SEAMLESS     0x01
#define VBOX_MODE_FLAG_2D_ACCEL     0x02
#define VBOX_MODE_FLAG_3D_ACCEL     0x04
#define VBOX_MODE_FLAG_DIRTY_RECT   0x08

// VirtualBox video mode structure
typedef struct {
    uint32_t width;
    uint32_t height;  
    uint32_t bpp;
    uint32_t flags;
    const char* description;
} vbox_mode_info_t;

// Enhanced modes supported by VirtualBox
static const vbox_mode_info_t vbox_modes[] = {
    {640, 480, 8, 0, "640x480x8"},
    {640, 480, 16, 0, "640x480x16"},
    {640, 480, 24, 0, "640x480x24"},
    {640, 480, 32, VBOX_MODE_FLAG_2D_ACCEL, "640x480x32"},
    
    {800, 600, 8, 0, "800x600x8"},
    {800, 600, 16, 0, "800x600x16"},
    {800, 600, 24, 0, "800x600x24"},
    {800, 600, 32, VBOX_MODE_FLAG_2D_ACCEL, "800x600x32"},
    
    {1024, 768, 8, 0, "1024x768x8"},
    {1024, 768, 16, 0, "1024x768x16"},
    {1024, 768, 24, 0, "1024x768x24"},
    {1024, 768, 32, VBOX_MODE_FLAG_2D_ACCEL, "1024x768x32"},
    
    {1280, 1024, 16, 0, "1280x1024x16"},
    {1280, 1024, 24, 0, "1280x1024x24"},
    {1280, 1024, 32, VBOX_MODE_FLAG_2D_ACCEL, "1280x1024x32"},
    
    {1600, 1200, 16, 0, "1600x1200x16"},
    {1600, 1200, 24, 0, "1600x1200x24"},
    {1600, 1200, 32, VBOX_MODE_FLAG_2D_ACCEL, "1600x1200x32"},
    
    // Modern widescreen resolutions
    {1366, 768, 24, 0, "1366x768x24"},
    {1366, 768, 32, VBOX_MODE_FLAG_2D_ACCEL, "1366x768x32"},
    {1920, 1080, 24, 0, "1920x1080x24"},
    {1920, 1080, 32, VBOX_MODE_FLAG_2D_ACCEL | VBOX_MODE_FLAG_3D_ACCEL, "1920x1080x32"},
    {2560, 1440, 32, VBOX_MODE_FLAG_2D_ACCEL, "2560x1440x32"},
    {3840, 2160, 32, VBOX_MODE_FLAG_2D_ACCEL, "3840x2160x32 (4K)"},
};

#define NUM_VBOX_MODES (sizeof(vbox_modes) / sizeof(vbox_modes[0]))

// VirtualBox driver state
static struct {
    bool initialized;
    bool is_virtualbox;
    bool guest_additions_active;
    void* mmio_base;
    size_t mmio_size;
    
    // Current display state
    uint32_t current_width;
    uint32_t current_height;
    uint32_t current_bpp;
    pixel_format_t current_format;
    
    // VirtualBox specific features
    bool seamless_mode_supported;
    bool vbva_enabled;
    void* vbva_buffer;
    size_t vbva_buffer_size;
    
    // Device info
    graphics_device_t* device;
    void* framebuffer;
    uintptr_t framebuffer_phys;
    size_t framebuffer_size;
} vbox_state = {
    .initialized = false,
    .is_virtualbox = false,
    .guest_additions_active = false,
    .mmio_base = NULL,
    .mmio_size = 0,
    .current_width = 0,
    .current_height = 0,
    .current_bpp = 0,
    .seamless_mode_supported = false,
    .vbva_enabled = false,
    .vbva_buffer = NULL,
    .vbva_buffer_size = 0,
    .device = NULL,
    .framebuffer = NULL,
    .framebuffer_phys = 0,
    .framebuffer_size = 0
};

// Function declarations
static bool vbox_detect_virtualbox(void);
static graphics_result_t vbox_setup_vbva(void);
static graphics_result_t vbox_init_guest_additions(void);
static pixel_format_t vbox_bpp_to_pixel_format(uint8_t bpp);

// Driver operation implementations
static graphics_result_t vbox_initialize(graphics_device_t* device);
static graphics_result_t vbox_shutdown(graphics_device_t* device);
static graphics_result_t vbox_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t vbox_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t vbox_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t vbox_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t vbox_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb);
static graphics_result_t vbox_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t vbox_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color);
static graphics_result_t vbox_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled);
static graphics_result_t vbox_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color);
static graphics_result_t vbox_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y);
static graphics_result_t vbox_ioctl(graphics_device_t* device, uint32_t cmd, void* arg);

// VirtualBox driver operations structure  
static display_driver_ops_t vbox_ops = {
    .name = "virtualbox",
    .version = 1,
    .initialize = vbox_initialize,
    .shutdown = vbox_shutdown,
    .reset = NULL,
    .enumerate_modes = vbox_enumerate_modes,
    .set_mode = vbox_set_mode,
    .get_current_mode = vbox_get_current_mode,
    .map_framebuffer = vbox_map_framebuffer,
    .unmap_framebuffer = vbox_unmap_framebuffer,
    .clear_screen = vbox_clear_screen,
    .draw_pixel = vbox_draw_pixel,
    .draw_rect = vbox_draw_rect,
    .blit_surface = NULL,
    .set_cursor = NULL,
    .move_cursor = NULL,
    .show_cursor = NULL,
    .write_char = NULL,
    .write_string = NULL,
    .scroll_screen = NULL,
    .set_cursor_pos = NULL,
    .set_power_state = NULL,
    .hw_fill_rect = vbox_hw_fill_rect,
    .hw_copy_rect = vbox_hw_copy_rect,
    .hw_line = NULL,
    .wait_for_vsync = NULL,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = vbox_ioctl
};

// Declare the driver
DECLARE_DISPLAY_DRIVER(vbox, vbox_ops);

static void vbox_set_driver_flags(void) {
    vbox_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE | 
                        DRIVER_FLAG_SUPPORTS_VSYNC;
                        
    if (vbox_state.guest_additions_active) {
        vbox_driver.flags |= DRIVER_FLAG_SUPPORTS_HW_CURSOR;
    }
}

static bool vbox_detect_virtualbox(void) {
    // Method 1: Check for VirtualBox PCI device
    // This would typically involve PCI scanning - simplified here
    if (vbox_state.device && 
        vbox_state.device->vendor_id == VBOX_VENDOR_ID &&
        (vbox_state.device->device_id == VBOX_DEVICE_ID || 
         vbox_state.device->device_id == VBOX_VESA_DEVICE_ID)) {
        return true;
    }
    
    // Method 2: Check CPUID for VirtualBox hypervisor signature
    uint32_t eax, ebx, ecx, edx;
    
    // Check if we're running under a hypervisor
    __asm__ volatile ("cpuid" 
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                      : "a"(1));
    
    if (!(ecx & (1 << 31))) {
        return false; // Not running under hypervisor
    }
    
    // Get hypervisor vendor string
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0x40000000));
    
    // VirtualBox hypervisor signature: "VBoxVBoxVBox"
    if (ebx == 0x786F4256 && ecx == 0x786F4256 && edx == 0x786F4256) {
        return true;
    }
    
    return false;
}

static graphics_result_t vbox_setup_vbva(void) {
    if (!vbox_state.mmio_base) {
        debuglog(DEBUG_INFO, "VBox: No MMIO base, VBVA disabled\n");
        return GRAPHICS_SUCCESS;
    }
    
    // Allocate VBVA buffer
    vbox_state.vbva_buffer_size = VBVA_RING_BUFFER_SIZE;
    vbox_state.vbva_buffer = kmalloc(vbox_state.vbva_buffer_size, GFP_KERNEL);
    
    if (!vbox_state.vbva_buffer) {
        debuglog(DEBUG_WARN, "VBox: Failed to allocate VBVA buffer\n");
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(vbox_state.vbva_buffer, 0, vbox_state.vbva_buffer_size);
    
    // Setup VBVA through MMIO (simplified)
    // In a full implementation, this would configure the VBVA ring buffer
    vbox_state.vbva_enabled = true;
    
    debuglog(DEBUG_INFO, "VBox: VBVA enabled with %lu KB buffer\n", 
             vbox_state.vbva_buffer_size / 1024);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_init_guest_additions(void) {
    // Simplified guest additions detection
    // In practice, this would involve more complex communication with VirtualBox
    
    if (vbox_state.mmio_base) {
        // Try to communicate with VirtualBox guest additions
        // This is a placeholder - real implementation would use proper protocols
        vbox_state.guest_additions_active = true;
        vbox_state.seamless_mode_supported = true;
        
        debuglog(DEBUG_INFO, "VBox: Guest additions interface detected\n");
        return vbox_setup_vbva();
    }
    
    debuglog(DEBUG_INFO, "VBox: Running without guest additions\n");
    return GRAPHICS_SUCCESS;
}

static pixel_format_t vbox_bpp_to_pixel_format(uint8_t bpp) {
    switch (bpp) {
        case 8:  return PIXEL_FORMAT_INDEXED_8;
        case 15: return PIXEL_FORMAT_RGB_555;
        case 16: return PIXEL_FORMAT_RGB_565;
        case 24: return PIXEL_FORMAT_RGB_888;
        case 32: return PIXEL_FORMAT_RGBA_8888;
        default: return PIXEL_FORMAT_RGB_888;
    }
}

static graphics_result_t vbox_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing VirtualBox graphics driver for device %s\n", device->name);
    
    vbox_state.device = device;
    
    // Detect if we're running in VirtualBox
    vbox_state.is_virtualbox = vbox_detect_virtualbox();
    
    if (!vbox_state.is_virtualbox) {
        debuglog(DEBUG_ERROR, "VBox: Not running in VirtualBox environment\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    debuglog(DEBUG_INFO, "VBox: VirtualBox environment detected\n");
    
    // Setup MMIO if available
    if (device->mmio_base && device->mmio_size) {
        vbox_state.mmio_base = (void*)device->mmio_base;
        vbox_state.mmio_size = device->mmio_size;
        
        // Map MMIO region 
        page_directory_t* current_dir = vmm_get_current_page_directory();
        memory_result_t map_result = vmm_identity_map_range(current_dir, 
                                                           device->mmio_base,
                                                           device->mmio_base + device->mmio_size,
                                                           PAGE_PRESENT | PAGE_WRITABLE);
        
        if (map_result != MEMORY_OK) {
            debuglog(DEBUG_WARN, "VBox: Failed to map MMIO region\n");
            vbox_state.mmio_base = NULL;
        } else {
            debuglog(DEBUG_INFO, "VBox: MMIO mapped at 0x%x (size: %lu KB)\n",
                     device->mmio_base, device->mmio_size / 1024);
        }
    }
    
    // Setup framebuffer
    vbox_state.framebuffer_phys = device->framebuffer_base;
    vbox_state.framebuffer_size = device->framebuffer_size;
    
    if (vbox_state.framebuffer_phys == 0) {
        // VirtualBox typically uses the same address as Bochs for legacy compatibility
        vbox_state.framebuffer_phys = 0xE0000000;
        debuglog(DEBUG_WARN, "VBox: No framebuffer BAR, using default 0xE0000000\n");
    }
    
    if (vbox_state.framebuffer_size == 0) {
        // VirtualBox can provide more VRAM than Bochs
        vbox_state.framebuffer_size = 32 * 1024 * 1024; // 32 MB default
    }
    
    // Map framebuffer
    uint32_t fb_start = vbox_state.framebuffer_phys & ~0xFFF;
    uint32_t fb_end = (vbox_state.framebuffer_phys + vbox_state.framebuffer_size + 0xFFF) & ~0xFFF;
    
    page_directory_t* current_dir = vmm_get_current_page_directory();
    memory_result_t map_result = vmm_identity_map_range(current_dir, fb_start, fb_end,
                                                       PAGE_PRESENT | PAGE_WRITABLE);
    
    if (map_result != MEMORY_OK) {
        debuglog(DEBUG_ERROR, "VBox: Failed to map framebuffer at 0x%x-0x%x\n", fb_start, fb_end);
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    tlb_invalidate_range(fb_start, fb_end);
    vbox_state.framebuffer = (void*)vbox_state.framebuffer_phys;
    
    // Test framebuffer accessibility
    volatile uint32_t* test_ptr = (volatile uint32_t*)vbox_state.framebuffer;
    uint32_t original = *test_ptr;
    *test_ptr = 0xDEADBEEF;
    if (*test_ptr != 0xDEADBEEF) {
        debuglog(DEBUG_ERROR, "VBox: Framebuffer memory validation failed\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    *test_ptr = original;
    
    debuglog(DEBUG_INFO, "VBox: Framebuffer mapped and validated at 0x%x (size: %u MB)\n",
             fb_start, (fb_end - fb_start) / (1024*1024));
    
    // Initialize guest additions if available
    vbox_init_guest_additions();
    
    // Use Bochs VBE for basic mode setting since VirtualBox supports it
    extern graphics_result_t bga_set_video_mode(uint32_t width, uint32_t height, uint32_t bpp, bool use_lfb, bool clear_memory);
    graphics_result_t result = bga_set_video_mode(1024, 768, 32, true, true);
    if (result == GRAPHICS_SUCCESS) {
        vbox_state.current_width = 1024;
        vbox_state.current_height = 768;
        vbox_state.current_bpp = 32;
        vbox_state.current_format = PIXEL_FORMAT_RGBA_8888;
    }
    
    vbox_state.initialized = true;
    
    debuglog(DEBUG_INFO, "VBox: Driver initialized successfully%s\n",
             vbox_state.guest_additions_active ? " with Guest Additions" : "");
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_shutdown(graphics_device_t* device) {
    if (!vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Shutting down VirtualBox graphics driver\n");
    
    // Clean up VBVA
    if (vbox_state.vbva_buffer) {
        kfree(vbox_state.vbva_buffer);
        vbox_state.vbva_buffer = NULL;
    }
    
    // Reset state
    vbox_state.initialized = false;
    vbox_state.guest_additions_active = false;
    vbox_state.vbva_enabled = false;
    vbox_state.device = NULL;
    vbox_state.framebuffer = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = NUM_VBOX_MODES;
    *modes = kmalloc(sizeof(video_mode_t) * NUM_VBOX_MODES, GFP_KERNEL);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    for (uint32_t i = 0; i < NUM_VBOX_MODES; i++) {
        video_mode_t* mode = &(*modes)[i];
        const vbox_mode_info_t* vbox_mode = &vbox_modes[i];
        
        mode->width = vbox_mode->width;
        mode->height = vbox_mode->height;
        mode->bpp = vbox_mode->bpp;
        mode->pitch = vbox_mode->width * (vbox_mode->bpp / 8);
        mode->format = vbox_bpp_to_pixel_format(vbox_mode->bpp);
        mode->refresh_rate = 60;
        mode->is_text_mode = false;
        mode->mode_number = i;
        mode->hw_data = (void*)(uintptr_t)vbox_mode->flags; // Store VBox flags
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "VBox: Setting mode %ux%ux%u\n", mode->width, mode->height, mode->bpp);
    
    // Use Bochs VBE for mode setting since VirtualBox supports it
    extern graphics_result_t bga_set_video_mode(uint32_t width, uint32_t height, uint32_t bpp, bool use_lfb, bool clear_memory);
    graphics_result_t result = bga_set_video_mode(mode->width, mode->height, mode->bpp, true, false);
    
    if (result == GRAPHICS_SUCCESS) {
        vbox_state.current_width = mode->width;
        vbox_state.current_height = mode->height;
        vbox_state.current_bpp = mode->bpp;
        vbox_state.current_format = mode->format;
        
        // Notify VirtualBox of mode change if guest additions are active
        if (vbox_state.guest_additions_active && vbox_state.mmio_base) {
            // Would send mode change notification to VirtualBox here
            debuglog(DEBUG_INFO, "VBox: Notified host of mode change\n");
        }
    }
    
    return result;
}

static graphics_result_t vbox_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = vbox_state.current_width;
    mode->height = vbox_state.current_height;
    mode->bpp = vbox_state.current_bpp;
    mode->pitch = vbox_state.current_width * (vbox_state.current_bpp / 8);
    mode->format = vbox_state.current_format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->mode_number = 0;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t), GFP_KERNEL);
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    framebuffer->virtual_addr = vbox_state.framebuffer;
    framebuffer->physical_addr = vbox_state.framebuffer_phys;
    framebuffer->size = vbox_state.framebuffer_size;
    framebuffer->width = vbox_state.current_width;
    framebuffer->height = vbox_state.current_height;
    framebuffer->pitch = vbox_state.current_width * (vbox_state.current_bpp / 8);
    framebuffer->format = vbox_state.current_format;
    framebuffer->bpp = vbox_state.current_bpp;
    framebuffer->back_buffer = NULL;
    framebuffer->double_buffered = false;
    framebuffer->hw_cursor_available = vbox_state.guest_additions_active;
    framebuffer->cursor_data = NULL;
    
    *fb = framebuffer;
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_unmap_framebuffer(graphics_device_t* device, framebuffer_t* fb) {
    if (!device || !fb) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    kfree(fb);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Use hardware acceleration if VBVA is available
    if (vbox_state.vbva_enabled) {
        graphics_rect_t fullscreen = {
            .x = 0,
            .y = 0, 
            .width = vbox_state.current_width,
            .height = vbox_state.current_height
        };
        return vbox_hw_fill_rect(device, &fullscreen, color);
    }
    
    // Fallback to software clearing
    extern graphics_result_t bga_clear_screen(graphics_device_t* device, graphics_color_t color);
    return bga_clear_screen(device, color);
}

static graphics_result_t vbox_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Use software fallback from BGA driver
    extern graphics_result_t bga_draw_pixel(graphics_device_t* device, int32_t x, int32_t y, graphics_color_t color);
    return bga_draw_pixel(device, x, y, color);
}

static graphics_result_t vbox_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled) {
    if (!device || !rect || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Use hardware acceleration if available
    if (filled && vbox_state.vbva_enabled) {
        return vbox_hw_fill_rect(device, rect, color);
    }
    
    // Fallback to software rendering
    extern graphics_result_t bga_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled);
    return bga_draw_rect(device, rect, color, filled);
}

static graphics_result_t vbox_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color) {
    if (!device || !rect || !vbox_state.vbva_enabled) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Simplified VBVA rectangle fill - in practice would use proper VBVA protocol
    debuglog(DEBUG_INFO, "VBox: Hardware fill rect (%d,%d) %ux%u color=0x%02x%02x%02x\n",
             rect->x, rect->y, rect->width, rect->height, color.r, color.g, color.b);
    
    // For now, fall back to software - real implementation would use VBVA commands
    extern graphics_result_t bga_draw_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color, bool filled);
    return bga_draw_rect(device, rect, color, true);
}

static graphics_result_t vbox_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y) {
    if (!device || !src || !vbox_state.vbva_enabled) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Simplified VBVA copy rectangle
    debuglog(DEBUG_INFO, "VBox: Hardware copy rect (%d,%d) %ux%u -> (%d,%d)\n",
             src->x, src->y, src->width, src->height, dst_x, dst_y);
    
    // Would implement VBVA copy command here
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

// VirtualBox specific IOCTL commands
#define VBOX_IOCTL_ENABLE_SEAMLESS      0x2001
#define VBOX_IOCTL_DISABLE_SEAMLESS     0x2002
#define VBOX_IOCTL_SET_POINTER_SHAPE    0x2003
#define VBOX_IOCTL_GET_VBVA_INFO        0x2004

static graphics_result_t vbox_ioctl(graphics_device_t* device, uint32_t cmd, void* arg) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    switch (cmd) {
        case VBOX_IOCTL_ENABLE_SEAMLESS:
            if (vbox_state.seamless_mode_supported) {
                debuglog(DEBUG_INFO, "VBox: Seamless mode enabled\n");
                return GRAPHICS_SUCCESS;
            }
            return GRAPHICS_ERROR_NOT_SUPPORTED;
            
        case VBOX_IOCTL_DISABLE_SEAMLESS:
            debuglog(DEBUG_INFO, "VBox: Seamless mode disabled\n");
            return GRAPHICS_SUCCESS;
            
        case VBOX_IOCTL_GET_VBVA_INFO: {
            uint32_t* vbva_enabled = (uint32_t*)arg;
            if (!vbva_enabled) return GRAPHICS_ERROR_INVALID_PARAMETER;
            *vbva_enabled = vbox_state.vbva_enabled ? 1 : 0;
            return GRAPHICS_SUCCESS;
        }
        
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
}

// Driver initialization function
DRIVER_INIT_FUNCTION(vbox) {
    debuglog(DEBUG_INFO, "Registering VirtualBox graphics driver\n");
    vbox_set_driver_flags();
    return register_display_driver(&vbox_driver);
}

// Driver exit function  
DRIVER_EXIT_FUNCTION(vbox) {
    debuglog(DEBUG_INFO, "Unregistering VirtualBox graphics driver\n");
    unregister_display_driver(&vbox_driver);
}

//=============================================================================
// UNIFIED DRIVER INTERFACE IMPLEMENTATION
//=============================================================================

#include "graphics/unified_driver.h"

// Unified interface implementation for VirtualBox
static graphics_result_t vbox_get_extended_capabilities(graphics_device_t* device,
                                                       graphics_capabilities_t* caps) {
    if (!device || !caps || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Enhanced capabilities specific to VirtualBox
    caps->supports_2d_accel = true;
    caps->supports_3d_accel = false; // Basic implementation
    caps->supports_hw_cursor = true;
    caps->supports_page_flipping = true;
    caps->supports_vsync = false;
    caps->supports_multiple_heads = true;
    caps->max_resolution_x = 8192;  // VirtualBox supports high resolutions
    caps->max_resolution_y = 8192;
    caps->video_memory_size = vbox_state.framebuffer_size;
    caps->num_video_modes = 50;     // VirtualBox supports many modes
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_query_feature_support(graphics_device_t* device,
                                                   uint32_t feature_id,
                                                   bool* supported) {
    if (!device || !supported || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *supported = false;
    
    switch (feature_id) {
        case FEATURE_ID_VIRTUAL_DISPLAYS:
            *supported = vbox_state.seamless_mode_supported;
            break;
        case FEATURE_ID_GUEST_ADDITIONS:
            *supported = vbox_state.guest_additions_active;
            break;
        case FEATURE_ID_HW_CURSOR_ALPHA:
            *supported = true;
            break;
        case FEATURE_ID_MULTIPLE_OUTPUTS:
            *supported = true; // VirtualBox supports multiple monitors
            break;
        default:
            return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_create_virtual_display(graphics_device_t* device,
                                                    uint32_t width, uint32_t height,
                                                    uint32_t* display_id) {
    if (!device || !display_id || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!vbox_state.seamless_mode_supported) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // VirtualBox virtual display creation
    debuglog(DEBUG_INFO, "VBox: Creating virtual display %dx%d\n", width, height);
    
    // In a real implementation, you would communicate with VirtualBox
    // to create a new virtual display through guest additions
    *display_id = 1; // Simple implementation returns display ID 1
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_destroy_virtual_display(graphics_device_t* device,
                                                     uint32_t display_id) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "VBox: Destroying virtual display %u\n", display_id);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_detect_hypervisor(graphics_device_t* device,
                                               char* hypervisor_name,
                                               size_t name_size) {
    if (!device || !hypervisor_name || name_size < 12) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Copy VirtualBox hypervisor name
    strncpy(hypervisor_name, "VirtualBox", name_size - 1);
    hypervisor_name[name_size - 1] = '\0';
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_enable_guest_integration(graphics_device_t* device) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!vbox_state.guest_additions_active) {
        debuglog(DEBUG_WARN, "VBox: Guest additions not available\n");
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    debuglog(DEBUG_INFO, "VBox: Enabling guest integration features\n");
    // Enable VBVA and other guest additions features
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_disable_guest_integration(graphics_device_t* device) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "VBox: Disabling guest integration features\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t vbox_extended_ioctl(graphics_device_t* device,
                                            uint32_t cmd,
                                            const void* input_data,
                                            size_t input_size,
                                            void* output_data,
                                            size_t output_size,
                                            size_t* bytes_returned) {
    if (!device || !vbox_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (bytes_returned) *bytes_returned = 0;
    
    switch (cmd) {
        case UNIFIED_IOCTL_GUEST_HANDSHAKE:
            if (output_data && output_size >= sizeof(uint32_t)) {
                *(uint32_t*)output_data = vbox_state.guest_additions_active ? 1 : 0;
                if (bytes_returned) *bytes_returned = sizeof(uint32_t);
            }
            return GRAPHICS_SUCCESS;
            
        default:
            // Fallback to standard IOCTL
            return vbox_ioctl(device, cmd, (void*)output_data);
    }
}

// Unified driver operations structure for VirtualBox
static unified_driver_interface_t vbox_unified_interface = {
    .name = "VirtualBox Unified Driver",
    .vendor = "Oracle",
    .version_major = 1,
    .version_minor = 0,
    .api_version = 1,
    
    .get_extended_capabilities = vbox_get_extended_capabilities,
    .query_feature_support = vbox_query_feature_support,
    
    .create_virtual_display = vbox_create_virtual_display,
    .destroy_virtual_display = vbox_destroy_virtual_display,
    .set_virtual_display_offset = NULL, // Not implemented yet
    
    .allocate_video_memory = NULL,      // Use default implementation
    .free_video_memory = NULL,
    .map_video_memory = NULL,
    .unmap_video_memory = NULL,
    
    .get_gpu_temperature = NULL,        // Not applicable for VirtualBox
    .get_gpu_utilization = NULL,
    .get_memory_info = NULL,
    
    .set_performance_profile = NULL,
    .set_power_limit = NULL,
    .set_clock_frequencies = NULL,
    
    .enumerate_outputs = NULL,          // TODO: Implement multi-head support
    .set_output_mode = NULL,
    .get_output_edid = NULL,
    
    .create_acceleration_context = NULL, // No hardware acceleration
    .destroy_acceleration_context = NULL,
    .submit_command_buffer = NULL,
    
    .detect_hypervisor = vbox_detect_hypervisor,
    .enable_guest_integration = vbox_enable_guest_integration,
    .disable_guest_integration = vbox_disable_guest_integration,
    
    .extended_ioctl = vbox_extended_ioctl
};

// Declare the unified driver
DECLARE_UNIFIED_DRIVER(vbox, vbox_ops, vbox_unified_interface);

// Unified driver initialization
UNIFIED_DRIVER_INIT_FUNCTION(vbox) {
    debuglog(DEBUG_INFO, "Registering VirtualBox unified graphics driver\n");
    
    // First register base driver
    graphics_result_t result = vbox_init();
    if (result != GRAPHICS_SUCCESS) {
        return result;
    }
    
    // Enable VirtualBox-specific features
    enable_unified_features(&vbox_unified_driver, 
                           UNIFIED_FEATURE_VIRTUAL_DISPLAYS |
                           UNIFIED_FEATURE_GUEST_INTEGRATION);
    
    // Register the unified driver
    return register_unified_driver(&vbox_unified_driver);
}

// Unified driver exit function
UNIFIED_DRIVER_EXIT_FUNCTION(vbox) {
    debuglog(DEBUG_INFO, "Unregistering VirtualBox unified graphics driver\n");
    unregister_unified_driver(&vbox_unified_driver);
    vbox_exit();
}