/**
 * Forest OS - Graphics Manager (V2 Bridge)
 * 
 * This file provides the legacy graphics API while using the new V2
 * driver system underneath. All functions maintain backward compatibility
 * with existing code.
 */

#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/graphics_driver_v2.h"
#include "../include/graphics/display_driver.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"
#include "../include/libc/stdio.h"

/* External V2 functions */
extern gfx_result_t gfx_init(void);
extern gfx_result_t gfx_shutdown(void);
extern gfx_result_t gfx_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
extern gfx_result_t gfx_get_framebuffer(gfx_framebuffer_t** fb);
extern gfx_result_t gfx_clear_screen(gfx_color_t color);
extern gfx_result_t gfx_draw_pixel(int32_t x, int32_t y, gfx_color_t color);
extern gfx_result_t gfx_draw_rect(const gfx_rect_t* rect, gfx_color_t color, bool filled);
extern gfx_result_t gfx_swap_buffers(void);
extern gfx_result_t gfx_get_device(uint32_t index, gfx_device_t** dev);
extern gfx_result_t gfx_get_primary_device(gfx_device_t** dev);
extern uint32_t gfx_get_device_count(void);
extern bool gfx_is_initialized(void);
extern void* gfx_get_fb_addr(void);
extern uint32_t gfx_get_fb_width(void);
extern uint32_t gfx_get_fb_height(void);
extern uint32_t gfx_get_fb_pitch(void);
extern uint32_t gfx_get_fb_bpp(void);

/* Global state */
static struct {
    bool initialized;
    framebuffer_t framebuffer;
    graphics_device_t device;
    video_mode_t current_mode;
    int32_t cursor_x;
    int32_t cursor_y;
    bool cursor_visible;
} graphics_state = {0};

/* Built-in 8x8 font for text rendering */
extern const uint8_t font8x8_basic[128][8];

/* Convert legacy color to V2 color */
static inline gfx_color_t to_v2_color(graphics_color_t c) {
    gfx_color_t v2 = {c.r, c.g, c.b, c.a};
    return v2;
}

/* Convert legacy rect to V2 rect */
static inline gfx_rect_t to_v2_rect(const graphics_rect_t* r) {
    gfx_rect_t v2 = {r->x, r->y, r->width, r->height};
    return v2;
}

static inline uint32_t fb_bytes_per_pixel_from_stride(const framebuffer_t* fb) {
    if (!fb) {
        return 0;
    }

    uint32_t bytes_pp = (fb->bpp + 7) / 8;
    if (fb->width != 0 && fb->pitch >= fb->width && (fb->pitch % fb->width) == 0) {
        uint32_t stride_bpp = fb->pitch / fb->width;
        if (stride_bpp >= 1 && stride_bpp <= 4) {
            bytes_pp = stride_bpp;
        }
    }

    return bytes_pp;
}

/* Update local framebuffer copy from V2 system */
static void update_framebuffer_state(void) {
    gfx_framebuffer_t* v2_fb = NULL;
    
    gfx_result_t result = gfx_get_framebuffer(&v2_fb);
    if (result == GFX_OK && v2_fb) {
        /* Note: Removed excessive logging here - was spamming serial output */
        
        graphics_state.framebuffer.virtual_addr = (uintptr_t)v2_fb->virt_addr;
        graphics_state.framebuffer.physical_addr = v2_fb->phys_addr;
        graphics_state.framebuffer.width = v2_fb->width;
        graphics_state.framebuffer.height = v2_fb->height;
        graphics_state.framebuffer.pitch = v2_fb->pitch;
        graphics_state.framebuffer.bpp = v2_fb->bpp;
        graphics_state.framebuffer.size = v2_fb->size;

        if (graphics_state.framebuffer.width != 0 &&
            graphics_state.framebuffer.pitch >= graphics_state.framebuffer.width &&
            (graphics_state.framebuffer.pitch % graphics_state.framebuffer.width) == 0) {
            uint32_t stride_bpp = graphics_state.framebuffer.pitch / graphics_state.framebuffer.width;
            uint32_t declared_bpp = (graphics_state.framebuffer.bpp + 7) / 8;
            if (stride_bpp >= 1 && stride_bpp <= 4 && declared_bpp != stride_bpp) {
                debuglog(DEBUG_WARN,
                         "[GFXMGR] FB bpp mismatch: reported=%u (%u Bpp), pitch/width=%u Bpp; normalizing\n",
                         graphics_state.framebuffer.bpp, declared_bpp, stride_bpp);
                graphics_state.framebuffer.bpp = stride_bpp * 8;
            }
        }
        
        switch (v2_fb->format) {
            case GFX_FORMAT_BGRX8888:
            case GFX_FORMAT_BGRA8888:
                graphics_state.framebuffer.format = PIXEL_FORMAT_BGRA_8888;
                break;
            case GFX_FORMAT_RGBX8888:
            case GFX_FORMAT_RGBA8888:
                graphics_state.framebuffer.format = PIXEL_FORMAT_RGBA_8888;
                break;
            case GFX_FORMAT_BGR888:
                graphics_state.framebuffer.format = PIXEL_FORMAT_BGR_888;
                break;
            case GFX_FORMAT_RGB888:
                graphics_state.framebuffer.format = PIXEL_FORMAT_RGB_888;
                break;
            default:
                /* Auto-detect based on bpp */
                if (graphics_state.framebuffer.bpp == 24) {
                    graphics_state.framebuffer.format = PIXEL_FORMAT_BGR_888;
                } else {
                    graphics_state.framebuffer.format = PIXEL_FORMAT_BGRA_8888;
                }
                break;
        }
        
        /* Update current mode */
        graphics_state.current_mode.width = v2_fb->width;
        graphics_state.current_mode.height = v2_fb->height;
        graphics_state.current_mode.bpp = graphics_state.framebuffer.bpp;
        graphics_state.current_mode.pitch = v2_fb->pitch;
        graphics_state.current_mode.is_text_mode = false;
        
        /* Validate framebuffer has valid virtual address */
        if (graphics_state.framebuffer.virtual_addr == 0 || 
            graphics_state.framebuffer.width == 0 ||
            graphics_state.framebuffer.height == 0) {
            debuglog(DEBUG_WARN, "[GFXMGR] Framebuffer has invalid dimensions or address!\n");
        }
    } else {
        debuglog(DEBUG_ERROR, "[GFXMGR] Failed to get V2 framebuffer: result=%d, fb=%p\n",
                 result, v2_fb);
    }
}

/* ============================================================================
 * Core Graphics Functions
 * ============================================================================ */

graphics_result_t graphics_init(void) {
    if (graphics_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "[GFXMGR] Initializing graphics manager (V2 backend)...\n");
    
    /* The V2 system should already be initialized by graphics_init.c */
    if (!gfx_is_initialized()) {
        debuglog(DEBUG_ERROR, "[GFXMGR] V2 graphics not initialized\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    /* Update our state from V2 system */
    update_framebuffer_state();
    
    /* Set up compatibility device */
    strncpy(graphics_state.device.name, "V2 Graphics Device", sizeof(graphics_state.device.name) - 1);
    graphics_state.device.type = GRAPHICS_DEVICE_VESA;
    graphics_state.device.is_active = true;
    
    graphics_state.cursor_x = 0;
    graphics_state.cursor_y = 0;
    graphics_state.cursor_visible = true;
    graphics_state.initialized = true;
    
    debuglog(DEBUG_INFO, "[GFXMGR] Graphics manager initialized: %ux%ux%u\n",
             graphics_state.framebuffer.width, graphics_state.framebuffer.height,
             graphics_state.framebuffer.bpp);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_shutdown(void) {
    if (!graphics_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "[GFXMGR] Shutting down graphics manager...\n");
    
    memset(&graphics_state, 0, sizeof(graphics_state));
    
    return GRAPHICS_SUCCESS;
}

bool graphics_is_initialized(void) {
    return graphics_state.initialized && gfx_is_initialized();
}

/* ============================================================================
 * Device Management
 * ============================================================================ */

graphics_device_t* graphics_get_primary_device(void) {
    if (!graphics_state.initialized) {
        return NULL;
    }
    return &graphics_state.device;
}

graphics_result_t graphics_set_primary_device(graphics_device_t* device) {
    (void)device;
    /* V2 system handles this automatically */
    return GRAPHICS_SUCCESS;
}

graphics_device_t* graphics_get_device(uint32_t index) {
    if (index == 0 && graphics_state.initialized) {
        return &graphics_state.device;
    }
    return NULL;
}

uint32_t graphics_get_device_count(void) {
    return gfx_get_device_count();
}

/* ============================================================================
 * Mode Management
 * ============================================================================ */

graphics_result_t graphics_set_mode(uint32_t width, uint32_t height, 
                                   uint32_t bpp, uint32_t refresh_rate) {
    (void)refresh_rate;
    
    gfx_result_t result = gfx_set_mode(width, height, bpp);
    if (result == GFX_OK) {
        update_framebuffer_state();
        return GRAPHICS_SUCCESS;
    }
    
    return GRAPHICS_ERROR_INVALID_MODE;
}

graphics_result_t graphics_set_text_mode(uint32_t cols, uint32_t rows) {
    (void)cols;
    (void)rows;
    /* V2 system doesn't switch to text mode dynamically */
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_get_current_mode(video_mode_t* mode) {
    if (!mode || !graphics_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *mode = graphics_state.current_mode;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_enumerate_modes(video_mode_t** modes, uint32_t* count) {
    if (!modes || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    /* Return just the current mode */
    video_mode_t* mode_list = (video_mode_t*)kmalloc(sizeof(video_mode_t));
    if (!mode_list) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    *mode_list = graphics_state.current_mode;
    *modes = mode_list;
    *count = 1;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_get_capabilities(graphics_capabilities_t* caps) {
    if (!caps) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    memset(caps, 0, sizeof(graphics_capabilities_t));
    caps->max_resolution_x = 4096;
    caps->max_resolution_y = 4096;
    caps->supports_hw_cursor = false;
    caps->supports_page_flipping = true;
    caps->supports_vsync = true;
    caps->supports_2d_accel = false;
    caps->supports_3d_accel = false;
    
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Framebuffer Access
 * ============================================================================ */

framebuffer_t* graphics_get_framebuffer(void) {
    if (!graphics_state.initialized) {
        return NULL;
    }
    
    update_framebuffer_state();
    return &graphics_state.framebuffer;
}

graphics_result_t graphics_map_framebuffer(framebuffer_t** fb) {
    if (!fb) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *fb = graphics_get_framebuffer();
    return (*fb) ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_GENERIC;
}

graphics_result_t graphics_unmap_framebuffer(framebuffer_t* fb) {
    (void)fb;
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Drawing Operations
 * ============================================================================ */

graphics_result_t graphics_clear_screen(graphics_color_t color) {
    gfx_result_t result = gfx_clear_screen(to_v2_color(color));
    return (result == GFX_OK) ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_GENERIC;
}

graphics_result_t graphics_draw_pixel(int32_t x, int32_t y, graphics_color_t color) {
    gfx_result_t result = gfx_draw_pixel(x, y, to_v2_color(color));
    return (result == GFX_OK) ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_GENERIC;
}

graphics_result_t graphics_draw_pixels_batch(const int32_t* x_coords, const int32_t* y_coords,
                                            graphics_color_t color, uint32_t count) {
    if (!x_coords || !y_coords || count == 0) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    gfx_color_t v2_color = to_v2_color(color);
    for (uint32_t i = 0; i < count; i++) {
        gfx_draw_pixel(x_coords[i], y_coords[i], v2_color);
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_get_pixel(int32_t x, int32_t y, graphics_color_t* color) {
    if (!color || !graphics_state.framebuffer.virtual_addr) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (x < 0 || y < 0 || 
        (uint32_t)x >= graphics_state.framebuffer.width ||
        (uint32_t)y >= graphics_state.framebuffer.height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t bytes_pp = fb_bytes_per_pixel_from_stride(&graphics_state.framebuffer);
    if (bytes_pp == 0) {
        return GRAPHICS_ERROR_GENERIC;
    }

    uint8_t* fb = (uint8_t*)graphics_state.framebuffer.virtual_addr;
    uint32_t offset = y * graphics_state.framebuffer.pitch + x * bytes_pp;
    
    if (bytes_pp == 4) {
        uint32_t pixel = *(uint32_t*)(fb + offset);
        color->b = (pixel >> 0) & 0xFF;
        color->g = (pixel >> 8) & 0xFF;
        color->r = (pixel >> 16) & 0xFF;
        color->a = 255;
    } else if (bytes_pp == 3) {
        const uint8_t* pixel = fb + offset;
        color->b = pixel[0];
        color->g = pixel[1];
        color->r = pixel[2];
        color->a = 255;
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_draw_rect(const graphics_rect_t* rect, 
                                    graphics_color_t color, bool filled) {
    if (!rect) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    gfx_rect_t v2_rect = to_v2_rect(rect);
    gfx_result_t result = gfx_draw_rect(&v2_rect, to_v2_color(color), filled);
    return (result == GFX_OK) ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_GENERIC;
}

graphics_result_t graphics_draw_line(int32_t x1, int32_t y1, 
                                    int32_t x2, int32_t y2, 
                                    graphics_color_t color) {
    /* Bresenham's line algorithm */
    int32_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int32_t dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = dx - dy;
    
    gfx_color_t v2_color = to_v2_color(color);
    
    while (1) {
        gfx_draw_pixel(x1, y1, v2_color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_blit_surface(const graphics_surface_t* surface,
                                       const graphics_rect_t* src_rect,
                                       int32_t dst_x, int32_t dst_y) {
    if (!surface || !surface->pixels) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    /* Determine source rectangle */
    int32_t src_x = src_rect ? src_rect->x : 0;
    int32_t src_y = src_rect ? src_rect->y : 0;
    uint32_t src_w = src_rect ? src_rect->width : surface->width;
    uint32_t src_h = src_rect ? src_rect->height : surface->height;
    
    /* Clip to framebuffer bounds */
    if (dst_x < 0) { src_x -= dst_x; src_w += dst_x; dst_x = 0; }
    if (dst_y < 0) { src_y -= dst_y; src_h += dst_y; dst_y = 0; }
    if (dst_x + (int32_t)src_w > (int32_t)fb->width) src_w = fb->width - dst_x;
    if (dst_y + (int32_t)src_h > (int32_t)fb->height) src_h = fb->height - dst_y;
    
    /* Copy pixels */
    uint8_t* src_pixels = (uint8_t*)surface->pixels;
    uint8_t* dst_pixels = (uint8_t*)fb->virtual_addr;
    uint32_t src_bytes_pp = (surface->bpp + 7) / 8;
    uint32_t dst_bytes_pp = fb_bytes_per_pixel_from_stride(fb);
    if (src_bytes_pp == 0 || dst_bytes_pp == 0 || src_bytes_pp != dst_bytes_pp) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    for (uint32_t y = 0; y < src_h; y++) {
        uint8_t* src_row = src_pixels + (src_y + y) * surface->pitch + src_x * src_bytes_pp;
        uint8_t* dst_row = dst_pixels + (dst_y + y) * fb->pitch + dst_x * dst_bytes_pp;
        memcpy(dst_row, src_row, src_w * src_bytes_pp);
    }
    
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Text Mode Operations
 * ============================================================================ */

graphics_result_t graphics_write_char(int32_t x, int32_t y, char c, uint8_t attr) {
    if (!graphics_state.initialized || !graphics_state.framebuffer.virtual_addr) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    if ((unsigned char)c >= 128) {
        c = '?';
    }
    
    /* Get foreground and background colors from attribute */
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;
    
    /* Standard VGA 16-color palette */
    static const uint32_t vga_palette[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
    };
    
    uint32_t fg_color = vga_palette[fg];
    uint32_t bg_color = vga_palette[bg];
    
    /* Convert character position to pixel position (8x8 font) */
    int32_t px = x * 8;
    int32_t py = y * 8;
    
    /* Get font bitmap */
    const uint8_t* bitmap = font8x8_basic[(unsigned char)c];
    
    /* Draw character */
    uint8_t* fb = (uint8_t*)graphics_state.framebuffer.virtual_addr;
    uint32_t pitch = graphics_state.framebuffer.pitch;
    uint32_t bytes_pp = fb_bytes_per_pixel_from_stride(&graphics_state.framebuffer);
    if (bytes_pp == 0) {
        return GRAPHICS_ERROR_GENERIC;
    }
    
    for (int row = 0; row < 8; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < 8; col++) {
            int32_t fx = px + col;
            int32_t fy = py + row;
            
            if (fx >= 0 && fy >= 0 && 
                (uint32_t)fx < graphics_state.framebuffer.width &&
                (uint32_t)fy < graphics_state.framebuffer.height) {
                
                uint32_t color = (bits & (1 << col)) ? fg_color : bg_color;
                uint8_t* pixel = fb + fy * pitch + fx * bytes_pp;
                
                if (bytes_pp == 4) {
                    *(uint32_t*)pixel = color;
                }
            }
        }
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_write_string(int32_t x, int32_t y, 
                                        const char* str, uint8_t attr) {
    if (!str) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    int32_t cx = x;
    int32_t cy = y;
    
    while (*str) {
        char c = *str++;
        
        if (c == '\n') {
            cx = x;
            cy++;
        } else if (c == '\r') {
            cx = x;
        } else if (c == '\t') {
            cx = (cx + 8) & ~7;
        } else {
            graphics_write_char(cx, cy, c, attr);
            cx++;
        }
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_printf(int32_t x, int32_t y, uint8_t attr, 
                                 const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    return graphics_write_string(x, y, buffer, attr);
}

graphics_result_t graphics_scroll_screen(int32_t lines) {
    if (!graphics_state.framebuffer.virtual_addr || lines == 0) {
        return GRAPHICS_SUCCESS;
    }
    
    uint8_t* fb = (uint8_t*)graphics_state.framebuffer.virtual_addr;
    uint32_t pitch = graphics_state.framebuffer.pitch;
    uint32_t height = graphics_state.framebuffer.height;
    uint32_t line_height = 8;  /* 8 pixels per text line */
    uint32_t scroll_pixels = (uint32_t)(lines > 0 ? lines : -lines) * line_height;
    
    if (lines > 0) {
        /* Scroll up */
        memmove(fb, fb + scroll_pixels * pitch, (height - scroll_pixels) * pitch);
        memset(fb + (height - scroll_pixels) * pitch, 0, scroll_pixels * pitch);
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_set_cursor_pos(int32_t x, int32_t y) {
    graphics_state.cursor_x = x;
    graphics_state.cursor_y = y;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_get_cursor_pos(int32_t* x, int32_t* y) {
    if (x) *x = graphics_state.cursor_x;
    if (y) *y = graphics_state.cursor_y;
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Cursor Management
 * ============================================================================ */

graphics_result_t graphics_set_cursor(const graphics_surface_t* cursor_surface,
                                     int32_t hotspot_x, int32_t hotspot_y) {
    (void)cursor_surface;
    (void)hotspot_x;
    (void)hotspot_y;
    /* Software cursor - handled elsewhere */
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_move_cursor(int32_t x, int32_t y) {
    graphics_state.cursor_x = x;
    graphics_state.cursor_y = y;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_show_cursor(bool show) {
    graphics_state.cursor_visible = show;
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Surface Management
 * ============================================================================ */

graphics_result_t graphics_create_surface(uint32_t width, uint32_t height,
                                         pixel_format_t format,
                                         graphics_surface_t** surface) {
    if (!surface || width == 0 || height == 0) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    graphics_surface_t* s = (graphics_surface_t*)kmalloc(sizeof(graphics_surface_t));
    if (!s) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(s, 0, sizeof(graphics_surface_t));
    s->width = width;
    s->height = height;
    s->format = format;
    s->bpp = 32;
    s->pitch = width * 4;
    
    size_t buffer_size = s->pitch * height;
    s->pixels = kmalloc(buffer_size);
    if (!s->pixels) {
        kfree(s);
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(s->pixels, 0, buffer_size);
    *surface = s;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_destroy_surface(graphics_surface_t* surface) {
    if (!surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (surface->pixels) {
        kfree(surface->pixels);
    }
    kfree(surface);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_copy_surface(const graphics_surface_t* src,
                                        graphics_surface_t* dst) {
    if (!src || !dst || !src->pixels || !dst->pixels) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t copy_width = (src->width < dst->width) ? src->width : dst->width;
    uint32_t copy_height = (src->height < dst->height) ? src->height : dst->height;
    uint32_t src_bytes_pp = (src->bpp + 7) / 8;
    uint32_t dst_bytes_pp = (dst->bpp + 7) / 8;
    if (src_bytes_pp == 0 || dst_bytes_pp == 0 || src_bytes_pp != dst_bytes_pp) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    for (uint32_t y = 0; y < copy_height; y++) {
        uint8_t* src_row = (uint8_t*)src->pixels + y * src->pitch;
        uint8_t* dst_row = (uint8_t*)dst->pixels + y * dst->pitch;
        memcpy(dst_row, src_row, copy_width * src_bytes_pp);
    }
    
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Color Utilities
 * ============================================================================ */

graphics_color_t graphics_make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    graphics_color_t c = {r, g, b, a};
    return c;
}

uint32_t graphics_color_to_pixel(graphics_color_t color, pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_BGRA_8888:
            return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
        case PIXEL_FORMAT_RGBA_8888:
            return (color.a << 24) | (color.b << 16) | (color.g << 8) | color.r;
        case PIXEL_FORMAT_RGB_565:
            return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
        default:
            return (color.r << 16) | (color.g << 8) | color.b;
    }
}

graphics_color_t graphics_pixel_to_color(uint32_t pixel, pixel_format_t format) {
    graphics_color_t c = {0, 0, 0, 255};
    
    switch (format) {
        case PIXEL_FORMAT_BGRA_8888:
            c.b = (pixel >> 0) & 0xFF;
            c.g = (pixel >> 8) & 0xFF;
            c.r = (pixel >> 16) & 0xFF;
            c.a = (pixel >> 24) & 0xFF;
            break;
        case PIXEL_FORMAT_RGBA_8888:
            c.r = (pixel >> 0) & 0xFF;
            c.g = (pixel >> 8) & 0xFF;
            c.b = (pixel >> 16) & 0xFF;
            c.a = (pixel >> 24) & 0xFF;
            break;
        default:
            c.b = (pixel >> 0) & 0xFF;
            c.g = (pixel >> 8) & 0xFF;
            c.r = (pixel >> 16) & 0xFF;
            break;
    }
    
    return c;
}

/* ============================================================================
 * Double Buffering
 * ============================================================================ */

graphics_result_t graphics_enable_double_buffering(bool enable) {
    (void)enable;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_swap_buffers(void) {
    gfx_result_t result = gfx_swap_buffers();
    return (result == GFX_OK) ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_GENERIC;
}

graphics_result_t graphics_wait_for_vsync(void) {
    return gfx_swap_buffers() == GFX_OK ? GRAPHICS_SUCCESS : GRAPHICS_ERROR_NOT_SUPPORTED;
}

/* ============================================================================
 * Font Management (stub - real implementation elsewhere)
 * ============================================================================ */

graphics_result_t graphics_load_font(const char* name, uint8_t size, font_t** font) {
    (void)name; (void)size; (void)font;
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_unload_font(font_t* font) {
    (void)font;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_draw_text(int32_t x, int32_t y, const char* text,
                                    font_t* font, graphics_color_t color) {
    (void)font; (void)color;
    return graphics_write_string(x / 8, y / 8, text, TEXT_ATTR_WHITE);
}

graphics_result_t graphics_get_text_bounds(const char* text, font_t* font,
                                          uint32_t* width, uint32_t* height) {
    (void)font;
    if (!text || !width || !height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *width = strlen(text) * 8;
    *height = 8;
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Debug and Diagnostics
 * ============================================================================ */

graphics_result_t graphics_dump_device_info(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Device: %s\n", device->name);
    debuglog(DEBUG_INFO, "  Type: %u\n", device->type);
    debuglog(DEBUG_INFO, "  Active: %s\n", device->is_active ? "yes" : "no");
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_run_diagnostics(void) {
    debuglog(DEBUG_INFO, "=== Graphics Diagnostics ===\n");
    debuglog(DEBUG_INFO, "Initialized: %s\n", graphics_state.initialized ? "yes" : "no");
    debuglog(DEBUG_INFO, "V2 System: %s\n", gfx_is_initialized() ? "yes" : "no");
    
    if (graphics_state.initialized) {
        debuglog(DEBUG_INFO, "Framebuffer: %ux%u %ubpp\n",
                 graphics_state.framebuffer.width,
                 graphics_state.framebuffer.height,
                 graphics_state.framebuffer.bpp);
        debuglog(DEBUG_INFO, "Pitch: %u\n", graphics_state.framebuffer.pitch);
        debuglog(DEBUG_INFO, "Address: 0x%lx\n", (unsigned long)graphics_state.framebuffer.virtual_addr);
    }
    
    return GRAPHICS_SUCCESS;
}

const char* graphics_get_error_string(graphics_result_t error) {
    switch (error) {
        case GRAPHICS_SUCCESS: return "Success";
        case GRAPHICS_ERROR_GENERIC: return "Generic error";
        case GRAPHICS_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case GRAPHICS_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case GRAPHICS_ERROR_NOT_SUPPORTED: return "Not supported";
        case GRAPHICS_ERROR_HARDWARE_FAULT: return "Hardware fault";
        case GRAPHICS_ERROR_INVALID_MODE: return "Invalid mode";
        case GRAPHICS_ERROR_DEVICE_BUSY: return "Device busy";
        default: return "Unknown error";
    }
}

/* ============================================================================
 * Stub functions for less commonly used features
 * ============================================================================ */

graphics_result_t graphics_register_input_handler(void (*handler)(const input_event_t* event)) {
    (void)handler;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_unregister_input_handler(void) {
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_inject_input_event(const input_event_t* event) {
    (void)event;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_set_config(const char* key, const char* value) {
    (void)key; (void)value;
    return GRAPHICS_SUCCESS;
}

graphics_result_t graphics_get_config(const char* key, char* value, size_t size) {
    (void)key; (void)value; (void)size;
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

/* Antialiased drawing stubs */
graphics_result_t graphics_draw_circle_aa(int32_t cx, int32_t cy, int32_t radius,
                                         graphics_color_t color, bool filled) {
    (void)cx; (void)cy; (void)radius; (void)color; (void)filled;
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_draw_ring_aa(int32_t cx, int32_t cy, int32_t radius,
                                       int32_t thickness, graphics_color_t color) {
    (void)cx; (void)cy; (void)radius; (void)thickness; (void)color;
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_draw_rounded_rect_aa(const graphics_rect_t* rect,
                                               int32_t corner_radius,
                                               graphics_color_t color, bool filled) {
    (void)rect; (void)corner_radius; (void)color; (void)filled;
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t graphics_draw_line_aa(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                       graphics_color_t color) {
    return graphics_draw_line(x1, y1, x2, y2, color);
}

graphics_result_t graphics_draw_pixel_blend(int32_t x, int32_t y, graphics_color_t color) {
    return graphics_draw_pixel(x, y, color);
}

/* Parallel graphics initialization for compatibility */
graphics_result_t graphics_init_parallel(graphics_surface_t* primary_surface) {
    (void)primary_surface;
    return graphics_init();
}
