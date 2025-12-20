#include "../../include/graphics/display_driver.h"
#include "../../include/graphics/graphics_types.h"
#include "../../include/hardware.h"
#include "../../include/memory.h"
#include "../../include/string.h"
#include "../../include/graphics/graphics_manager.h"
#include "../../include/debuglog.h"

// AMD/ATI register offsets (Radeon series)
#define ATI_CRTC_OFFSET             0x0224  // CRTC display offset
#define ATI_CRTC_OFFSET_CNTL        0x0228  // CRTC offset control
#define ATI_CRTC_PITCH              0x022C  // CRTC pitch
#define ATI_CRTC_MORE_CNTL          0x027C  // Extended CRTC control
#define ATI_CRTC_EXT_CNTL           0x0054  // CRTC extended control
#define ATI_CRTC_GEN_CNTL           0x0050  // CRTC general control

// Display controller registers
#define ATI_CRTC_H_TOTAL_DISP       0x0200  // Horizontal total and display
#define ATI_CRTC_H_SYNC_STRT_WID    0x0204  // Horizontal sync
#define ATI_CRTC_V_TOTAL_DISP       0x0208  // Vertical total and display
#define ATI_CRTC_V_SYNC_STRT_WID    0x020C  // Vertical sync

// Control register bits
#define ATI_CRTC_EN                 (1 << 25)  // CRTC enable
#define ATI_CRTC_DISP_REQ_EN        (1 << 1)   // Display request enable
#define ATI_CRTC_HSYNC_DIS          (1 << 8)   // H-sync disable
#define ATI_CRTC_VSYNC_DIS          (1 << 9)   // V-sync disable

// Pixel format control
#define ATI_CRTC_PIX_WIDTH_MASK     (7 << 8)
#define ATI_CRTC_PIX_WIDTH_4BPP     (1 << 8)
#define ATI_CRTC_PIX_WIDTH_8BPP     (2 << 8)
#define ATI_CRTC_PIX_WIDTH_15BPP    (3 << 8)
#define ATI_CRTC_PIX_WIDTH_16BPP    (4 << 8)
#define ATI_CRTC_PIX_WIDTH_24BPP    (5 << 8)
#define ATI_CRTC_PIX_WIDTH_32BPP    (6 << 8)

// 2D acceleration registers (for hardware-accelerated operations)
#define ATI_DST_CNTL                0x530   // Destination control
#define ATI_SRC_CNTL                0x534   // Source control
#define ATI_HOST_CNTL               0x540   // Host control
#define ATI_PAT_REG0                0x580   // Pattern register 0
#define ATI_PAT_REG1                0x584   // Pattern register 1
#define ATI_PAT_CNTL                0x588   // Pattern control
#define ATI_SC_LEFT_RIGHT           0x1608  // Scissor left/right
#define ATI_SC_TOP_BOTTOM           0x160C  // Scissor top/bottom
#define ATI_DP_BKGD_CLR             0x1478  // Background color
#define ATI_DP_FRGD_CLR             0x147C  // Foreground color
#define ATI_DP_MIX                  0x16C8  // ROP control
#define ATI_DP_SRC                  0x16CC  // Source control
#define ATI_CLR_CMP_CNTL            0x1920  // Color compare control
#define ATI_GUI_TRAJ_CNTL           0x1704  // Trajectory control
#define ATI_DST_Y_X                 0x1438  // Destination Y,X
#define ATI_DST_HEIGHT_WIDTH        0x143C  // Destination height,width

// GPU families
typedef enum {
    ATI_FAMILY_UNKNOWN = 0,
    ATI_FAMILY_R100,    // Radeon 7000 series
    ATI_FAMILY_R200,    // Radeon 8000/9000 series
    ATI_FAMILY_R300,    // Radeon 9500/9600/9700/9800
    ATI_FAMILY_R400,    // Radeon X series
    ATI_FAMILY_R500,    // Radeon X1000 series
    ATI_FAMILY_R600,    // Radeon HD 2000/3000
    ATI_FAMILY_R700,    // Radeon HD 4000
    ATI_FAMILY_EVERGREEN, // Radeon HD 5000/6000
    ATI_FAMILY_SOUTHERN_ISLANDS, // Radeon HD 7000
} ati_gpu_family_t;

// Driver state for AMD/ATI Graphics
static struct {
    bool initialized;
    graphics_device_t* device;
    void* mmio_base;            // Memory-mapped I/O registers
    size_t mmio_size;
    void* framebuffer_base;     // Framebuffer memory
    size_t vram_size;
    
    // GPU information
    ati_gpu_family_t family;
    bool has_2d_accel;
    bool has_3d_accel;
    bool supports_dualhead;
    
    // Current display configuration
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_bpp;
    pixel_format_t display_format;
    uint32_t display_pitch;
    
    // CRTC state
    bool crtc_enabled;
} amd_ati_state = {
    .initialized = false,
    .device = NULL,
    .mmio_base = NULL,
    .family = ATI_FAMILY_UNKNOWN,
    .crtc_enabled = false
};

// Common AMD/ATI display modes
static const video_mode_t amd_ati_modes[] = {
    {640, 480, 16, 640*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {640, 480, 24, 640*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {640, 480, 32, 640*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {800, 600, 16, 800*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {800, 600, 24, 800*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {800, 600, 32, 800*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {1024, 768, 16, 1024*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {1024, 768, 24, 1024*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1024, 768, 32, 1024*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {1280, 1024, 16, 1280*2, PIXEL_FORMAT_RGB_565, 60, false, 0, NULL},
    {1280, 1024, 24, 1280*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1280, 1024, 32, 1280*4, PIXEL_FORMAT_RGBA_8888, 60, false, 0, NULL},
    {1600, 1200, 24, 1600*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
    {1920, 1080, 24, 1920*3, PIXEL_FORMAT_RGB_888, 60, false, 0, NULL},
};

#define AMD_ATI_MODE_COUNT (sizeof(amd_ati_modes) / sizeof(amd_ati_modes[0]))

// Driver operation implementations
static graphics_result_t amd_ati_initialize(graphics_device_t* device);
static graphics_result_t amd_ati_shutdown(graphics_device_t* device);
static graphics_result_t amd_ati_reset(graphics_device_t* device);
static graphics_result_t amd_ati_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count);
static graphics_result_t amd_ati_set_mode(graphics_device_t* device, const video_mode_t* mode);
static graphics_result_t amd_ati_get_current_mode(graphics_device_t* device, video_mode_t* mode);
static graphics_result_t amd_ati_map_framebuffer(graphics_device_t* device, framebuffer_t** fb);
static graphics_result_t amd_ati_clear_screen(graphics_device_t* device, graphics_color_t color);
static graphics_result_t amd_ati_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color);
static graphics_result_t amd_ati_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y);
static graphics_result_t amd_ati_wait_for_vsync(graphics_device_t* device);

// Helper functions
static graphics_result_t amd_ati_detect_family(graphics_device_t* device);
static graphics_result_t amd_ati_setup_crtc(const video_mode_t* mode);
static graphics_result_t amd_ati_init_2d_engine(void);
static graphics_result_t amd_ati_wait_for_idle(void);
static uint32_t amd_ati_read_reg(uint32_t offset);
static void amd_ati_write_reg(uint32_t offset, uint32_t value);
static uint32_t amd_ati_bpp_to_pixel_width(uint32_t bpp);

// AMD/ATI driver operations structure
static display_driver_ops_t amd_ati_ops = {
    .name = "amd",
    .version = 1,
    .initialize = amd_ati_initialize,
    .shutdown = amd_ati_shutdown,
    .reset = amd_ati_reset,
    .enumerate_modes = amd_ati_enumerate_modes,
    .set_mode = amd_ati_set_mode,
    .get_current_mode = amd_ati_get_current_mode,
    .map_framebuffer = amd_ati_map_framebuffer,
    .unmap_framebuffer = NULL,
    .clear_screen = amd_ati_clear_screen,
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
    .hw_fill_rect = amd_ati_hw_fill_rect,
    .hw_copy_rect = amd_ati_hw_copy_rect,
    .hw_line = NULL,
    .wait_for_vsync = amd_ati_wait_for_vsync,
    .page_flip = NULL,
    .read_edid = NULL,
    .ioctl = NULL
};

// Driver structure
DECLARE_DISPLAY_DRIVER(amd_ati, amd_ati_ops);

static graphics_result_t amd_ati_initialize(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Initializing AMD/ATI Graphics driver for device %04x:%04x\n",
            device->vendor_id, device->device_id);
    
    amd_ati_state.device = device;
    
    // Detect GPU family
    graphics_result_t result = amd_ati_detect_family(device);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Could not detect AMD/ATI GPU family\n");
    }
    
    // Map MMIO space
    if (device->mmio_base && device->mmio_size) {
        amd_ati_state.mmio_base = (void*)device->mmio_base;
        amd_ati_state.mmio_size = device->mmio_size;
        debuglog(DEBUG_INFO, "AMD/ATI MMIO mapped at 0x%08X (size: %u KB)\n",
                (uint32_t)device->mmio_base, device->mmio_size / 1024);
    } else {
        debuglog(DEBUG_ERROR, "AMD/ATI Graphics: No MMIO region found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Map framebuffer
    if (device->framebuffer_base && device->framebuffer_size) {
        amd_ati_state.framebuffer_base = (void*)device->framebuffer_base;
        amd_ati_state.vram_size = device->framebuffer_size;
        debuglog(DEBUG_INFO, "AMD/ATI framebuffer at 0x%08X (size: %u MB)\n",
                (uint32_t)device->framebuffer_base, device->framebuffer_size / (1024*1024));
    } else {
        debuglog(DEBUG_ERROR, "AMD/ATI Graphics: No framebuffer region found\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Reset hardware
    amd_ati_reset(device);
    
    // Initialize 2D acceleration engine if supported
    if (amd_ati_state.has_2d_accel) {
        result = amd_ati_init_2d_engine();
        if (result != GRAPHICS_SUCCESS) {
            debuglog(DEBUG_WARN, "Failed to initialize 2D acceleration\n");
            amd_ati_state.has_2d_accel = false;
        }
    }
    
    // Set device capabilities
    device->caps.supports_2d_accel = amd_ati_state.has_2d_accel;
    device->caps.supports_3d_accel = amd_ati_state.has_3d_accel;
    device->caps.supports_hw_cursor = true;
    device->caps.supports_page_flipping = true;
    device->caps.supports_vsync = true;
    device->caps.supports_multiple_heads = amd_ati_state.supports_dualhead;
    device->caps.max_resolution_x = 1920;
    device->caps.max_resolution_y = 1200;
    device->caps.video_memory_size = amd_ati_state.vram_size;
    
    amd_ati_state.initialized = true;
    debuglog(DEBUG_INFO, "AMD/ATI Graphics driver initialized successfully\n");
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_shutdown(graphics_device_t* device) {
    debuglog(DEBUG_INFO, "Shutting down AMD/ATI Graphics driver\n");
    
    if (amd_ati_state.initialized) {
        // Disable CRTC
        if (amd_ati_state.crtc_enabled) {
            uint32_t crtc_gen_cntl = amd_ati_read_reg(ATI_CRTC_GEN_CNTL);
            crtc_gen_cntl &= ~ATI_CRTC_EN;
            amd_ati_write_reg(ATI_CRTC_GEN_CNTL, crtc_gen_cntl);
            amd_ati_state.crtc_enabled = false;
        }
        
        amd_ati_state.initialized = false;
        amd_ati_state.device = NULL;
    }
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_reset(graphics_device_t* device) {
    if (!device || !amd_ati_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Resetting AMD/ATI Graphics hardware\n");
    
    // Disable CRTC
    uint32_t crtc_gen_cntl = amd_ati_read_reg(ATI_CRTC_GEN_CNTL);
    crtc_gen_cntl &= ~ATI_CRTC_EN;
    amd_ati_write_reg(ATI_CRTC_GEN_CNTL, crtc_gen_cntl);
    amd_ati_state.crtc_enabled = false;
    
    // Reset display registers
    amd_ati_write_reg(ATI_CRTC_OFFSET, 0);
    amd_ati_write_reg(ATI_CRTC_PITCH, 0);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_enumerate_modes(graphics_device_t* device, video_mode_t** modes, uint32_t* count) {
    if (!device || !modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *count = AMD_ATI_MODE_COUNT;
    *modes = kmalloc(sizeof(video_mode_t) * AMD_ATI_MODE_COUNT);
    
    if (!*modes) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    // Copy supported modes
    memcpy(*modes, amd_ati_modes, sizeof(video_mode_t) * AMD_ATI_MODE_COUNT);
    
    debuglog(DEBUG_INFO, "AMD/ATI Graphics enumerated %u display modes\n", *count);
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_set_mode(graphics_device_t* device, const video_mode_t* mode) {
    if (!device || !mode || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (mode->is_text_mode) {
        debuglog(DEBUG_WARN, "AMD/ATI Graphics doesn't support text modes\n");
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    debuglog(DEBUG_INFO, "AMD/ATI Graphics setting mode: %ux%ux%u\n",
            mode->width, mode->height, mode->bpp);
    
    // Validate mode
    bool mode_supported = false;
    for (uint32_t i = 0; i < AMD_ATI_MODE_COUNT; i++) {
        if (amd_ati_modes[i].width == mode->width &&
            amd_ati_modes[i].height == mode->height &&
            amd_ati_modes[i].bpp == mode->bpp) {
            mode_supported = true;
            break;
        }
    }
    
    if (!mode_supported) {
        debuglog(DEBUG_ERROR, "Unsupported mode: %ux%ux%u\n", mode->width, mode->height, mode->bpp);
        return GRAPHICS_ERROR_INVALID_MODE;
    }
    
    // Disable CRTC before mode change
    uint32_t crtc_gen_cntl = amd_ati_read_reg(ATI_CRTC_GEN_CNTL);
    crtc_gen_cntl &= ~ATI_CRTC_EN;
    amd_ati_write_reg(ATI_CRTC_GEN_CNTL, crtc_gen_cntl);
    amd_ati_state.crtc_enabled = false;
    
    // Setup CRTC for new mode
    graphics_result_t result = amd_ati_setup_crtc(mode);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to setup AMD/ATI CRTC\n");
        return result;
    }
    
    // Update state
    amd_ati_state.display_width = mode->width;
    amd_ati_state.display_height = mode->height;
    amd_ati_state.display_bpp = mode->bpp;
    amd_ati_state.display_format = mode->format;
    amd_ati_state.display_pitch = mode->pitch;
    
    // Update device current mode
    device->current_mode = *mode;
    
    // Enable CRTC
    crtc_gen_cntl = amd_ati_read_reg(ATI_CRTC_GEN_CNTL);
    crtc_gen_cntl |= ATI_CRTC_EN | ATI_CRTC_DISP_REQ_EN;
    amd_ati_write_reg(ATI_CRTC_GEN_CNTL, crtc_gen_cntl);
    amd_ati_state.crtc_enabled = true;
    
    debuglog(DEBUG_INFO, "AMD/ATI Graphics mode set successfully\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_get_current_mode(graphics_device_t* device, video_mode_t* mode) {
    if (!device || !mode || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    mode->width = amd_ati_state.display_width;
    mode->height = amd_ati_state.display_height;
    mode->bpp = amd_ati_state.display_bpp;
    mode->format = amd_ati_state.display_format;
    mode->refresh_rate = 60;
    mode->is_text_mode = false;
    mode->pitch = amd_ati_state.display_pitch;
    mode->mode_number = 0;
    mode->hw_data = NULL;
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_map_framebuffer(graphics_device_t* device, framebuffer_t** fb) {
    if (!device || !fb || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* framebuffer = kmalloc(sizeof(framebuffer_t));
    if (!framebuffer) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    framebuffer->physical_addr = (uintptr_t)amd_ati_state.framebuffer_base;
    framebuffer->virtual_addr = amd_ati_state.framebuffer_base;
    framebuffer->size = amd_ati_state.display_pitch * amd_ati_state.display_height;
    framebuffer->width = amd_ati_state.display_width;
    framebuffer->height = amd_ati_state.display_height;
    framebuffer->pitch = amd_ati_state.display_pitch;
    framebuffer->format = amd_ati_state.display_format;
    framebuffer->bpp = amd_ati_state.display_bpp;
    framebuffer->double_buffered = false;
    framebuffer->back_buffer = NULL;
    framebuffer->hw_cursor_available = true;
    framebuffer->cursor_data = NULL;
    
    *fb = framebuffer;
    
    debuglog(DEBUG_INFO, "AMD/ATI framebuffer mapped at 0x%08X, size: %u bytes\n",
            (uint32_t)framebuffer->physical_addr, framebuffer->size);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_clear_screen(graphics_device_t* device, graphics_color_t color) {
    if (!device || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    graphics_rect_t full_screen = {
        0, 0, amd_ati_state.display_width, amd_ati_state.display_height
    };
    
    return amd_ati_hw_fill_rect(device, &full_screen, color);
}

static graphics_result_t amd_ati_hw_fill_rect(graphics_device_t* device, const graphics_rect_t* rect, graphics_color_t color) {
    if (!device || !rect || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!amd_ati_state.has_2d_accel) {
        // Fall back to software implementation
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Wait for 2D engine to be idle
    amd_ati_wait_for_idle();
    
    uint32_t pixel_value = graphics_color_to_pixel(color, amd_ati_state.display_format);
    
    // Setup 2D engine for solid fill
    amd_ati_write_reg(ATI_DP_FRGD_CLR, pixel_value);
    amd_ati_write_reg(ATI_DP_MIX, 0x00070003); // SRCCOPY ROP
    amd_ati_write_reg(ATI_DP_SRC, 0x00000100);  // Foreground color as source
    
    // Set destination coordinates and size
    amd_ati_write_reg(ATI_DST_Y_X, (rect->y << 16) | rect->x);
    amd_ati_write_reg(ATI_DST_HEIGHT_WIDTH, (rect->height << 16) | rect->width);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_hw_copy_rect(graphics_device_t* device, const graphics_rect_t* src, int32_t dst_x, int32_t dst_y) {
    if (!device || !src || !amd_ati_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!amd_ati_state.has_2d_accel) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Wait for 2D engine to be idle
    amd_ati_wait_for_idle();
    
    // Setup 2D engine for screen-to-screen blit
    amd_ati_write_reg(ATI_DP_MIX, 0x00070003); // SRCCOPY ROP
    amd_ati_write_reg(ATI_DP_SRC, 0x00000200);  // Video memory as source
    
    // Set source and destination coordinates
    uint32_t src_y_x = (src->y << 16) | src->x;
    uint32_t dst_y_x = (dst_y << 16) | dst_x;
    
    // Determine blit direction to handle overlapping regions
    uint32_t direction = 0;
    if (dst_y > src->y || (dst_y == src->y && dst_x > src->x)) {
        direction = 0; // Left-to-right, top-to-bottom
    } else {
        direction = 1; // Right-to-left, bottom-to-top
        src_y_x = ((src->y + src->height - 1) << 16) | (src->x + src->width - 1);
        dst_y_x = ((dst_y + src->height - 1) << 16) | (dst_x + src->width - 1);
    }
    
    amd_ati_write_reg(ATI_SRC_CNTL, direction);
    amd_ati_write_reg(ATI_DST_Y_X, dst_y_x);
    amd_ati_write_reg(ATI_DST_HEIGHT_WIDTH, (src->height << 16) | src->width);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_wait_for_vsync(graphics_device_t* device) {
    if (!device || !amd_ati_state.initialized) {
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
static graphics_result_t amd_ati_detect_family(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint16_t device_id = device->device_id;
    
    // Detect GPU family based on device ID ranges
    if (device_id >= 0x4150 && device_id <= 0x4170) {
        amd_ati_state.family = ATI_FAMILY_R100;
        amd_ati_state.has_2d_accel = true;
        amd_ati_state.has_3d_accel = false;
    } else if (device_id >= 0x4966 && device_id <= 0x4990) {
        amd_ati_state.family = ATI_FAMILY_R200;
        amd_ati_state.has_2d_accel = true;
        amd_ati_state.has_3d_accel = true;
    } else if (device_id >= 0x4144 && device_id <= 0x4155) {
        amd_ati_state.family = ATI_FAMILY_R300;
        amd_ati_state.has_2d_accel = true;
        amd_ati_state.has_3d_accel = true;
        amd_ati_state.supports_dualhead = true;
    } else if (device_id >= 0x5460 && device_id <= 0x5470) {
        amd_ati_state.family = ATI_FAMILY_R400;
        amd_ati_state.has_2d_accel = true;
        amd_ati_state.has_3d_accel = true;
        amd_ati_state.supports_dualhead = true;
    } else if (device_id >= 0x7100 && device_id <= 0x7200) {
        amd_ati_state.family = ATI_FAMILY_R500;
        amd_ati_state.has_2d_accel = true;
        amd_ati_state.has_3d_accel = true;
        amd_ati_state.supports_dualhead = true;
    } else {
        amd_ati_state.family = ATI_FAMILY_UNKNOWN;
        amd_ati_state.has_2d_accel = false;
        amd_ati_state.has_3d_accel = false;
    }
    
    const char* family_names[] = {
        "Unknown", "R100", "R200", "R300", "R400", "R500", 
        "R600", "R700", "Evergreen", "Southern Islands"
    };
    
    debuglog(DEBUG_INFO, "Detected AMD/ATI GPU family: %s (Device ID: 0x%04X)\n",
            family_names[amd_ati_state.family], device_id);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_setup_crtc(const video_mode_t* mode) {
    if (!mode || !amd_ati_state.mmio_base) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate timing values (simplified)
    uint32_t h_total = mode->width + 160;  // Simplified horizontal total
    uint32_t v_total = mode->height + 45;  // Simplified vertical total
    
    // Set horizontal timing
    uint32_t h_total_disp = ((h_total - 1) << 16) | (mode->width - 1);
    amd_ati_write_reg(ATI_CRTC_H_TOTAL_DISP, h_total_disp);
    
    // Set vertical timing
    uint32_t v_total_disp = ((v_total - 1) << 16) | (mode->height - 1);
    amd_ati_write_reg(ATI_CRTC_V_TOTAL_DISP, v_total_disp);
    
    // Set framebuffer offset and pitch
    amd_ati_write_reg(ATI_CRTC_OFFSET, 0); // Start at beginning of framebuffer
    amd_ati_write_reg(ATI_CRTC_PITCH, mode->pitch / 8); // Pitch in 8-byte units
    
    // Set pixel format
    uint32_t crtc_gen_cntl = amd_ati_read_reg(ATI_CRTC_GEN_CNTL);
    crtc_gen_cntl &= ~ATI_CRTC_PIX_WIDTH_MASK;
    crtc_gen_cntl |= amd_ati_bpp_to_pixel_width(mode->bpp);
    amd_ati_write_reg(ATI_CRTC_GEN_CNTL, crtc_gen_cntl);
    
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_init_2d_engine(void) {
    if (!amd_ati_state.mmio_base) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    // Initialize 2D acceleration engine
    amd_ati_write_reg(ATI_DST_CNTL, 0x00000003); // Enable destination
    amd_ati_write_reg(ATI_SRC_CNTL, 0x00000000); // Reset source control
    amd_ati_write_reg(ATI_HOST_CNTL, 0x00000000); // Reset host control
    
    // Set default clipping to full screen
    amd_ati_write_reg(ATI_SC_LEFT_RIGHT, (amd_ati_state.display_width << 16) | 0);
    amd_ati_write_reg(ATI_SC_TOP_BOTTOM, (amd_ati_state.display_height << 16) | 0);
    
    debuglog(DEBUG_INFO, "AMD/ATI 2D acceleration engine initialized\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t amd_ati_wait_for_idle(void) {
    if (!amd_ati_state.mmio_base) {
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

static uint32_t amd_ati_read_reg(uint32_t offset) {
    if (!amd_ati_state.mmio_base) {
        return 0;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)amd_ati_state.mmio_base + offset);
    return *reg;
}

static void amd_ati_write_reg(uint32_t offset, uint32_t value) {
    if (!amd_ati_state.mmio_base) {
        return;
    }
    
    volatile uint32_t* reg = (volatile uint32_t*)((uint8_t*)amd_ati_state.mmio_base + offset);
    *reg = value;
}

static uint32_t amd_ati_bpp_to_pixel_width(uint32_t bpp) {
    switch (bpp) {
        case 4:  return ATI_CRTC_PIX_WIDTH_4BPP;
        case 8:  return ATI_CRTC_PIX_WIDTH_8BPP;
        case 15: return ATI_CRTC_PIX_WIDTH_15BPP;
        case 16: return ATI_CRTC_PIX_WIDTH_16BPP;
        case 24: return ATI_CRTC_PIX_WIDTH_24BPP;
        case 32: return ATI_CRTC_PIX_WIDTH_32BPP;
        default: return ATI_CRTC_PIX_WIDTH_8BPP;
    }
}

// Driver initialization function
DRIVER_INIT_FUNCTION(amd_ati) {
    debuglog(DEBUG_INFO, "Registering AMD/ATI Graphics driver\n");
    
    // Set driver flags
    amd_ati_driver.flags = DRIVER_FLAG_SUPPORTS_GRAPHICS_MODE | 
                          DRIVER_FLAG_SUPPORTS_HW_CURSOR |
                          DRIVER_FLAG_SUPPORTS_VSYNC;
    
    return register_display_driver(&amd_ati_driver);
}
