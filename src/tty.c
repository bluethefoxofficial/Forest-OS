#include "include/tty.h"

#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"
#include "include/graphics/tty_font_renderer.h"
#include "include/graphics/graphics_types.h"
#include "include/debuglog.h"
#include "include/libc/stdio.h"
#include "include/smp.h"
#include "include/splash.h"
#include "include/vfs.h"
#include "include/bmp.h"

// Direct framebuffer crash screen functions
static void crash_draw_char(int x, int y, char c, uint32_t color);
static void crash_draw_string(int x, int y, const char* str, uint32_t color);
static void crash_clear_screen(uint32_t color);
static void crash_draw_hex(int x, int y, uint64_t value, int digits, uint32_t color);
#include "include/string.h"
#include "include/memory.h"
#include "include/mm.h"

typedef enum {
    TTY_BACKEND_FRAMEBUFFER = 0
} tty_backend_t;

typedef struct {
    char ch;
    uint8_t attr;
    uint8_t dirty;  // 1 if cell needs redraw, 0 if clean
} tty_cell_t;

typedef struct {
    tty_cell_t* cells;
    size_t cell_count;
    uint16_t cols;
    uint16_t rows;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint16_t saved_cursor_x;
    uint16_t saved_cursor_y;
    uint8_t fg;
    uint8_t bg;
    bool bold;
    bool faint;
    bool underline;
    bool blink;
    bool inverse;
    bool conceal;
    bool italic;
    bool strike;
    bool double_underline;
    bool overlined;
    bool framed;
    bool encircled;
    bool crossed_out;
    graphics_color_t true_fg;
    graphics_color_t true_bg;
    bool use_true_colors;
    bool initialized;
} vt_buffer_t;

#define TTY_VT_COUNT 12
#define TTY_FIRST_TTY_VT 3
#define TTY_LAST_TTY_VT 12

static vt_buffer_t g_vt_buffers[TTY_VT_COUNT];
static uint8_t g_current_vt = 1;
static bool g_vt_buffers_initialized = false;

static struct {
    tty_backend_t backend;
    uint16_t cols;
    uint16_t rows;
    uint16_t char_width;
    uint16_t char_height;
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint8_t fg;
    uint8_t bg;
    bool bold;
    bool faint;
    bool underline;
    bool blink;
    bool inverse;
    bool conceal;
    bool italic;
    bool strike;
    bool double_underline;
    bool overlined;
    bool framed;
    bool encircled;
    bool crossed_out;
    graphics_color_t true_fg;
    graphics_color_t true_bg;
    bool use_true_colors;
    bool cursor_visible;
    uint16_t saved_cursor_x;
    uint16_t saved_cursor_y;
    bool initialized;
    bool boot_mode;     // When true, bypass framebuffer TTY for fast VGA text mode
    bool graphics_app_active;  // When true, suppress TTY output (graphical app owns the display)
    tty_cell_t* cells;
    size_t cell_count;
} tty_state = {
    .backend = TTY_BACKEND_FRAMEBUFFER,
    .cols = 80,
    .rows = 25,
    .char_width = 8,
    .char_height = 8,  // Match 8x8 bitmap font
    .cursor_x = 0,
    .cursor_y = 0,
    .fg = TEXT_ATTR_LIGHT_GRAY,
    .bg = TEXT_ATTR_BLACK,
    .bold = false,
    .faint = false,
    .underline = false,
    .blink = false,
    .inverse = false,
    .conceal = false,
    .italic = false,
    .strike = false,
    .double_underline = false,
    .overlined = false,
    .framed = false,
    .encircled = false,
    .crossed_out = false,
    .true_fg = {170, 170, 170, 255},
    .true_bg = {0, 0, 0, 255},
    .use_true_colors = false,
    .cursor_visible = true,
    .saved_cursor_x = 0,
    .saved_cursor_y = 0,
    .initialized = false,
    .boot_mode = true,  // Start in boot mode for fast VGA text output
    .cells = NULL,
    .cell_count = 0,
};

typedef enum {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
    ANSI_STATE_DCS,
    ANSI_STATE_STRING
} ansi_state_t;

static struct {
    ansi_state_t state;
    int params[16];
    size_t param_count;
    bool param_in_progress;
    bool private_mode;
    char string_buffer[256];
    size_t string_length;
    char final_char;
    bool application_mode;
    bool bracketed_paste_mode;
} ansi_parser = {
    .state = ANSI_STATE_NORMAL,
    .params = {0},
    .param_count = 0,
    .param_in_progress = false,
    .private_mode = false,
    .string_buffer = {0},
    .string_length = 0,
    .final_char = 0,
    .application_mode = false,
    .bracketed_paste_mode = false,
};

// Extended 256-color palette
static graphics_color_t tty_palette_256[256];
static bool palette_initialized = false;

// Software cursor tracking (for devices without hardware cursor support)
static bool cursor_drawn = false;
static uint16_t cursor_drawn_x = 0;
static uint16_t cursor_drawn_y = 0;

// TTY status bar
#define TTY_STATUS_BAR_HEIGHT 24
static bool status_bar_drawn = false;
static bool status_bar_visible = true;

// Status bar logo cached data
static bmp_image_t* g_statusbar_logo = NULL;
static bool g_statusbar_logo_loaded = false;

// Login status tracking
static char g_login_status[64] = "Logging in...";
static bool g_user_logged_in = false;
static char g_current_user[32] = "";

// =============================================================================
// STATUS BAR LOGO - Load, scale, and render the bootup logo (BMP)
// =============================================================================

// Load logo from initrd (BMP file)
static bool tty_load_statusbar_logo(void) {
    if (g_statusbar_logo_loaded) {
        return true; // Already loaded
    }

    // Try to load the logo from initrd (BMP format)
    // Note: VFS paths should NOT have leading slash
    bmp_result_t result = bmp_load_from_file("usr/share/images/bootup/logo.bmp", &g_statusbar_logo);
    if (result != BMP_SUCCESS) {
        // Try alternative path
        result = bmp_load_from_file("bootup/logo.bmp", &g_statusbar_logo);
        if (result != BMP_SUCCESS) {
            // Try PNG fallback
            result = bmp_load_from_file("usr/share/images/bootup/logo.png", &g_statusbar_logo);
            if (result != BMP_SUCCESS) {
                result = bmp_load_from_file("bootup/logo.png", &g_statusbar_logo);
                if (result != BMP_SUCCESS) {
                    debuglog(DEBUG_WARN, "TTY: Could not find logo in initrd (error: %d)\n", result);
                    return false;
                }
            }
        }
    }

    if (!g_statusbar_logo || !g_statusbar_logo->pixel_data) {
        debuglog(DEBUG_WARN, "TTY: logo.bmp is empty or invalid\n");
        return false;
    }

    g_statusbar_logo_loaded = true;
    debuglog(DEBUG_INFO, "TTY: Loaded logo.bmp: %ux%u, %ubpp\n", 
             g_statusbar_logo->width, g_statusbar_logo->height, g_statusbar_logo->bpp);
    return true;
}

// Draw scaled logo to framebuffer at specified position
static void tty_draw_logo_scaled(int32_t dest_x, int32_t dest_y, int32_t dest_width, int32_t dest_height) {
    if (!g_statusbar_logo_loaded || !g_statusbar_logo || !g_statusbar_logo->pixel_data) {
        return;
    }

    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }

    // Bounds checking
    if (dest_x < 0 || dest_y < 0 || dest_width <= 0 || dest_height <= 0) {
        return;
    }
    if (dest_x + dest_width > (int32_t)fb->width || dest_y + dest_height > (int32_t)fb->height) {
        return;
    }

    // Get BMP image properties
    uint32_t src_width = g_statusbar_logo->width;
    uint32_t src_height = g_statusbar_logo->height;
    uint8_t bpp = g_statusbar_logo->bpp;
    uint8_t* pixel_data = g_statusbar_logo->pixel_data;
    bool top_down = g_statusbar_logo->top_down;
    
    // Calculate bytes per pixel in source image
    uint32_t src_bytes_per_pixel = bpp / 8;
    uint32_t src_row_size = src_width * src_bytes_per_pixel;
    uint32_t src_row_padding = (4 - (src_row_size % 4)) % 4;
    uint32_t src_row_size_with_padding = src_row_size + src_row_padding;

    // Calculate scaling factors
    float scale_x = (float)src_width / (float)dest_width;
    float scale_y = (float)src_height / (float)dest_height;

    // Use nearest-neighbor scaling for pixel art look
    uint32_t bytes_per_pixel = (fb->bpp + 7) / 8;
    volatile uint8_t* framebuffer = (volatile uint8_t*)fb->virtual_addr;

    for (int32_t dy = 0; dy < dest_height; dy++) {
        // Calculate source Y coordinate
        int32_t src_y = (int32_t)((float)dy * scale_y);
        if (src_y >= (int32_t)src_height) src_y = src_height - 1;
        
        // Handle BMP row ordering (bottom-up by default)
        uint32_t bmp_row = top_down ? src_y : (src_height - 1 - src_y);
        
        for (int32_t dx = 0; dx < dest_width; dx++) {
            // Calculate source X coordinate
            int32_t src_x = (int32_t)((float)dx * scale_x);
            if (src_x >= (int32_t)src_width) src_x = src_width - 1;

            // Get pixel from source image (BGR or BGRA format)
            uint32_t src_idx = (bmp_row * src_row_size_with_padding) + (src_x * src_bytes_per_pixel);
            
            uint8_t b = pixel_data[src_idx];
            uint8_t g = pixel_data[src_idx + 1];
            uint8_t r = pixel_data[src_idx + 2];
            uint8_t a = (src_bytes_per_pixel == 4) ? pixel_data[src_idx + 3] : 255;

            // Skip fully transparent pixels
            if (a < 128) {
                continue;
            }

            // Calculate destination position
            int32_t fb_x = dest_x + dx;
            int32_t fb_y = dest_y + dy;
            
            if (fb_x < 0 || fb_x >= (int32_t)fb->width || fb_y < 0 || fb_y >= (int32_t)fb->height) {
                continue;
            }

            size_t offset = ((uint32_t)fb_y * fb->pitch) + ((uint32_t)fb_x * bytes_per_pixel);

            if (a >= 250) {
                // Fully opaque - just write the pixel
                framebuffer[offset] = b;           // Blue
                framebuffer[offset + 1] = g;       // Green
                framebuffer[offset + 2] = r;        // Red
                if (bytes_per_pixel == 4) {
                    framebuffer[offset + 3] = 255; // Alpha
                }
            } else {
                // Alpha blend with background
                float alpha = (float)a / 255.0f;
                float inv_alpha = 1.0f - alpha;
                
                uint8_t bg_b = framebuffer[offset];
                uint8_t bg_g = framebuffer[offset + 1];
                uint8_t bg_r = framebuffer[offset + 2];

                framebuffer[offset] = (uint8_t)((float)b * alpha + (float)bg_b * inv_alpha);
                framebuffer[offset + 1] = (uint8_t)((float)g * alpha + (float)bg_g * inv_alpha);
                framebuffer[offset + 2] = (uint8_t)((float)r * alpha + (float)bg_r * inv_alpha);
                if (bytes_per_pixel == 4) {
                    framebuffer[offset + 3] = 255;
                }
            }
        }
    }
}

// Draw tree sprite for CPU cores (simple tree icon)
static void tty_draw_cpu_tree(int32_t x, int32_t y, int32_t size, graphics_color_t color) {
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }

    uint32_t bytes_per_pixel = (fb->bpp + 7) / 8;
    volatile uint8_t* framebuffer = (volatile uint8_t*)fb->virtual_addr;

    // Simple tree shape: triangle on top, trunk at bottom
    int32_t trunk_width = size / 3;
    int32_t trunk_height = size / 4;
    int32_t tree_top = size - trunk_height;

    for (int32_t dy = 0; dy < size; dy++) {
        for (int32_t dx = 0; dx < size; dx++) {
            int32_t fb_x = x + dx;
            int32_t fb_y = y + dy;

            if (fb_x < 0 || fb_x >= (int32_t)fb->width || fb_y < 0 || fb_y >= (int32_t)fb->height) {
                continue;
            }

            bool is_tree = false;

            // Tree foliage (triangle shape)
            if (dy < tree_top) {
                int32_t width_at_y = (dy * size) / tree_top;
                int32_t left_edge = (size - width_at_y) / 2;
                int32_t right_edge = left_edge + width_at_y;
                if (dx >= left_edge && dx <= right_edge) {
                    is_tree = true;
                }
            }
            // Trunk
            else if (dx >= (size - trunk_width) / 2 && dx < (size + trunk_width) / 2) {
                is_tree = true;
            }

            if (is_tree) {
                size_t offset = ((uint32_t)fb_y * fb->pitch) + ((uint32_t)fb_x * bytes_per_pixel);
                framebuffer[offset] = color.b;           // Blue
                framebuffer[offset + 1] = color.g;       // Green
                framebuffer[offset + 2] = color.r;       // Red
                if (bytes_per_pixel == 4) {
                    framebuffer[offset + 3] = 255;       // Alpha
                }
            }
        }
    }
}

// CPU core sprites
#define CPU_SPRITE_SIZE 40
#define CPU_SPRITE_SPACING 15
static const graphics_color_t cpu_sprite_colors[] = {
    {255, 0, 0, 255},       // Bright Red
    {255, 165, 0, 255},     // Orange
    {255, 255, 0, 255},     // Yellow
    {0, 255, 0, 255},       // Lime Green
    {0, 255, 128, 255},     // Spring Green
    {0, 255, 255, 255},     // Cyan
    {0, 128, 255, 255},     // Sky Blue
    {0, 0, 255, 255},       // Blue
    {128, 0, 255, 255},     // Purple
    {255, 0, 255, 255},     // Magenta
    {255, 0, 128, 255},     // Hot Pink
    {255, 20, 147, 255},    // Deep Pink
    {255, 69, 0, 255},      // Red Orange
    {255, 215, 0, 255},     // Gold
    {50, 205, 50, 255},     // Lime Green
    {0, 191, 255, 255},     // Deep Sky Blue
};

// Draw a CPU core sprite at position (x,y) with given color
static void tty_draw_cpu_sprite(int32_t x, int32_t y, graphics_color_t color) {
    // Draw border (2 pixels thick)
    graphics_color_t border_color = {255, 255, 255, 255}; // White border

    // Outer border
    graphics_draw_rect(&(graphics_rect_t){x, y, CPU_SPRITE_SIZE, CPU_SPRITE_SIZE}, border_color, false);
    graphics_draw_rect(&(graphics_rect_t){x+1, y+1, CPU_SPRITE_SIZE-2, CPU_SPRITE_SIZE-2}, border_color, false);

    // Fill inside
    graphics_draw_rect(&(graphics_rect_t){x+2, y+2, CPU_SPRITE_SIZE-4, CPU_SPRITE_SIZE-4}, color, true);
}

// Display CPU core sprites on framebuffer
static void tty_display_cpu_sprites(void) {
    uint32_t cpu_count = smp_get_cpu_count();
    if (cpu_count == 0) cpu_count = 1;

    // Get current video mode
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS) {
        return; // Can't get mode, skip sprites
    }

    // Position sprites below the TTY status bar, in top-right area
    int32_t start_x = mode.width - (cpu_count * (CPU_SPRITE_SIZE + CPU_SPRITE_SPACING)) - CPU_SPRITE_SPACING;
    int32_t start_y = TTY_STATUS_BAR_HEIGHT + CPU_SPRITE_SPACING;

    for (uint32_t i = 0; i < cpu_count && i < sizeof(cpu_sprite_colors)/sizeof(cpu_sprite_colors[0]); i++) {
        tty_draw_cpu_sprite(start_x + i * (CPU_SPRITE_SIZE + CPU_SPRITE_SPACING), start_y, cpu_sprite_colors[i]);
    }
}

// Clear CPU core sprites from framebuffer
static void tty_clear_cpu_sprites(void) {
    uint32_t cpu_count = smp_get_cpu_count();
    if (cpu_count == 0) cpu_count = 1;

    // Get current video mode
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS) {
        return; // Can't get mode, skip
    }

    // Position sprites below the TTY status bar, in top-right area
    int32_t start_x = mode.width - (cpu_count * (CPU_SPRITE_SIZE + CPU_SPRITE_SPACING)) - CPU_SPRITE_SPACING;
    int32_t start_y = TTY_STATUS_BAR_HEIGHT + CPU_SPRITE_SPACING;

    graphics_color_t bg_color = {0, 0, 0, 255}; // Black background

    for (uint32_t i = 0; i < cpu_count && i < sizeof(cpu_sprite_colors)/sizeof(cpu_sprite_colors[0]); i++) {
        graphics_draw_rect(&(graphics_rect_t){
            start_x + i * (CPU_SPRITE_SIZE + CPU_SPRITE_SPACING),
            start_y,
            CPU_SPRITE_SIZE,
            CPU_SPRITE_SIZE
        }, bg_color, true);
    }
}

// Helper to render a string using 8x8 font directly to framebuffer
static void tty_render_string_8x8(int32_t x, int32_t y, const char* str, graphics_color_t fg, graphics_color_t bg) {
    if (!str) return;

    tty_font_t* tty_font = NULL;
    if (tty_font_load_builtin("tty-8x8", 8, &tty_font) != TTY_FONT_SUCCESS || !tty_font) {
        return;
    }

    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }

    graphics_surface_t surface;
    surface.pixels = (void*)fb->virtual_addr;
    surface.width = fb->width;
    surface.height = fb->height;
    surface.pitch = fb->pitch;
    surface.format = fb->format;
    surface.bpp = fb->bpp;

    int32_t px = x;
    for (const char* p = str; *p; p++) {
        tty_font_render_char(tty_font, &surface, px, y, (uint32_t)*p, fg, bg);
        px += 8;  // 8x8 font width
    }
}

void tty_draw_status_bar(void) {
    // Don't draw status bar when hidden or when a graphics app owns the display
    if (!status_bar_visible || tty_state.graphics_app_active) {
        return;
    }
    
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }

    // Draw status bar background
    graphics_color_t status_bg = {30, 30, 35, 255};
    graphics_color_t status_text = {200, 200, 200, 255};
    graphics_rect_t status_rect = {0, 0, fb->width, TTY_STATUS_BAR_HEIGHT};

    graphics_draw_rect(&status_rect, status_bg, true);

    // Draw separator line
    graphics_color_t separator = {60, 60, 70, 255};
    graphics_rect_t line_rect = {0, TTY_STATUS_BAR_HEIGHT - 2, fb->width, 2};
    graphics_draw_rect(&line_rect, separator, true);

    // Load and draw the logo (scaled to fit in status bar)
    // Logo goes on the left side, after the system name
    if (!g_statusbar_logo_loaded) {
        tty_load_statusbar_logo();
    }
    
    if (g_statusbar_logo_loaded && g_statusbar_logo && g_statusbar_logo->pixel_data) {
        // Scale logo to fit in status bar - max 20px height
        int32_t max_logo_height = TTY_STATUS_BAR_HEIGHT - 4;
        int32_t max_logo_width = 100; // Max width for logo
        
        // Calculate scaled size maintaining aspect ratio
        float aspect_ratio = (float)g_statusbar_logo->width / (float)g_statusbar_logo->height;
        int32_t logo_width = max_logo_height * aspect_ratio;
        int32_t logo_height = max_logo_height;
        
        // Limit width if too large
        if (logo_width > max_logo_width) {
            logo_width = max_logo_width;
            logo_height = max_logo_width / aspect_ratio;
        }
        
        // Position logo after "Forest OS" text (which is at x=10, 8 chars = 64px)
        // Add some padding
        int32_t logo_x = 80;
        int32_t logo_y = (TTY_STATUS_BAR_HEIGHT - logo_height) / 2;
        
        tty_draw_logo_scaled(logo_x, logo_y, logo_width, logo_height);
    }

    // Draw system name
    tty_render_string_8x8(10, 8, "Forest OS", status_text, status_bg);

    // Get CPU count for tree sprites
    uint32_t cpu_count = smp_get_cpu_count();
    if (cpu_count == 0) cpu_count = 1;
    
    // Tree sprite size (smaller for status bar)
    #define TREE_SIZE 16
    #define TREE_SPACING 4
    
    // Calculate position for tree sprites (right side of status bar)
    int32_t trees_start_x = fb->width - (cpu_count * (TREE_SIZE + TREE_SPACING)) - 10;
    
    // Draw tree for each CPU core
    for (uint32_t i = 0; i < cpu_count; i++) {
        // Use green color for trees
        graphics_color_t tree_color = {34, 139, 34, 255}; // Forest Green
        tty_draw_cpu_tree(trees_start_x + i * (TREE_SIZE + TREE_SPACING), 
                         (TTY_STATUS_BAR_HEIGHT - TREE_SIZE) / 2, 
                         TREE_SIZE, tree_color);
    }

    // Draw login status in the center area
    // Position between logo area and tree area
    const char* status_msg = g_user_logged_in ? g_current_user : g_login_status;
    int32_t status_x = 200; // Position after logo
    if (status_x + 100 < (int32_t)fb->width - (cpu_count * (TREE_SIZE + TREE_SPACING)) - 20) {
        tty_render_string_8x8(status_x, 8, status_msg, status_text, status_bg);
    }

    status_bar_drawn = true;
}

void tty_clear_status_bar(void) {
    if (!status_bar_drawn) return;
    
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }
    
    // Clear status bar area
    graphics_color_t black = {0, 0, 0, 255};
    graphics_rect_t clear_rect = {0, 0, fb->width, TTY_STATUS_BAR_HEIGHT};
    graphics_draw_rect(&clear_rect, black, true);
    
    status_bar_drawn = false;
}

void tty_set_status_bar_visible(bool visible) {
    status_bar_visible = visible;
    if (!visible && status_bar_drawn) {
        tty_clear_status_bar();
    }
}

bool tty_is_status_bar_visible(void) {
    return status_bar_visible;
}

static void tty_init_256_palette(void) {
    if (palette_initialized) return;
    
    // Standard 16 colors (0-15)
    tty_palette_256[0]  = (graphics_color_t){0, 0, 0, 255};         // Black
    tty_palette_256[1]  = (graphics_color_t){128, 0, 0, 255};       // Dark Red
    tty_palette_256[2]  = (graphics_color_t){0, 128, 0, 255};       // Dark Green
    tty_palette_256[3]  = (graphics_color_t){128, 128, 0, 255};     // Dark Yellow
    tty_palette_256[4]  = (graphics_color_t){0, 0, 128, 255};       // Dark Blue
    tty_palette_256[5]  = (graphics_color_t){128, 0, 128, 255};     // Dark Magenta
    tty_palette_256[6]  = (graphics_color_t){0, 128, 128, 255};     // Dark Cyan
    tty_palette_256[7]  = (graphics_color_t){192, 192, 192, 255};   // Light Gray
    tty_palette_256[8]  = (graphics_color_t){128, 128, 128, 255};   // Dark Gray
    tty_palette_256[9]  = (graphics_color_t){255, 0, 0, 255};       // Bright Red
    tty_palette_256[10] = (graphics_color_t){0, 255, 0, 255};       // Bright Green
    tty_palette_256[11] = (graphics_color_t){255, 255, 0, 255};     // Bright Yellow
    tty_palette_256[12] = (graphics_color_t){0, 0, 255, 255};       // Bright Blue
    tty_palette_256[13] = (graphics_color_t){255, 0, 255, 255};     // Bright Magenta
    tty_palette_256[14] = (graphics_color_t){0, 255, 255, 255};     // Bright Cyan
    tty_palette_256[15] = (graphics_color_t){255, 255, 255, 255};   // White
    
    // 6x6x6 color cube (16-231)
    for (int i = 0; i < 216; i++) {
        int r = (i / 36) % 6;
        int g = (i / 6) % 6;
        int b = i % 6;
        tty_palette_256[16 + i] = (graphics_color_t){
            .r = (uint8_t)(r ? 55 + r * 40 : 0),
            .g = (uint8_t)(g ? 55 + g * 40 : 0),
            .b = (uint8_t)(b ? 55 + b * 40 : 0),
            .a = 255
        };
    }
    
    // Grayscale ramp (232-255)
    for (int i = 0; i < 24; i++) {
        uint8_t level = (uint8_t)(8 + i * 10);
        tty_palette_256[232 + i] = (graphics_color_t){level, level, level, 255};
    }
    
    palette_initialized = true;
}

static graphics_color_t tty_color_from_nibble(uint8_t nibble) {
    if (!palette_initialized) {
        tty_init_256_palette();
    }
    return tty_palette_256[nibble & 0x0F];
}

static graphics_color_t tty_color_from_256(uint8_t index) {
    if (!palette_initialized) {
        tty_init_256_palette();
    }
    return tty_palette_256[index];
}

static uint8_t tty_current_attr(void);

static uint8_t tty_palette_best_match(uint8_t r, uint8_t g, uint8_t b, bool allow_bright) {
    if (!palette_initialized) {
        tty_init_256_palette();
    }
    
    uint32_t best_error = UINT32_MAX;
    uint8_t best_index = 7; // default to light gray
    uint8_t limit = allow_bright ? 16 : 8;

    for (uint8_t i = 0; i < limit; i++) {
        int32_t dr = (int32_t)r - (int32_t)tty_palette_256[i].r;
        int32_t dg = (int32_t)g - (int32_t)tty_palette_256[i].g;
        int32_t db = (int32_t)b - (int32_t)tty_palette_256[i].b;
        uint32_t error = (uint32_t)(dr * dr + dg * dg + db * db);
        if (error < best_error) {
            best_error = error;
            best_index = i;
        }
    }

    return best_index;
}

static uint8_t tty_map_rgb_to_attr(uint8_t r, uint8_t g, uint8_t b, bool is_background) {
    uint8_t match = tty_palette_best_match(r, g, b, true);
    if (is_background) {
        match &= 0x07; // background plane supports only base colors
    }
    return match;
}

static uint8_t tty_map_256_color(uint8_t idx, bool is_background) {
    (void)idx; (void)is_background;
    if (idx < 16) {
        return is_background ? (idx & 0x07) : (idx & 0x0F);
    }

    uint8_t r, g, b;
    if (idx >= 16 && idx <= 231) {
        uint8_t cube = (uint8_t)(idx - 16);
        r = (uint8_t)((cube / 36) % 6 * 51);
        g = (uint8_t)((cube / 6) % 6 * 51);
        b = (uint8_t)(cube % 6 * 51);
    } else {
        uint8_t gray = (uint8_t)(8 + (idx - 232) * 10);
        r = g = b = gray;
    }

    return tty_map_rgb_to_attr(r, g, b, is_background);
}

static void tty_update_dimensions_from_graphics(void) {
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS) {
        return;
    }

    // Subtract status bar height from available height
    uint32_t available_height = mode.height;
    if (available_height > TTY_STATUS_BAR_HEIGHT) {
        available_height -= TTY_STATUS_BAR_HEIGHT;
    }

    // Always derive terminal dimensions from framebuffer mode using 8x8 font metrics
    uint16_t char_w = 8;
    uint16_t char_h = 8;  // Fixed: Changed from 16 to 8 to match 8x8 font
    tty_font_t* tty_font = NULL;
    if (tty_font_load_builtin("tty-8x8", 8, &tty_font) == TTY_FONT_SUCCESS && tty_font) {
        char_w = tty_font->width;
        char_h = tty_font->height;
    }

    if (char_w == 0 || char_h == 0) {
        return; // Avoid division by zero
    }

    uint16_t cols = (uint16_t)(mode.width / char_w);
    uint16_t rows = (uint16_t)(available_height / char_h);
    
    // Sanity check for reasonable terminal dimensions
    if (cols > 0 && rows > 0 && cols <= 200 && rows <= 200) {
        tty_state.cols = cols;
        tty_state.rows = rows;
        tty_state.char_width = char_w;
        tty_state.char_height = char_h;
    }
}

static void tty_render_cell(uint16_t x, uint16_t y, char ch, uint8_t attr);
static void tty_update_cursor_visual(void);

static void tty_apply_cursor(void) {
    graphics_set_cursor_pos(tty_state.cursor_x, tty_state.cursor_y);
    tty_update_cursor_visual();
}

static inline size_t tty_cell_index(uint16_t x, uint16_t y) {
    return (size_t)y * (size_t)tty_state.cols + x;
}

static void tty_redraw_cell_at(uint16_t x, uint16_t y) {
    if (!tty_state.cells || x >= tty_state.cols || y >= tty_state.rows) {
        return;
    }
    size_t idx = tty_cell_index(x, y);
    tty_cell_t cell = tty_state.cells[idx];
    tty_render_cell(x, y, cell.ch, cell.attr);
}

static void tty_update_cursor_visual(void) {
    if (!tty_state.initialized || !tty_state.cells) {
        return;
    }

    // Restore previous cursor cell if needed
    if (cursor_drawn) {
        tty_redraw_cell_at(cursor_drawn_x, cursor_drawn_y);
        cursor_drawn = false;
    }

    if (!tty_state.cursor_visible ||
        tty_state.cursor_x >= tty_state.cols ||
        tty_state.cursor_y >= tty_state.rows) {
        return;
    }

    size_t idx = tty_cell_index(tty_state.cursor_x, tty_state.cursor_y);
    tty_cell_t cell = tty_state.cells[idx];
    uint8_t fg = cell.attr & 0x0F;
    uint8_t bg = (cell.attr >> 4) & 0x0F;
    uint8_t block_attr = (fg << 4) | bg; // swap fg/bg to show a solid block
    tty_render_cell(tty_state.cursor_x, tty_state.cursor_y, cell.ch ? cell.ch : ' ', block_attr);

    cursor_drawn_x = tty_state.cursor_x;
    cursor_drawn_y = tty_state.cursor_y;
    cursor_drawn = true;
}

// Forward declaration for crash_font (defined below in crash screen section)
extern const uint16_t crash_font[95][16];

static void tty_render_cell_framebuffer(uint16_t x, uint16_t y, char ch, uint8_t attr) {
    // Use TTY font renderer
    tty_font_t* tty_font = NULL;
    if (tty_font_load_builtin("tty-8x8", 8, &tty_font) == TTY_FONT_SUCCESS && tty_font) {
        framebuffer_t* fb = graphics_get_framebuffer();
        if (!fb || !fb->virtual_addr) {
            return;
        }

        graphics_surface_t surface;
        surface.pixels = (void*)fb->virtual_addr;
        surface.width = fb->width;
        surface.height = fb->height;
        surface.pitch = fb->pitch;
        surface.format = fb->format;
        surface.bpp = fb->bpp;

        int32_t py_offset = TTY_STATUS_BAR_HEIGHT;
        int32_t px = (int32_t)x * tty_state.char_width;
        int32_t py = (int32_t)y * tty_state.char_height + py_offset;

        // Get colors
        graphics_color_t fg_c = tty_color_from_nibble(attr & 0x0F);
        graphics_color_t bg_c = tty_color_from_nibble((attr >> 4) & 0x0F);

        tty_font_render_char(tty_font, &surface, px, py, (uint32_t)ch, fg_c, bg_c);
        return;
    }

    // Fallback: Direct framebuffer rendering using crash_font bitmap
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;  // Silently fail - avoid log spam
    }

    int32_t py_offset = TTY_STATUS_BAR_HEIGHT;
    int32_t px = (int32_t)x * tty_state.char_width;
    int32_t py = (int32_t)y * tty_state.char_height + py_offset;

    // Bounds checking
    if (px < 0 || py < 0 || px + tty_state.char_width > (int32_t)fb->width || py + tty_state.char_height > (int32_t)fb->height) {
        return;
    }

    // Get colors as 32-bit packed values for fast rendering
    graphics_color_t fg_c = tty_color_from_nibble(attr & 0x0F);
    graphics_color_t bg_c = tty_color_from_nibble((attr >> 4) & 0x0F);
    uint32_t fg_color = (fg_c.a << 24) | (fg_c.r << 16) | (fg_c.g << 8) | fg_c.b;
    uint32_t bg_color = (bg_c.a << 24) | (bg_c.r << 16) | (bg_c.g << 8) | bg_c.b;

    // Get font glyph index (crash_font covers ASCII 32-126)
    int char_index = ch - 32;
    if (char_index < 0 || char_index >= 95) {
        char_index = 0; // Use space for invalid chars
    }

    // Calculate bytes per pixel from actual bpp
    uint32_t bytes_per_pixel = (fb->bpp + 7) / 8;
    volatile uint8_t* framebuffer = (volatile uint8_t*)fb->virtual_addr;

    // Render using actual font bitmap
    for (int32_t cy = 0; cy < tty_state.char_height; cy++) {
        uint16_t row_bits = crash_font[char_index][cy];
        size_t row_offset = ((uint32_t)(py + cy) * fb->pitch) + ((uint32_t)px * bytes_per_pixel);

        for (int32_t cx = 0; cx < tty_state.char_width; cx++) {
            // Check if bit is set in font bitmap (0x80 >> cx tests each bit from MSB)
            uint32_t pixel_color = (row_bits & (0x80 >> cx)) ? fg_color : bg_color;
            size_t pixel_offset = row_offset + (cx * bytes_per_pixel);
            
            // Write pixel based on bpp (handles 24bpp and 32bpp)
            framebuffer[pixel_offset] = pixel_color & 0xFF;         // Blue
            framebuffer[pixel_offset + 1] = (pixel_color >> 8) & 0xFF;  // Green
            framebuffer[pixel_offset + 2] = (pixel_color >> 16) & 0xFF; // Red
            if (bytes_per_pixel == 4) {
                framebuffer[pixel_offset + 3] = (pixel_color >> 24) & 0xFF; // Alpha (only for 32bpp)
            }
        }
    }
}

static void tty_render_cell(uint16_t x, uint16_t y, char ch, uint8_t attr) {
    // Always use framebuffer rendering with 8x8 font to avoid TrueType corruption
    // The 8x8 bitmap font is more reliable for TTY output
    if (graphics_is_initialized()) {
        framebuffer_t* fb = graphics_get_framebuffer();
        if (fb && fb->virtual_addr) {
            tty_render_cell_framebuffer(x, y, ch, attr);
            return;
        }
    }
    // Fall back to graphics text mode if framebuffer not available
    graphics_write_char(x, y, ch, attr);
}

static void tty_flush_screen(void) {
    // Don't flush TTY when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }
    
    if (!tty_state.cells || tty_state.cols == 0 || tty_state.rows == 0) {
        return;
    }

    // Draw status bar first
    tty_draw_status_bar();

    // Only render dirty cells for performance (critical for VirtualBox)
    for (uint16_t y = 0; y < tty_state.rows; y++) {
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            size_t idx = tty_cell_index(x, y);
            tty_cell_t* cell = &tty_state.cells[idx];
            if (cell->dirty) {
                tty_render_cell(x, y, cell->ch, cell->attr);
                cell->dirty = 0;  // Mark as clean after rendering
            }
        }
    }
    tty_apply_cursor();
}

// Force full screen redraw (used for tty_clear and initial display)
static void tty_flush_screen_full(void) {
    // Don't flush TTY when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }
    
    if (!tty_state.cells || tty_state.cols == 0 || tty_state.rows == 0) {
        return;
    }

    // Draw status bar first
    tty_draw_status_bar();

    for (uint16_t y = 0; y < tty_state.rows; y++) {
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            size_t idx = tty_cell_index(x, y);
            tty_cell_t* cell = &tty_state.cells[idx];
            tty_render_cell(x, y, cell->ch, cell->attr);
            cell->dirty = 0;  // Mark as clean
        }
    }
    tty_apply_cursor();
}

static bool tty_set_dimensions(uint16_t cols, uint16_t rows) {
    if (cols == 0 || rows == 0) {
        return false;
    }

    size_t new_count = (size_t)cols * (size_t)rows;
    if (tty_state.cells && new_count == tty_state.cell_count) {
        tty_state.cols = cols;
        tty_state.rows = rows;
        if (tty_state.cursor_x >= cols) tty_state.cursor_x = cols - 1;
        if (tty_state.cursor_y >= rows) tty_state.cursor_y = rows - 1;
        return true;
    }

    tty_cell_t* new_cells = (tty_cell_t*)kzalloc(new_count * sizeof(tty_cell_t));
    if (!new_cells) {
        return false;
    }

    uint8_t attr = tty_current_attr();
    for (size_t i = 0; i < new_count; i++) {
        new_cells[i].ch = ' ';
        new_cells[i].attr = attr;
        new_cells[i].dirty = 1;  // Mark new cells as dirty for initial render
    }

    if (tty_state.cells) {
        uint16_t copy_rows = tty_state.rows < rows ? tty_state.rows : rows;
        uint16_t copy_cols = tty_state.cols < cols ? tty_state.cols : cols;
        for (uint16_t y = 0; y < copy_rows; y++) {
            memcpy(&new_cells[y * cols],
                   &tty_state.cells[y * tty_state.cols],
                   copy_cols * sizeof(tty_cell_t));
        }
        kfree(tty_state.cells);
    }

    tty_state.cells = new_cells;
    tty_state.cell_count = new_count;
    tty_state.cols = cols;
    tty_state.rows = rows;
    if (tty_state.cursor_x >= cols) tty_state.cursor_x = cols - 1;
    if (tty_state.cursor_y >= rows) tty_state.cursor_y = rows - 1;
    return true;
}

static uint8_t tty_current_attr(void) {
    uint8_t fg = tty_state.fg & 0x0F;
    uint8_t bg = tty_state.bg & 0x0F;

    // Bold/bright handling
    if (tty_state.bold && !(fg & TEXT_ATTR_BRIGHT)) {
        fg |= TEXT_ATTR_BRIGHT;
    }
    if (tty_state.underline && !(fg & TEXT_ATTR_BRIGHT)) {
        fg |= TEXT_ATTR_BRIGHT;
    }
    if (tty_state.faint) {
        fg &= (uint8_t)~TEXT_ATTR_BRIGHT;
    }

    // Inverse video swaps the planes
    if (tty_state.inverse) {
        uint8_t tmp = fg;
        fg = bg;
        bg = tmp;
    }

    if (tty_state.conceal) {
        fg = bg;
    }

    uint8_t attr = (uint8_t)((bg << 4) | (fg & 0x0F));
    if (tty_state.blink) {
        attr |= TEXT_ATTR_BLINK;
    }

    return attr;
}

static void tty_backend_put(char c) {
    uint8_t attr = tty_current_attr();
    if (tty_state.cursor_x >= tty_state.cols || tty_state.cursor_y >= tty_state.rows) {
        return;
    }

    size_t idx = tty_cell_index(tty_state.cursor_x, tty_state.cursor_y);
    if (tty_state.cells && idx < tty_state.cell_count) {
        tty_state.cells[idx].ch = c;
        tty_state.cells[idx].attr = attr;
    }

    tty_render_cell(tty_state.cursor_x, tty_state.cursor_y, c, attr);

    // Also output to serial for debugging
    debuglog_write_char(c);
}

static void tty_backend_clear_line_from_cursor(void) {
    uint8_t attr = tty_current_attr();
    uint16_t y = tty_state.cursor_y;
    for (uint16_t x = tty_state.cursor_x; x < tty_state.cols; x++) {
        size_t idx = tty_cell_index(x, y);
        if (tty_state.cells && idx < tty_state.cell_count) {
            tty_state.cells[idx].ch = ' ';
            tty_state.cells[idx].attr = attr;
        }
        tty_render_cell(x, y, ' ', attr);
    }
    tty_state.cursor_x = 0;
}

static void tty_scroll_if_needed(void) {
    if (tty_state.cursor_y < tty_state.rows) {
        return;
    }

    uint8_t attr = tty_current_attr();

    if (tty_state.cells) {
        // Shift all cells up by one row using memmove
        size_t line_size = (size_t)tty_state.cols * sizeof(tty_cell_t);
        memmove(tty_state.cells,
                tty_state.cells + tty_state.cols,
                line_size * (tty_state.rows - 1));

        // Mark ALL cells as dirty since content shifted
        for (size_t i = 0; i < tty_state.cell_count; i++) {
            tty_state.cells[i].dirty = 1;
        }

        // Clear the last row
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            size_t idx = tty_cell_index(x, tty_state.rows - 1);
            tty_state.cells[idx].ch = ' ';
            tty_state.cells[idx].attr = attr;
            tty_state.cells[idx].dirty = 1;
        }
        tty_state.cursor_y = tty_state.rows - 1;
        tty_flush_screen();
    } else {
        // Use graphics subsystem for scrolling
        graphics_scroll_screen(1);
        tty_state.cursor_y = tty_state.rows - 1;
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            graphics_write_char(x, tty_state.cursor_y, ' ', attr);
        }
    }
}

static void tty_handle_control(char c) {
    switch (c) {
        case '\n': // Line Feed
            tty_state.cursor_x = 0;
            tty_state.cursor_y++;
            tty_scroll_if_needed();
            break;
        case '\r': // Carriage Return
            tty_state.cursor_x = 0;
            break;
        case '\b': // Backspace
            if (tty_state.cursor_x > 0) {
                tty_state.cursor_x--;
                tty_backend_put(' ');
            }
            break;
        case '\t': // Horizontal Tab
            tty_state.cursor_x = (uint16_t)((tty_state.cursor_x + 8) & ~(uint16_t)(8 - 1));
            if (tty_state.cursor_x >= tty_state.cols) {
                tty_state.cursor_x = 0;
                tty_state.cursor_y++;
                tty_scroll_if_needed();
            }
            break;
        case '\v': // Vertical Tab
            tty_state.cursor_y++;
            tty_scroll_if_needed();
            break;
        case '\f': // Form Feed
            tty_clear();
            break;
        case '\a': // Bell - could implement system beep
            // For now, just ignore
            break;
        case 0x7F: // Delete
            // Could implement character deletion
            break;
        default:
            if (c >= 32 || c < 0) { // Printable characters
                tty_backend_put(c);
                tty_state.cursor_x++;
                if (tty_state.cursor_x >= tty_state.cols) {
                    tty_state.cursor_x = 0;
                    tty_state.cursor_y++;
                    tty_scroll_if_needed();
                }
            }
            break;
    }

    tty_apply_cursor();
}

static void tty_reset_ansi_parser(void) {
    ansi_parser.state = ANSI_STATE_NORMAL;
    ansi_parser.param_count = 0;
    ansi_parser.param_in_progress = false;
    ansi_parser.private_mode = false;
    ansi_parser.string_length = 0;
    ansi_parser.final_char = 0;
    memset(ansi_parser.params, 0, sizeof(ansi_parser.params));
    memset(ansi_parser.string_buffer, 0, sizeof(ansi_parser.string_buffer));
}

static void tty_handle_osc_command(void) {
    // OSC sequences: ESC ] Ps ; Pt ST
    // Common OSC sequences:
    // OSC 0 ; title ST  - Set window title
    // OSC 1 ; name ST   - Set window name
    // OSC 2 ; title ST  - Set window title (same as OSC 0)
    // OSC 4 ; color ; rgb ST - Set color palette
    // Implement basic OSC handling here if needed
    (void)ansi_parser.string_buffer; // Suppress unused warning for now
}

static void tty_handle_dcs_command(void) {
    // DCS sequences: ESC P ... ST
    // Used for various device control functions
    // For now, just ignore DCS sequences
    (void)ansi_parser.string_buffer; // Suppress unused warning for now
}

static uint8_t tty_color_nibble_from_ansi(int code) {
    bool bright = false;
    uint8_t base_color = 0;

    if (code >= 90 && code <= 97) {
        base_color = (uint8_t)(code - 90);
        bright = true;
    } else if (code >= 30 && code <= 37) {
        base_color = (uint8_t)(code - 30);
    } else if (code >= 100 && code <= 107) {
        base_color = (uint8_t)(code - 100);
        bright = true;
    } else if (code >= 40 && code <= 47) {
        base_color = (uint8_t)(code - 40);
    } else {
        return 0xFF;
    }

    if (bright) {
        base_color |= TEXT_ATTR_BRIGHT;
    }

    return (uint8_t)(base_color & 0x0F);
}

static void tty_handle_sgr(const int* params, size_t count) {
    if (count == 0) {
        // Reset all attributes
        tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
        tty_state.bold = false;
        tty_state.faint = false;
        tty_state.underline = false;
        tty_state.double_underline = false;
        tty_state.blink = false;
        tty_state.inverse = false;
        tty_state.conceal = false;
        tty_state.italic = false;
        tty_state.strike = false;
        tty_state.crossed_out = false;
        tty_state.overlined = false;
        tty_state.framed = false;
        tty_state.encircled = false;
        tty_state.use_true_colors = false;
        return;
    }

    for (size_t i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) {
            // Reset all attributes
            tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
            tty_state.bold = false;
            tty_state.faint = false;
            tty_state.underline = false;
            tty_state.double_underline = false;
            tty_state.blink = false;
            tty_state.inverse = false;
            tty_state.conceal = false;
            tty_state.italic = false;
            tty_state.strike = false;
            tty_state.crossed_out = false;
            tty_state.overlined = false;
            tty_state.framed = false;
            tty_state.encircled = false;
            tty_state.use_true_colors = false;
        } else if (p == 1) {
            tty_state.bold = true;
            tty_state.faint = false;
        } else if (p == 2) {
            tty_state.faint = true;
            tty_state.bold = false;
        } else if (p == 3) {
            tty_state.italic = true;
        } else if (p == 4) {
            tty_state.underline = true;
            tty_state.double_underline = false;
        } else if (p == 5 || p == 6) {
            tty_state.blink = true;
        } else if (p == 7) {
            tty_state.inverse = true;
        } else if (p == 8) {
            tty_state.conceal = true;
        } else if (p == 9) {
            tty_state.strike = true;
        } else if (p == 21) {
            tty_state.double_underline = true;
            tty_state.underline = false;
        } else if (p == 22) {
            tty_state.bold = false;
            tty_state.faint = false;
        } else if (p == 23) {
            tty_state.italic = false;
        } else if (p == 24) {
            tty_state.underline = false;
            tty_state.double_underline = false;
        } else if (p == 25) {
            tty_state.blink = false;
        } else if (p == 27) {
            tty_state.inverse = false;
        } else if (p == 28) {
            tty_state.conceal = false;
        } else if (p == 29) {
            tty_state.strike = false;
        } else if (p == 51) {
            tty_state.framed = true;
        } else if (p == 52) {
            tty_state.encircled = true;
        } else if (p == 53) {
            tty_state.overlined = true;
        } else if (p == 54) {
            tty_state.framed = false;
            tty_state.encircled = false;
        } else if (p == 55) {
            tty_state.overlined = false;
        } else if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97)) {
            uint8_t nibble = tty_color_nibble_from_ansi(p);
            if (nibble != 0xFF) {
                tty_state.fg = nibble;
                tty_state.use_true_colors = false;
            }
        } else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
            uint8_t nibble = tty_color_nibble_from_ansi(p);
            if (nibble != 0xFF) {
                tty_state.bg = nibble & 0x07; // backgrounds limited to base palette
                tty_state.use_true_colors = false;
            }
        } else if (p == 39) {
            tty_state.fg = TEXT_ATTR_LIGHT_GRAY;
            tty_state.use_true_colors = false;
        } else if (p == 49) {
            tty_state.bg = TEXT_ATTR_BLACK;
            tty_state.use_true_colors = false;
        } else if (p == 38 || p == 48) {
            bool is_bg = (p == 48);
            if (i + 1 < count) {
                int mode = params[i + 1];
                if (mode == 5 && (i + 2) < count) {
                    // 256-color mode: ESC[38;5;n or ESC[48;5;n
                    uint8_t idx = (uint8_t)params[i + 2];
                    if (!palette_initialized) {
                        tty_init_256_palette();
                    }
                    if (is_bg) {
                        tty_state.true_bg = tty_palette_256[idx];
                        tty_state.bg = tty_map_256_color(idx, true);
                    } else {
                        tty_state.true_fg = tty_palette_256[idx];
                        tty_state.fg = tty_map_256_color(idx, false);
                    }
                    tty_state.use_true_colors = true;
                    i += 2;
                } else if (mode == 2 && (i + 4) < count) {
                    // Truecolor mode: ESC[38;2;r;g;b or ESC[48;2;r;g;b
                    uint8_t r = (uint8_t)params[i + 2];
                    uint8_t g = (uint8_t)params[i + 3];
                    uint8_t b = (uint8_t)params[i + 4];
                    if (is_bg) {
                        tty_state.true_bg = (graphics_color_t){r, g, b, 255};
                        tty_state.bg = tty_map_rgb_to_attr(r, g, b, true);
                    } else {
                        tty_state.true_fg = (graphics_color_t){r, g, b, 255};
                        tty_state.fg = tty_map_rgb_to_attr(r, g, b, false);
                    }
                    tty_state.use_true_colors = true;
                    i += 4;
                }
            }
        }
    }
}

static void tty_handle_csi_command(char command) {
    if (command == 'm') {
        tty_handle_sgr(ansi_parser.params, ansi_parser.param_count);
        return;
    }

    if ((command == 'h' || command == 'l') && ansi_parser.private_mode) {
        for (size_t i = 0; i < ansi_parser.param_count; i++) {
            switch (ansi_parser.params[i]) {
                case 25:  // DECTCEM - cursor visibility
                    tty_state.cursor_visible = (command == 'h');
                    break;
                case 47:  // Alternate screen buffer
                case 1047:
                case 1049:
                    // Switch to/from alternate screen buffer
                    // For now, just acknowledge but don't implement
                    break;
                case 7:   // Auto wrap mode
                    // Enable/disable line wrapping
                    break;
                case 1:   // Application cursor keys
                    ansi_parser.application_mode = (command == 'h');
                    break;
                case 2004: // Bracketed paste mode
                    ansi_parser.bracketed_paste_mode = (command == 'h');
                    break;
                default:
                    // Handle other private mode sequences
                    break;
            }
        }
        return;
    }

    if (command == 's') {
        tty_state.saved_cursor_x = tty_state.cursor_x;
        tty_state.saved_cursor_y = tty_state.cursor_y;
        return;
    }

    if (command == 'u') {
        tty_state.cursor_x = tty_state.saved_cursor_x < tty_state.cols ? tty_state.saved_cursor_x : (tty_state.cols - 1);
        tty_state.cursor_y = tty_state.saved_cursor_y < tty_state.rows ? tty_state.saved_cursor_y : (tty_state.rows - 1);
        tty_apply_cursor();
        return;
    }

    // Cursor movement commands
    if (command == 'A' || command == 'B' || command == 'C' || command == 'D' || 
        command == 'E' || command == 'F' || command == 'G') {
        int amount = (ansi_parser.param_count > 0 && ansi_parser.params[0] > 0) ? ansi_parser.params[0] : 1;
        switch (command) {
            case 'A': // Cursor up
                tty_state.cursor_y = (tty_state.cursor_y >= amount) ? (tty_state.cursor_y - amount) : 0;
                break;
            case 'B': // Cursor down
                tty_state.cursor_y = (tty_state.cursor_y + amount < tty_state.rows) ? (tty_state.cursor_y + amount) : (tty_state.rows - 1);
                break;
            case 'C': // Cursor right
                tty_state.cursor_x = (tty_state.cursor_x + amount < tty_state.cols) ? (tty_state.cursor_x + amount) : (tty_state.cols - 1);
                break;
            case 'D': // Cursor left
                tty_state.cursor_x = (tty_state.cursor_x >= amount) ? (tty_state.cursor_x - amount) : 0;
                break;
            case 'E': // Cursor next line
                tty_state.cursor_x = 0;
                tty_state.cursor_y = (tty_state.cursor_y + amount < tty_state.rows) ? (tty_state.cursor_y + amount) : (tty_state.rows - 1);
                break;
            case 'F': // Cursor previous line
                tty_state.cursor_x = 0;
                tty_state.cursor_y = (tty_state.cursor_y >= amount) ? (tty_state.cursor_y - amount) : 0;
                break;
            case 'G': // Cursor horizontal absolute
                if (amount > 0) amount--; // 1-based to 0-based
                tty_state.cursor_x = (amount < tty_state.cols) ? amount : (tty_state.cols - 1);
                break;
        }
        tty_apply_cursor();
        return;
    }

    if (command == 'H' || command == 'f') {
        uint16_t row = 1;
        uint16_t col = 1;
        if (ansi_parser.param_count >= 1 && ansi_parser.params[0] > 0) {
            row = (uint16_t)ansi_parser.params[0];
        }
        if (ansi_parser.param_count >= 2 && ansi_parser.params[1] > 0) {
            col = (uint16_t)ansi_parser.params[1];
        }
        if (row > 0) row--;
        if (col > 0) col--;
        tty_state.cursor_y = row < tty_state.rows ? row : tty_state.rows - 1;
        tty_state.cursor_x = col < tty_state.cols ? col : tty_state.cols - 1;
        tty_apply_cursor();
        return;
    }

    if (command == 'J') {
        // 0: cursor to end, 1: start to cursor, 2: entire screen, 3: entire screen + saved lines
        int mode = (ansi_parser.param_count > 0) ? ansi_parser.params[0] : 0;
        if (mode == 2 || mode == 3) {
            tty_clear();
            tty_state.cursor_x = 0;
            tty_state.cursor_y = 0;
            tty_apply_cursor();
        } else if (mode == 0) {
            // Clear from cursor to end of screen
            uint16_t start_y = tty_state.cursor_y;
            uint16_t start_x = tty_state.cursor_x;
            for (uint16_t y = start_y; y < tty_state.rows; y++) {
                for (uint16_t x = (y == start_y ? start_x : 0); x < tty_state.cols; x++) {
                    tty_state.cursor_x = x;
                    tty_state.cursor_y = y;
                    tty_backend_put(' ');
                }
            }
            tty_state.cursor_x = start_x;
            tty_state.cursor_y = start_y;
            tty_apply_cursor();
        } else if (mode == 1) {
            // Clear from start to cursor
            uint16_t end_y = tty_state.cursor_y;
            uint16_t end_x = tty_state.cursor_x;
            for (uint16_t y = 0; y <= end_y; y++) {
                for (uint16_t x = 0; x < (y == end_y ? end_x + 1 : tty_state.cols); x++) {
                    tty_state.cursor_x = x;
                    tty_state.cursor_y = y;
                    tty_backend_put(' ');
                }
            }
            tty_state.cursor_x = end_x;
            tty_state.cursor_y = end_y;
            tty_apply_cursor();
        }
        return;
    }

    if (command == 'K') {
        int mode = (ansi_parser.param_count > 0) ? ansi_parser.params[0] : 0;
        if (mode == 0) {
            tty_backend_clear_line_from_cursor();
        } else {
            uint16_t original_x = tty_state.cursor_x;
            if (mode == 1) {
                // Clear from start to cursor
                tty_state.cursor_x = 0;
                tty_backend_clear_line_from_cursor();
            } else if (mode == 2) {
                // Clear entire line
                tty_state.cursor_x = 0;
                for (uint16_t x = 0; x < tty_state.cols; x++) {
                    tty_backend_put(' ');
                    tty_state.cursor_x++;
                }
            }
            tty_state.cursor_x = original_x < tty_state.cols ? original_x : (tty_state.cols - 1);
        }
        tty_apply_cursor();
        return;
    }

    // Cursor position request
    if (command == 'n' && ansi_parser.param_count > 0 && ansi_parser.params[0] == 6) {
        // Device Status Report - Cursor Position Report
        // Should respond with ESC[{row};{col}R but we don't have output capability here
        return;
    }

    // Insert/Delete operations
    if (command == 'L' || command == 'M' || command == 'P' || command == '@') {
        int amount = (ansi_parser.param_count > 0 && ansi_parser.params[0] > 0) ? ansi_parser.params[0] : 1;
        (void)amount;  // TODO: implement insert/delete operations
        // Implement insert/delete lines and characters
        switch (command) {
            case 'L': // Insert lines
            case 'M': // Delete lines  
            case 'P': // Delete characters
            case '@': // Insert characters
                // These would require more complex buffer manipulation
                // For now, acknowledge but don't implement
                break;
        }
        return;
    }

    // Scrolling region
    if (command == 'r') {
        // Set scrolling region: ESC[top;bottomr
        uint16_t top = 1, bottom = tty_state.rows;
        if (ansi_parser.param_count >= 1 && ansi_parser.params[0] > 0) {
            top = (uint16_t)ansi_parser.params[0];
        }
        if (ansi_parser.param_count >= 2 && ansi_parser.params[1] > 0) {
            bottom = (uint16_t)ansi_parser.params[1];
        }
        // Validate and store scrolling region (not fully implemented)
        // For now, just acknowledge the command
        (void)top; (void)bottom;
        return;
    }

    // Set/reset modes (screen modes)
    if ((command == 'h' || command == 'l') && !ansi_parser.private_mode) {
        for (size_t i = 0; i < ansi_parser.param_count; i++) {
            switch (ansi_parser.params[i]) {
                case 4: // Insert mode
                    // Would enable/disable insert mode
                    break;
                case 20: // Automatic newline mode
                    // Would enable/disable automatic CR->CRLF
                    break;
                default:
                    break;
            }
        }
        return;
    }

    // Tab operations
    if (command == 'I') {
        // Forward tabulation
        int amount = (ansi_parser.param_count > 0 && ansi_parser.params[0] > 0) ? ansi_parser.params[0] : 1;
        for (int i = 0; i < amount; i++) {
            tty_state.cursor_x = (uint16_t)((tty_state.cursor_x + 8) & ~(uint16_t)(8 - 1));
            if (tty_state.cursor_x >= tty_state.cols) {
                tty_state.cursor_x = 0;
                tty_state.cursor_y++;
                tty_scroll_if_needed();
            }
        }
        tty_apply_cursor();
        return;
    }
    if (command == 'Z') {
        // Backward tabulation
        int amount = (ansi_parser.param_count > 0 && ansi_parser.params[0] > 0) ? ansi_parser.params[0] : 1;
        for (int i = 0; i < amount; i++) {
            if (tty_state.cursor_x >= 8) {
                tty_state.cursor_x = (uint16_t)((tty_state.cursor_x - 1) & ~(uint16_t)(8 - 1));
            } else {
                tty_state.cursor_x = 0;
            }
        }
        tty_apply_cursor();
        return;
    }
}

static void tty_process_ansi(char c) {
    switch (ansi_parser.state) {
        case ANSI_STATE_NORMAL:
            if (c == '\x1B') {
                ansi_parser.state = ANSI_STATE_ESC;
            } else {
                tty_handle_control(c);
            }
            break;

        case ANSI_STATE_ESC:
            if (c == '[') {
                ansi_parser.state = ANSI_STATE_CSI;
                ansi_parser.param_count = 0;
                ansi_parser.param_in_progress = false;
                ansi_parser.private_mode = false;
                memset(ansi_parser.params, 0, sizeof(ansi_parser.params));
            } else if (c == ']') {
                ansi_parser.state = ANSI_STATE_OSC;
                ansi_parser.string_length = 0;
                memset(ansi_parser.string_buffer, 0, sizeof(ansi_parser.string_buffer));
            } else if (c == 'P') {
                ansi_parser.state = ANSI_STATE_DCS;
                ansi_parser.string_length = 0;
                memset(ansi_parser.string_buffer, 0, sizeof(ansi_parser.string_buffer));
            } else if (c == '7') {
                // DECSC - Save cursor position
                tty_state.saved_cursor_x = tty_state.cursor_x;
                tty_state.saved_cursor_y = tty_state.cursor_y;
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else if (c == '8') {
                // DECRC - Restore cursor position
                tty_state.cursor_x = tty_state.saved_cursor_x < tty_state.cols ? tty_state.saved_cursor_x : (tty_state.cols - 1);
                tty_state.cursor_y = tty_state.saved_cursor_y < tty_state.rows ? tty_state.saved_cursor_y : (tty_state.rows - 1);
                tty_apply_cursor();
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else if (c == 'c') {
                // RIS - Reset to Initial State
                tty_clear();
                tty_state.cursor_x = 0;
                tty_state.cursor_y = 0;
                tty_state.fg = TEXT_ATTR_LIGHT_GRAY;
                tty_state.bg = TEXT_ATTR_BLACK;
                tty_state.bold = false;
                tty_state.faint = false;
                tty_state.underline = false;
                tty_state.blink = false;
                tty_state.inverse = false;
                tty_state.use_true_colors = false;
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else if (c == 'M') {
                // Reverse Index - move cursor up one line, scroll if needed
                if (tty_state.cursor_y > 0) {
                    tty_state.cursor_y--;
                } else {
                    // Would need to implement reverse scroll here
                }
                tty_apply_cursor();
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else if (c == 'D') {
                // Index - move cursor down one line, scroll if needed
                tty_state.cursor_y++;
                tty_scroll_if_needed();
                tty_apply_cursor();
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else if (c == 'E') {
                // Next Line - move to start of next line
                tty_state.cursor_x = 0;
                tty_state.cursor_y++;
                tty_scroll_if_needed();
                tty_apply_cursor();
                ansi_parser.state = ANSI_STATE_NORMAL;
            } else {
                // Unknown escape, treat literally
                ansi_parser.state = ANSI_STATE_NORMAL;
                tty_handle_control(c);
            }
            break;

        case ANSI_STATE_CSI:
            if (c >= '0' && c <= '9') {
                if (ansi_parser.param_count < (sizeof(ansi_parser.params) / sizeof(ansi_parser.params[0]))) {
                    ansi_parser.params[ansi_parser.param_count] = ansi_parser.params[ansi_parser.param_count] * 10 + (c - '0');
                    ansi_parser.param_in_progress = true;
                }
            } else if (c == '?') {
                ansi_parser.private_mode = true;
            } else if (c == ';' || c == ':') {
                if (ansi_parser.param_in_progress) {
                    ansi_parser.param_count++;
                    ansi_parser.param_in_progress = false;
                } else {
                    // Empty parameter defaults to zero
                    if (ansi_parser.param_count < (sizeof(ansi_parser.params) / sizeof(ansi_parser.params[0]))) {
                        ansi_parser.params[ansi_parser.param_count++] = 0;
                    }
                }
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '@' || c == '`' || c == '~') {
                if (ansi_parser.param_in_progress || ansi_parser.param_count == 0) {
                    ansi_parser.param_count++;
                }
                ansi_parser.final_char = c;
                tty_handle_csi_command(c);
                tty_reset_ansi_parser();
            }
            break;

        case ANSI_STATE_OSC:
            if (c == '\x07' || c == '\x9C') { // BEL or ST
                tty_handle_osc_command();
                tty_reset_ansi_parser();
            } else if (c == '\x1B') {
                ansi_parser.state = ANSI_STATE_STRING;
            } else if (ansi_parser.string_length < sizeof(ansi_parser.string_buffer) - 1) {
                ansi_parser.string_buffer[ansi_parser.string_length++] = c;
            }
            break;

        case ANSI_STATE_DCS:
            if (c == '\x1B') {
                ansi_parser.state = ANSI_STATE_STRING;
            } else if (c == '\x9C') { // ST
                tty_handle_dcs_command();
                tty_reset_ansi_parser();
            } else if (ansi_parser.string_length < sizeof(ansi_parser.string_buffer) - 1) {
                ansi_parser.string_buffer[ansi_parser.string_length++] = c;
            }
            break;

        case ANSI_STATE_STRING:
            if (c == '\\') { // ESC \ (ST)
                if (ansi_parser.state == ANSI_STATE_STRING) {
                    tty_handle_osc_command();
                    tty_reset_ansi_parser();
                }
            } else {
                ansi_parser.state = ANSI_STATE_NORMAL;
                tty_handle_control(c);
            }
            break;
    }
}

bool tty_init(void) {
    if (!tty_state.initialized) {
        // Initialize with reasonable defaults
        tty_state.cols = 80;
        tty_state.rows = 25;
        tty_init_256_palette();  // Initialize the extended color palette
    }

    // Framebuffer-only TTY - require graphics to be initialized
    if (!graphics_is_initialized()) {
        debuglog(DEBUG_ERROR, "TTY: graphics subsystem required for framebuffer console\n");
        debuglog(DEBUG_INFO, "TTY: Checking V2 graphics status directly...\n");
        
        // Try to get more info about what's available
        extern bool gfx_is_initialized(void);
        extern uint32_t gfx_get_fb_width(void);
        extern uint32_t gfx_get_fb_height(void);
        extern uint32_t gfx_get_fb_bpp(void);
        extern void* gfx_get_fb_addr(void);
        
        if (gfx_is_initialized()) {
            debuglog(DEBUG_INFO, "TTY: V2 graphics IS initialized! Trying to use it directly...\n");
            debuglog(DEBUG_INFO, "TTY: V2 framebuffer: %ux%u %ubpp @ %p\n",
                    gfx_get_fb_width(), gfx_get_fb_height(), gfx_get_fb_bpp(), gfx_get_fb_addr());
            
            // Try to force graphics_init() since V2 is ready
            extern graphics_result_t graphics_init(void);
            graphics_result_t init_result = graphics_init();
            if (init_result == GRAPHICS_SUCCESS) {
                debuglog(DEBUG_INFO, "TTY: Manually initialized legacy graphics manager!\n");
            } else {
                debuglog(DEBUG_ERROR, "TTY: Manual graphics_init() failed\n");
                return false;
            }
        } else {
            debuglog(DEBUG_ERROR, "TTY: V2 graphics is NOT initialized\n");
            return false;
        }
    }

    // Initialize TTY font renderer
    if (tty_font_renderer_init() != TTY_FONT_SUCCESS) {
        debuglog(DEBUG_ERROR, "TTY: failed to initialize TTY font renderer\n");
        return false;
    }

    // Try to set a graphics mode suitable for text rendering
    debuglog(DEBUG_INFO, "TTY: Attempting to set graphics mode for framebuffer console...\n");

    // Test writing directly to framebuffer since graphics_set_mode is broken
    debuglog(DEBUG_INFO, "TTY: Testing direct framebuffer access\n");

    framebuffer_t* fb = NULL;
    graphics_result_t result = graphics_map_framebuffer(&fb);
    debuglog(DEBUG_INFO, "TTY: graphics_map_framebuffer returned %s\n", graphics_get_error_string(result));

    if (result == GRAPHICS_SUCCESS && fb) {
        debuglog(DEBUG_INFO, "TTY: Framebuffer mapped at 0x%x, size %u bytes, %ux%u\n",
                (uint32_t)(uintptr_t)fb->virtual_addr, fb->size, fb->width, fb->height);

        // Set up basic framebuffer console with 8x8 font
        tty_state.backend = TTY_BACKEND_FRAMEBUFFER;
        tty_state.cols = fb->width / 8;
        // Reserve space for status bar (24 pixels)
        tty_state.rows = (fb->height - 24) / 8;  // Use 8x8 font height
        tty_state.char_width = 8;
        tty_state.char_height = 8;  // Match 8x8 font

        // Try to update with actual font metrics
        tty_update_dimensions_from_graphics();

        debuglog(DEBUG_INFO, "TTY: framebuffer console size %ux%u, char size %ux%u\n",
                tty_state.cols, tty_state.rows, tty_state.char_width, tty_state.char_height);

        // Unmap framebuffer
        graphics_unmap_framebuffer(fb);

        // Now complete the initialization - allocate cell buffer
        if (!tty_set_dimensions(tty_state.cols, tty_state.rows)) {
            debuglog(DEBUG_ERROR, "TTY: failed to allocate screen buffer\n");
            return false;
        }

        tty_reset_ansi_parser();
        tty_state.initialized = true;
        debuglog(DEBUG_INFO, "TTY: framebuffer console fully initialized\n");

        // Initialize virtual terminal buffers for VT 3-12
        tty_init_vt_buffers();

        // Clear screen after full initialization
        tty_clear();
        return true;
    } else {
        debuglog(DEBUG_ERROR, "TTY: Failed to map framebuffer\n");
    }

    debuglog(DEBUG_ERROR, "TTY: failed to initialize framebuffer console\n");
    return false;
}

void tty_clear(void) {
    // Always reset cursor position first
    tty_state.cursor_x = 0;
    tty_state.cursor_y = 0;

    uint8_t attr = tty_current_attr();

    // Ensure cell buffer exists
    if (!tty_state.cells) {
        tty_set_dimensions(tty_state.cols, tty_state.rows);
    }

    // Clear cell buffer if it exists
    if (tty_state.cells) {
        for (size_t i = 0; i < tty_state.cell_count; i++) {
            tty_state.cells[i].ch = ' ';
            tty_state.cells[i].attr = attr;
            tty_state.cells[i].dirty = 1;  // Mark all as dirty for full redraw
        }
        tty_flush_screen_full();  // Use full redraw for clear operation
    } else {
        // Fallback: use graphics subsystem for clearing
        graphics_color_t bg = tty_color_from_nibble((attr >> 4) & 0x0F);
        graphics_clear_screen(bg);
    }

    // Always apply cursor position after clearing
    tty_apply_cursor();
}

void tty_force_redraw(void) {
    // Force a full screen redraw without clearing content
    // Used when switching TTY sessions via Ctrl+Alt+Fn
    if (!tty_state.initialized) {
        return;
    }

    // Don't redraw TTY when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }

    // Mark all cells as dirty
    if (tty_state.cells) {
        for (size_t i = 0; i < tty_state.cell_count; i++) {
            tty_state.cells[i].dirty = 1;
        }
    }

    // Redraw status bar with current TTY session
    tty_draw_status_bar();

    // Flush entire screen
    tty_flush_screen_full();
}

void tty_putc(char c) {
    // Skip TTY output when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }
    tty_process_ansi(c);
}

void tty_write(const char* text) {
    if (!text) return;
    // Skip TTY output when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }
    while (*text) {
        tty_process_ansi(*text++);
    }
}

void tty_write_ansi(const char* text) {
    if (!text) return;
    // Skip TTY output when a graphics app owns the display
    if (tty_state.graphics_app_active) {
        return;
    }
    while (*text) {
        tty_process_ansi(*text++);
    }
}

void tty_set_attr(uint8_t attr) {
    tty_state.fg = attr & 0x0F;
    tty_state.bg = (attr >> 4) & 0x0F;
    tty_state.blink = (attr & TEXT_ATTR_BLINK) != 0;
    tty_state.bold = (attr & TEXT_ATTR_BRIGHT) != 0;
    tty_state.faint = false;
    tty_state.inverse = false;
    tty_state.underline = false;
    // No longer needed - graphics subsystem handles all rendering
}

uint8_t tty_get_attr(void) {
    return tty_current_attr();
}

bool tty_uses_graphics_backend(void) {
    return true; // Always true for framebuffer-only TTY
}

bool tty_try_enable_graphics_backend(void) {
    // Graphics backend is always enabled in framebuffer-only TTY
    return graphics_is_initialized();
}

bool tty_is_ready(void) {
    // Return false during boot mode to use fast VGA text mode for boot messages
    if (tty_state.boot_mode) {
        return false;
    }
    return tty_state.initialized && graphics_is_initialized();
}

// Exit boot mode and switch to framebuffer TTY for graphics
// Call this after early boot is complete (e.g., before starting desktop)
void tty_exit_boot_mode(void) {
    if (!tty_state.boot_mode) {
        return;  // Already exited boot mode
    }

    tty_state.boot_mode = false;
    debuglog(DEBUG_INFO, "TTY: Exited boot mode, framebuffer TTY now active\n");

    // Clear and redraw screen with framebuffer TTY (skip if splash is still visible)
    if (tty_state.initialized && !splash_is_running()) {
        tty_clear();
    }

    // Display CPU core sprites if graphics is initialized
    if (graphics_is_initialized()) {
        tty_display_cpu_sprites();
    }
}

// Check if still in boot mode
bool tty_in_boot_mode(void) {
    return tty_state.boot_mode;
}

// =============================================================================
// GRAPHICS APP MODE - Suppress TTY output when graphical apps own the display
// =============================================================================

// Clear framebuffer directly - used when switching between GUI and TTY
void tty_clear_framebuffer_raw(void) {
    framebuffer_t* fb = NULL;
    if (graphics_map_framebuffer(&fb) != GRAPHICS_SUCCESS || !fb) {
        return;
    }

    if (fb->virtual_addr && fb->size) {
        memset((uint8_t*)fb->virtual_addr, 0, fb->size);
        __asm__ volatile("mfence" ::: "memory");
    }
    graphics_unmap_framebuffer(fb);
}

// Enable graphics app mode - TTY output to framebuffer is suppressed
// Call this before launching a graphical application (like CanopyDM)
void tty_set_graphics_app_active(bool active) {
    bool was_active = tty_state.graphics_app_active;
    tty_state.graphics_app_active = active;
    
    if (active && !was_active) {
        // Entering graphics app mode - clear the status bar so it doesn't overlap
        tty_clear_status_bar();
        // Clear the framebuffer so TTY remnants don't show behind GUI apps
        if (graphics_is_initialized()) {
            tty_clear_framebuffer_raw();
        }
        debuglog(DEBUG_INFO, "TTY: Graphics app mode enabled, TTY output suppressed\n");
    } else if (!active && was_active) {
        debuglog(DEBUG_INFO, "TTY: Graphics app mode disabled, TTY output restored\n");
        // Redraw TTY content when graphical app exits
        tty_force_redraw();
    }
}

// Check if graphics app mode is active
bool tty_is_graphics_app_active(void) {
    return tty_state.graphics_app_active;
}

// =============================================================================
// CRASH SCREEN FUNCTIONS - Direct framebuffer access bypassing graphics subsystem
// =============================================================================

// Simple 8x16 bitmap font for crash screen (only essential ASCII characters)
const uint16_t crash_font[95][16] = {
    // Space (32)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ! (33)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0000, 0x0000, 0x0800, 0x0800, 0x0000, 0x0000},
    // " (34)
    {0x0000, 0x0000, 0x0000, 0x2400, 0x2400, 0x2400, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // # (35)
    {0x0000, 0x0000, 0x0000, 0x2400, 0x2400, 0x7E00, 0x2400, 0x2400,
     0x2400, 0x7E00, 0x2400, 0x2400, 0x0000, 0x0000, 0x0000, 0x0000},
    // $ (36)
    {0x0000, 0x0000, 0x0800, 0x1C00, 0x2A00, 0x2800, 0x1C00, 0x0A00,
     0x0A00, 0x2800, 0x2A00, 0x1C00, 0x0800, 0x0000, 0x0000, 0x0000},
    // % (37)
    {0x0000, 0x0000, 0x0000, 0x6200, 0x9200, 0x6400, 0x0800, 0x1000,
     0x2600, 0x4900, 0x4600, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // & (38)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200, 0x1C00, 0x1D00,
     0x2500, 0x2200, 0x1D00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ' (39)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x0800, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ( (40)
    {0x0000, 0x0000, 0x0200, 0x0400, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0400, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000},
    // ) (41)
    {0x0000, 0x0000, 0x0800, 0x0400, 0x0200, 0x0200, 0x0200, 0x0200,
     0x0200, 0x0200, 0x0400, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000},
    // * (42)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0800, 0x2A00, 0x1C00, 0x2A00,
     0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // + (43)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x3E00, 0x0800,
     0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // , (44)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0800, 0x0800, 0x1000, 0x0000, 0x0000, 0x0000},
    // - (45)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3E00, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // . (46)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000},
    // / (47)
    {0x0000, 0x0000, 0x0200, 0x0200, 0x0400, 0x0400, 0x0800, 0x0800,
     0x1000, 0x1000, 0x2000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 0 (48)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x3200, 0x2A00, 0x2600,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 1 (49)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x1800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 2 (50)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x0200, 0x0400, 0x0800,
     0x1000, 0x2000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 3 (51)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x0200, 0x0C00, 0x0200,
     0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 4 (52)
    {0x0000, 0x0000, 0x0000, 0x0400, 0x0C00, 0x1400, 0x2400, 0x4400,
     0x3E00, 0x0400, 0x0400, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 5 (53)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x2000, 0x3C00, 0x0200, 0x0200,
     0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 6 (54)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2000, 0x3C00, 0x2200, 0x2200,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 7 (55)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x0200, 0x0400, 0x0800, 0x0800,
     0x1000, 0x1000, 0x1000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 8 (56)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200, 0x1C00, 0x1C00,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // 9 (57)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200, 0x2200, 0x1E00,
     0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // : (58)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x0000, 0x0000,
     0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ; (59)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x0000, 0x0000,
     0x0800, 0x0800, 0x1000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // < (60)
    {0x0000, 0x0000, 0x0000, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000,
     0x1000, 0x0800, 0x0400, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000},
    // = (61)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3E00, 0x0000, 0x3E00,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // > (62)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080,
     0x0100, 0x0200, 0x0400, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000},
    // ? (63)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x0200, 0x0400, 0x0800,
     0x0800, 0x0000, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000},
    // @ (64)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2E00, 0x2A00, 0x2E00,
     0x2A00, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // A (65)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x1400, 0x2200, 0x2200, 0x2200,
     0x3E00, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // B (66)
    {0x0000, 0x0000, 0x0000, 0x3C00, 0x2200, 0x2200, 0x3C00, 0x2200,
     0x2200, 0x2200, 0x3C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // C (67)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2000, 0x2000, 0x2000,
     0x2000, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // D (68)
    {0x0000, 0x0000, 0x0000, 0x3800, 0x2400, 0x2200, 0x2200, 0x2200,
     0x2200, 0x2400, 0x3800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // E (69)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x2000, 0x2000, 0x3C00, 0x2000,
     0x2000, 0x2000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // F (70)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x2000, 0x2000, 0x3C00, 0x2000,
     0x2000, 0x2000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // G (71)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2000, 0x2000, 0x2E00,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // H (72)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200, 0x3E00, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // I (73)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // J (74)
    {0x0000, 0x0000, 0x0000, 0x0E00, 0x0400, 0x0400, 0x0400, 0x0400,
     0x2400, 0x2400, 0x1800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // K (75)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2400, 0x2800, 0x3000, 0x2800,
     0x2400, 0x2200, 0x2100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // L (76)
    {0x0000, 0x0000, 0x0000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000,
     0x2000, 0x2000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // M (77)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x3600, 0x2A00, 0x2A00, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // N (78)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x3200, 0x2A00, 0x2600, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // O (79)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200, 0x2200, 0x2200,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // P (80)
    {0x0000, 0x0000, 0x0000, 0x3C00, 0x2200, 0x2200, 0x2200, 0x3C00,
     0x2000, 0x2000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // Q (81)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200, 0x2200, 0x2200,
     0x2600, 0x2200, 0x1D00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // R (82)
    {0x0000, 0x0000, 0x0000, 0x3C00, 0x2200, 0x2200, 0x2200, 0x3C00,
     0x2400, 0x2200, 0x2100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // S (83)
    {0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2000, 0x1C00, 0x0200,
     0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // T (84)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // U (85)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // V (86)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200, 0x2200, 0x2200,
     0x2200, 0x1400, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // W (87)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200, 0x2200, 0x2A00,
     0x2A00, 0x3600, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // X (88)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x1400, 0x0800, 0x0800,
     0x1400, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // Y (89)
    {0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200, 0x1400, 0x0800,
     0x0800, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // Z (90)
    {0x0000, 0x0000, 0x0000, 0x3E00, 0x0200, 0x0400, 0x0800, 0x1000,
     0x2000, 0x2000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // [ (91)
    {0x0000, 0x0000, 0x0E00, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // \ (92)
    {0x0000, 0x0000, 0x2000, 0x2000, 0x1000, 0x1000, 0x0800, 0x0800,
     0x0400, 0x0400, 0x0200, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000},
    // ] (93)
    {0x0000, 0x0000, 0x0E00, 0x0200, 0x0200, 0x0200, 0x0200, 0x0200,
     0x0200, 0x0200, 0x0E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ^ (94)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x1400, 0x2200, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // _ (95)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000},
    // ` (96)
    {0x0000, 0x0000, 0x0000, 0x1000, 0x0800, 0x0400, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // a (97)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C00, 0x0200, 0x1E00,
     0x2200, 0x2200, 0x1E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // b (98)
    {0x0000, 0x0000, 0x2000, 0x2000, 0x2C00, 0x3200, 0x2200, 0x2200,
     0x2200, 0x3200, 0x2C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // c (99)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2000,
     0x2000, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // d (100)
    {0x0000, 0x0000, 0x0200, 0x0200, 0x1A00, 0x2600, 0x2200, 0x2200,
     0x2200, 0x2600, 0x1A00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // e (101)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200,
     0x3E00, 0x2000, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // f (102)
    {0x0000, 0x0000, 0x0600, 0x0800, 0x0800, 0x1C00, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // g (103)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1E00, 0x2200, 0x2200,
     0x2200, 0x1E00, 0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000},
    // h (104)
    {0x0000, 0x0000, 0x2000, 0x2000, 0x2C00, 0x3200, 0x2200, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // i (105)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x0000, 0x1800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // j (106)
    {0x0000, 0x0000, 0x0000, 0x0400, 0x0000, 0x0C00, 0x0400, 0x0400,
     0x0400, 0x0400, 0x2400, 0x1800, 0x0000, 0x0000, 0x0000, 0x0000},
    // k (107)
    {0x0000, 0x0000, 0x2000, 0x2000, 0x2400, 0x2800, 0x3000, 0x2800,
     0x2400, 0x2200, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // l (108)
    {0x0000, 0x0000, 0x1800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // m (109)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2C00, 0x3200, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // n (110)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2C00, 0x3200, 0x2200,
     0x2200, 0x2200, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // o (111)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C00, 0x2200, 0x2200,
     0x2200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // p (112)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2C00, 0x3200, 0x2200,
     0x2200, 0x3200, 0x2C00, 0x2000, 0x2000, 0x0000, 0x0000, 0x0000},
    // q (113)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1A00, 0x2600, 0x2200,
     0x2200, 0x2600, 0x1A00, 0x0200, 0x0200, 0x0000, 0x0000, 0x0000},
    // r (114)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2E00, 0x3200, 0x2000,
     0x2000, 0x2000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // s (115)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1E00, 0x2000, 0x1C00,
     0x0200, 0x0200, 0x3C00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // t (116)
    {0x0000, 0x0000, 0x0000, 0x0800, 0x0800, 0x1C00, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0600, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // u (117)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200,
     0x2200, 0x2600, 0x1A00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // v (118)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200,
     0x2200, 0x1400, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // w (119)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200,
     0x2A00, 0x2A00, 0x3600, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // x (120)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2200, 0x1400, 0x0800,
     0x0800, 0x1400, 0x2200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // y (121)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x2200, 0x2200, 0x2200,
     0x2200, 0x1E00, 0x0200, 0x2200, 0x1C00, 0x0000, 0x0000, 0x0000},
    // z (122)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3E00, 0x0400, 0x0800,
     0x1000, 0x2000, 0x3E00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // { (123)
    {0x0000, 0x0000, 0x0200, 0x0400, 0x0400, 0x0800, 0x1000, 0x0800,
     0x0400, 0x0400, 0x0200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // | (124)
    {0x0000, 0x0000, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800, 0x0800,
     0x0800, 0x0800, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // } (125)
    {0x0000, 0x0000, 0x0800, 0x0400, 0x0400, 0x0200, 0x0100, 0x0200,
     0x0400, 0x0400, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // ~ (126)
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1C00, 0x2200,
     0x0100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000}
};

static void crash_draw_char(int x, int y, char c, uint32_t color) {
    // Get framebuffer directly (bypass graphics subsystem)
    framebuffer_t* fb = NULL;
    if (graphics_map_framebuffer(&fb) != GRAPHICS_SUCCESS || !fb) {
        return;
    }

    int char_index = c - 32;
    if (char_index < 0 || char_index >= 96) {
        char_index = 0; // Use space for invalid chars
    }

    volatile uint8_t* framebuffer = (volatile uint8_t*)fb->virtual_addr;
    uint32_t bytes_per_pixel = (fb->bpp + 7) / 8;

    for (int cy = 0; cy < 16; cy++) {
        uint16_t row_bits = crash_font[char_index][cy];
        for (int cx = 0; cx < 8; cx++) {
            // Check if the bit is set in the font bitmap
            if (row_bits & (0x80 >> cx)) {
                int fb_x = x + cx;
                int fb_y = y + cy;

                if (fb_x >= 0 && fb_x < (int)fb->width && fb_y >= 0 && fb_y < (int)fb->height) {
                    size_t offset = (fb_y * fb->pitch) + (fb_x * bytes_per_pixel);
                    if (offset + bytes_per_pixel <= fb->size) {
                        // Write pixel based on bpp (handles 24bpp and 32bpp)
                        framebuffer[offset] = color & 0xFF;           // Blue
                        framebuffer[offset + 1] = (color >> 8) & 0xFF;  // Green
                        framebuffer[offset + 2] = (color >> 16) & 0xFF; // Red
                        if (bytes_per_pixel == 4) {
                            framebuffer[offset + 3] = (color >> 24) & 0xFF; // Alpha
                        }
                    }
                }
            }
        }
    }

    graphics_unmap_framebuffer(fb);
}

static void crash_draw_string(int x, int y, const char* str, uint32_t color) {
    if (!str) return;

    int current_x = x;
    while (*str) {
        crash_draw_char(current_x, y, *str, color);
        current_x += 8;
        str++;
    }
}

static void crash_clear_screen(uint32_t color) {
    framebuffer_t* fb = NULL;
    if (graphics_map_framebuffer(&fb) != GRAPHICS_SUCCESS || !fb) {
        return;
    }

    volatile uint8_t* framebuffer = (volatile uint8_t*)fb->virtual_addr;
    uint32_t bytes_per_pixel = (fb->bpp + 7) / 8;

    for (uint32_t y = 0; y < fb->height; y++) {
        volatile uint8_t* row = framebuffer + y * fb->pitch;
        for (uint32_t x = 0; x < fb->width; x++) {
            uint32_t offset = x * bytes_per_pixel;
            row[offset] = color & 0xFF;              // Blue
            row[offset + 1] = (color >> 8) & 0xFF;   // Green
            row[offset + 2] = (color >> 16) & 0xFF;  // Red
            if (bytes_per_pixel == 4) {
                row[offset + 3] = (color >> 24) & 0xFF; // Alpha
            }
        }
    }

    graphics_unmap_framebuffer(fb);
}

static void crash_draw_hex(int x, int y, uint64_t value, int digits, uint32_t color) {
    char buffer[32];
    int len = 0;

    // Convert to hex string
    for (int i = digits - 1; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        buffer[len++] = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buffer[len] = '\0';

    crash_draw_string(x, y, buffer, color);
}

// Public crash screen API
void tty_show_crash_screen(const char* title, const char* message, uint64_t eip, uint64_t error_code, uint64_t cr2) {
    // Clear screen with blue background
    crash_clear_screen(0xFF000080); // Blue background

    // Draw title in white
    crash_draw_string(10, 10, title, 0xFFFFFFFF);

    // Draw message in yellow
    crash_draw_string(10, 30, message, 0xFFFFFF00);

    // Draw register info
    crash_draw_string(10, 60, "EIP: 0x", 0xFFFFFFFF);
    crash_draw_hex(60, 60, eip, 16, 0xFFFF0000);

    crash_draw_string(10, 80, "Error Code: 0x", 0xFFFFFFFF);
    crash_draw_hex(120, 80, error_code, 8, 0xFFFF0000);

    crash_draw_string(10, 100, "CR2: 0x", 0xFFFFFFFF);
    crash_draw_hex(60, 100, cr2, 16, 0xFFFF0000);
}

bool tty_get_dimensions(uint16_t* cols, uint16_t* rows) {
    if (!tty_state.initialized || !tty_state.cells) {
        return false;
    }

    if (cols) {
        *cols = tty_state.cols;
    }
    if (rows) {
        *rows = tty_state.rows;
    }
    return true;
}

bool tty_get_cell_metrics(uint16_t* char_width, uint16_t* char_height) {
    if (!graphics_is_initialized()) {
        return false;
    }

    uint16_t cw = 8;
    uint16_t ch = 16;
    tty_font_t* tty_font = NULL;

    if (tty_font_load_builtin("tty-8x8", 8, &tty_font) == TTY_FONT_SUCCESS && tty_font) {
        cw = tty_font->width;
        ch = tty_font->height;
    }

    if (char_width) {
        *char_width = cw;
    }
    if (char_height) {
        *char_height = ch;
    }
    return true;
}

bool tty_get_cell(uint16_t x, uint16_t y, char* ch, uint8_t* attr) {
    if (!tty_state.initialized || !tty_state.cells) {
        return false;
    }
    if (x >= tty_state.cols || y >= tty_state.rows) {
        return false;
    }

    tty_cell_t cell = tty_state.cells[tty_cell_index(x, y)];
    if (ch) {
        *ch = cell.ch;
    }
    if (attr) {
        *attr = cell.attr;
    }
    return true;
}

void tty_redraw_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    if (!tty_state.cells || width == 0 || height == 0) {
        return;
    }

    uint16_t max_x = x + width;
    uint16_t max_y = y + height;
    if (max_x > tty_state.cols) {
        max_x = tty_state.cols;
    }
    if (max_y > tty_state.rows) {
        max_y = tty_state.rows;
    }

    for (uint16_t row = y; row < max_y; row++) {
        for (uint16_t col = x; col < max_x; col++) {
            tty_cell_t cell = tty_state.cells[tty_cell_index(col, row)];
            tty_render_cell(col, row, cell.ch, cell.attr);
        }
    }

    tty_apply_cursor();
}

// =============================================================================
// VIRTUAL TERMINAL (VT) MANAGEMENT
// =============================================================================

bool tty_init_vt_buffers(void) {
    if (g_vt_buffers_initialized) {
        return true;
    }

    uint16_t cols = tty_state.cols;
    uint16_t rows = tty_state.rows;

    if (cols == 0 || rows == 0) {
        debuglog(DEBUG_ERROR, "TTY: Cannot init VT buffers - invalid dimensions %ux%u\n", cols, rows);
        return false;
    }

    size_t cell_count = (size_t)cols * rows;

    // Initialize VT buffers for TTY VTs (3-12)
    for (int i = TTY_FIRST_TTY_VT - 1; i < TTY_LAST_TTY_VT; i++) {
        vt_buffer_t* vt = &g_vt_buffers[i];
        
        vt->cells = (tty_cell_t*)kzalloc(cell_count * sizeof(tty_cell_t));
        if (!vt->cells) {
            debuglog(DEBUG_ERROR, "TTY: Failed to allocate buffer for VT %d\n", i + 1);
            // Clean up already allocated buffers
            for (int j = TTY_FIRST_TTY_VT - 1; j < i; j++) {
                if (g_vt_buffers[j].cells) {
                    kfree(g_vt_buffers[j].cells);
                    g_vt_buffers[j].cells = NULL;
                }
            }
            return false;
        }

        vt->cell_count = cell_count;
        vt->cols = cols;
        vt->rows = rows;
        vt->cursor_x = 0;
        vt->cursor_y = 0;
        vt->saved_cursor_x = 0;
        vt->saved_cursor_y = 0;
        vt->fg = TEXT_ATTR_LIGHT_GRAY;
        vt->bg = TEXT_ATTR_BLACK;
        vt->bold = false;
        vt->faint = false;
        vt->underline = false;
        vt->blink = false;
        vt->inverse = false;
        vt->conceal = false;
        vt->italic = false;
        vt->strike = false;
        vt->double_underline = false;
        vt->overlined = false;
        vt->framed = false;
        vt->encircled = false;
        vt->crossed_out = false;
        vt->true_fg = (graphics_color_t){170, 170, 170, 255};
        vt->true_bg = (graphics_color_t){0, 0, 0, 255};
        vt->use_true_colors = false;
        vt->initialized = true;

        // Initialize cells to spaces
        uint8_t attr = (vt->bg << 4) | (vt->fg & 0x0F);
        for (size_t j = 0; j < cell_count; j++) {
            vt->cells[j].ch = ' ';
            vt->cells[j].attr = attr;
            vt->cells[j].dirty = 1;
        }

        debuglog(DEBUG_INFO, "TTY: Initialized VT %d buffer (%zu bytes)\n", i + 1, cell_count * sizeof(tty_cell_t));
    }

    g_vt_buffers_initialized = true;
    g_current_vt = 1;  // Start at VT1 (graphical)
    debuglog(DEBUG_INFO, "TTY: Virtual terminal buffers initialized for VTs %d-%d\n", 
             TTY_FIRST_TTY_VT, TTY_LAST_TTY_VT);
    return true;
}

static void tty_save_current_vt_buffer(void) {
    if (!tty_state.cells || !g_vt_buffers_initialized) {
        return;
    }

    // Only save if current VT is a TTY VT (3-12)
    if (g_current_vt < TTY_FIRST_TTY_VT || g_current_vt > TTY_LAST_TTY_VT) {
        return;
    }

    vt_buffer_t* vt = &g_vt_buffers[g_current_vt - 1];
    if (!vt->cells || vt->cell_count != tty_state.cell_count) {
        return;
    }

    // Copy current TTY state to VT buffer
    memcpy(vt->cells, tty_state.cells, tty_state.cell_count * sizeof(tty_cell_t));
    vt->cursor_x = tty_state.cursor_x;
    vt->cursor_y = tty_state.cursor_y;
    vt->saved_cursor_x = tty_state.saved_cursor_x;
    vt->saved_cursor_y = tty_state.saved_cursor_y;
    vt->fg = tty_state.fg;
    vt->bg = tty_state.bg;
    vt->bold = tty_state.bold;
    vt->faint = tty_state.faint;
    vt->underline = tty_state.underline;
    vt->blink = tty_state.blink;
    vt->inverse = tty_state.inverse;
    vt->conceal = tty_state.conceal;
    vt->italic = tty_state.italic;
    vt->strike = tty_state.strike;
    vt->double_underline = tty_state.double_underline;
    vt->overlined = tty_state.overlined;
    vt->framed = tty_state.framed;
    vt->encircled = tty_state.encircled;
    vt->crossed_out = tty_state.crossed_out;
    vt->true_fg = tty_state.true_fg;
    vt->true_bg = tty_state.true_bg;
    vt->use_true_colors = tty_state.use_true_colors;
}

static void tty_restore_vt_buffer(uint8_t vt_num) {
    if (!g_vt_buffers_initialized) {
        return;
    }

    if (vt_num < TTY_FIRST_TTY_VT || vt_num > TTY_LAST_TTY_VT) {
        return;
    }

    vt_buffer_t* vt = &g_vt_buffers[vt_num - 1];
    if (!vt->cells || !tty_state.cells || vt->cell_count != tty_state.cell_count) {
        return;
    }

    // Restore VT buffer to current TTY state
    memcpy(tty_state.cells, vt->cells, tty_state.cell_count * sizeof(tty_cell_t));
    tty_state.cursor_x = vt->cursor_x;
    tty_state.cursor_y = vt->cursor_y;
    tty_state.saved_cursor_x = vt->saved_cursor_x;
    tty_state.saved_cursor_y = vt->saved_cursor_y;
    tty_state.fg = vt->fg;
    tty_state.bg = vt->bg;
    tty_state.bold = vt->bold;
    tty_state.faint = vt->faint;
    tty_state.underline = vt->underline;
    tty_state.blink = vt->blink;
    tty_state.inverse = vt->inverse;
    tty_state.conceal = vt->conceal;
    tty_state.italic = vt->italic;
    tty_state.strike = vt->strike;
    tty_state.double_underline = vt->double_underline;
    tty_state.overlined = vt->overlined;
    tty_state.framed = vt->framed;
    tty_state.encircled = vt->encircled;
    tty_state.crossed_out = vt->crossed_out;
    tty_state.true_fg = vt->true_fg;
    tty_state.true_bg = vt->true_bg;
    tty_state.use_true_colors = vt->use_true_colors;
}

bool tty_switch_vt(uint8_t vt_number) {
    if (vt_number < 1 || vt_number > TTY_VT_COUNT) {
        debuglog(DEBUG_ERROR, "TTY: Invalid VT number %u (valid: 1-%d)\n", vt_number, TTY_VT_COUNT);
        return false;
    }

    // Save current VT buffer if it's a TTY VT
    tty_save_current_vt_buffer();

    // Update current VT
    uint8_t old_vt = g_current_vt;
    g_current_vt = vt_number;

    debuglog(DEBUG_INFO, "TTY: Switching from VT %u to VT %u\n", old_vt, vt_number);

    // If switching to a TTY VT, restore its buffer and bypass double buffering
    if (vt_number >= TTY_FIRST_TTY_VT && vt_number <= TTY_LAST_TTY_VT) {
        // Disable double buffering for direct framebuffer access
        // This ensures TTY writes go directly to the screen
        graphics_enable_double_buffering(false);
        
        // Signal that graphics apps should pause their display output
        // but continue running in the background
        tty_set_graphics_app_active(false);
        
        tty_restore_vt_buffer(vt_number);
        tty_force_redraw();
        return true;
    }

    // VT 1-2 are graphical - handled by display manager
    // Re-enable double buffering for graphical mode
    graphics_enable_double_buffering(true);
    
    return true;
}

uint8_t tty_get_current_vt(void) {
    return g_current_vt;
}

bool tty_is_active(void) {
    // TTY is active when current VT is a TTY VT (not graphical)
    return (g_current_vt >= TTY_FIRST_TTY_VT && g_current_vt <= TTY_LAST_TTY_VT);
}
