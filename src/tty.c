#include "include/tty.h"

#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"
#include "include/debuglog.h"
#include "include/libc/stdio.h"
#include "include/string.h"
#include "include/memory.h"
#include <string.h>

typedef enum {
    TTY_BACKEND_FRAMEBUFFER = 0
} tty_backend_t;

typedef struct {
    char ch;
    uint8_t attr;
} tty_cell_t;

static struct {
    tty_backend_t backend;
    uint16_t cols;
    uint16_t rows;
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
    tty_cell_t* cells;
    size_t cell_count;
} tty_state = {
    .backend = TTY_BACKEND_FRAMEBUFFER,
    .cols = 80,
    .rows = 25,
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

    // Always derive terminal dimensions from framebuffer mode using font metrics
    uint16_t char_w = 8;
    uint16_t char_h = 8;  // Fixed: Changed from 16 to 8 to match 8x8 font
    font_t* sys_font = NULL;
    if (font_get_system_font(&sys_font) == GRAPHICS_SUCCESS && sys_font) {
        if (sys_font->fixed_width > 0) {
            char_w = sys_font->fixed_width;
        }
        if (sys_font->metrics.height > 0) {
            char_h = sys_font->metrics.height;
        }
    }

    if (char_w == 0 || char_h == 0) {
        return; // Avoid division by zero
    }

    uint16_t cols = (uint16_t)(mode.width / char_w);
    uint16_t rows = (uint16_t)(mode.height / char_h);
    
    // Sanity check for reasonable terminal dimensions
    if (cols > 0 && rows > 0 && cols <= 200 && rows <= 200) {
        tty_state.cols = cols;
        tty_state.rows = rows;
    }
}

static void tty_apply_cursor(void) {
    graphics_set_cursor_pos(tty_state.cursor_x, tty_state.cursor_y);
}

static inline size_t tty_cell_index(uint16_t x, uint16_t y) {
    return (size_t)y * (size_t)tty_state.cols + x;
}

static void tty_render_cell_framebuffer(uint16_t x, uint16_t y, char ch, uint8_t attr) {
    // Direct framebuffer rendering with enhanced attributes
    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }
    
    // Get font for character rendering
    font_t* sys_font = NULL;
    if (font_get_system_font(&sys_font) != GRAPHICS_SUCCESS || !sys_font) {
        return;
    }
    
    uint16_t char_width = sys_font->fixed_width > 0 ? sys_font->fixed_width : 8;
    uint16_t char_height = sys_font->metrics.height > 0 ? sys_font->metrics.height : 16;
    
    int32_t px = (int32_t)x * (int32_t)char_width;
    int32_t py = (int32_t)y * (int32_t)char_height;
    
    // Enhanced bounds checking to prevent corruption
    if (px < 0 || py < 0 || 
        px + char_width > (int32_t)fb->width || 
        py + char_height > (int32_t)fb->height ||
        px >= (int32_t)fb->width || py >= (int32_t)fb->height) {
        debuglog(DEBUG_WARN, "TTY: Character at (%u,%u) would render outside framebuffer bounds (%ux%u)\n",
                x, y, fb->width, fb->height);
        return;
    }
    
    // Determine colors
    graphics_color_t fg_color = tty_state.use_true_colors ? tty_state.true_fg : tty_color_from_nibble(attr & 0x0F);
    graphics_color_t bg_color = tty_state.use_true_colors ? tty_state.true_bg : tty_color_from_nibble((attr >> 4) & 0x0F);
    
    // Apply text attributes to colors
    if (tty_state.bold) {
        fg_color.r = (uint8_t)(fg_color.r * 1.2 > 255 ? 255 : fg_color.r * 1.2);
        fg_color.g = (uint8_t)(fg_color.g * 1.2 > 255 ? 255 : fg_color.g * 1.2);
        fg_color.b = (uint8_t)(fg_color.b * 1.2 > 255 ? 255 : fg_color.b * 1.2);
    }
    if (tty_state.faint) {
        fg_color.r = (uint8_t)(fg_color.r * 0.5);
        fg_color.g = (uint8_t)(fg_color.g * 0.5);
        fg_color.b = (uint8_t)(fg_color.b * 0.5);
    }
    if (tty_state.inverse) {
        graphics_color_t temp = fg_color;
        fg_color = bg_color;
        bg_color = temp;
    }
    if (tty_state.conceal) {
        fg_color = bg_color;
    }
    
    // Create graphics surface for the framebuffer
    graphics_surface_t surface;
    surface.pixels = fb->virtual_addr;
    surface.width = fb->width;
    surface.height = fb->height;
    surface.pitch = fb->pitch;
    surface.format = fb->format;
    surface.bpp = fb->bpp;
    
    // Render character with enhanced style
    text_style_t style = {
        .foreground = fg_color,
        .background = bg_color,
        .has_background = true,
        .bold = tty_state.bold,
        .italic = tty_state.italic,
        .underline = tty_state.underline || tty_state.double_underline,
        .strikethrough = tty_state.strike,
        .shadow_offset = 0,
        .shadow_color = {0, 0, 0, 255}
    };
    
    font_render_char(sys_font, &surface, px, py, (uint32_t)ch, &style);
    
    // Add additional visual effects
    if (tty_state.double_underline) {
        // Draw double underline
        for (int i = 0; i < char_width; i++) {
            if (px + i < (int32_t)fb->width) {
                if (py + char_height - 2 < (int32_t)fb->height) {
                    graphics_draw_pixel(px + i, py + char_height - 2, fg_color);
                }
                if (py + char_height - 4 < (int32_t)fb->height) {
                    graphics_draw_pixel(px + i, py + char_height - 4, fg_color);
                }
            }
        }
    }
    if (tty_state.overlined) {
        // Draw overline
        for (int i = 0; i < char_width; i++) {
            if (px + i < (int32_t)fb->width && py >= 0) {
                graphics_draw_pixel(px + i, py, fg_color);
            }
        }
    }
    if (tty_state.framed || tty_state.encircled) {
        // Draw frame/circle around character
        graphics_rect_t rect = {px, py, char_width, char_height};
        graphics_draw_rect(&rect, fg_color, false);
    }
}

static void tty_render_cell(uint16_t x, uint16_t y, char ch, uint8_t attr) {
    // Always use framebuffer rendering
    if (tty_state.use_true_colors && graphics_is_initialized()) {
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
    if (!tty_state.cells || tty_state.cols == 0 || tty_state.rows == 0) {
        return;
    }

    for (uint16_t y = 0; y < tty_state.rows; y++) {
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            tty_cell_t cell = tty_state.cells[tty_cell_index(x, y)];
            tty_render_cell(x, y, cell.ch, cell.attr);
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
        size_t line_size = (size_t)tty_state.cols * sizeof(tty_cell_t);
        memmove(tty_state.cells,
                tty_state.cells + tty_state.cols,
                line_size * (tty_state.rows - 1));

        for (uint16_t x = 0; x < tty_state.cols; x++) {
            size_t idx = tty_cell_index(x, tty_state.rows - 1);
            tty_state.cells[idx].ch = ' ';
            tty_state.cells[idx].attr = attr;
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
        return false;
    }

    // Try to set a graphics mode suitable for text rendering
    graphics_result_t result = graphics_set_mode(800, 600, 32, 60);
    if (result != GRAPHICS_SUCCESS) {
        // Fallback to VGA mode
        result = graphics_set_mode(640, 480, 32, 60);
    }
    
    if (result == GRAPHICS_SUCCESS) {
        tty_state.backend = TTY_BACKEND_FRAMEBUFFER;
        tty_update_dimensions_from_graphics();
        debuglog(DEBUG_INFO, "TTY: framebuffer console with enhanced ANSI support enabled\n");
    } else {
        debuglog(DEBUG_ERROR, "TTY: failed to initialize framebuffer console\n");
        return false;
    }

    if (!tty_set_dimensions(tty_state.cols, tty_state.rows)) {
        debuglog(DEBUG_ERROR, "TTY: failed to allocate screen buffer\n");
        return false;
    }
    
    tty_clear();
    tty_reset_ansi_parser();
    tty_state.initialized = true;
    return true;
}

void tty_clear(void) {
    tty_state.cursor_x = 0;
    tty_state.cursor_y = 0;

    uint8_t attr = tty_current_attr();

    if (tty_state.cells || tty_set_dimensions(tty_state.cols, tty_state.rows)) {
        for (size_t i = 0; i < tty_state.cell_count; i++) {
            tty_state.cells[i].ch = ' ';
            tty_state.cells[i].attr = attr;
        }
        tty_flush_screen();
        return;
    }

    // Always use graphics subsystem for clearing
    graphics_color_t bg = tty_color_from_nibble((attr >> 4) & 0x0F);
    graphics_clear_screen(bg);

    tty_apply_cursor();
}

void tty_putc(char c) {
    tty_process_ansi(c);
}

void tty_write(const char* text) {
    if (!text) return;
    while (*text) {
        tty_process_ansi(*text++);
    }
}

void tty_write_ansi(const char* text) {
    if (!text) return;
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
