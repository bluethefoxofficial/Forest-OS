#include "include/tty.h"

#include "include/screen.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"
#include "include/debuglog.h"
#include "include/libc/stdio.h"
#include "include/string.h"
#include "include/memory.h"

typedef enum {
    TTY_BACKEND_LEGACY = 0,
    TTY_BACKEND_GRAPHICS_TEXT
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
    bool cursor_visible;
    uint16_t saved_cursor_x;
    uint16_t saved_cursor_y;
    bool initialized;
    tty_cell_t* cells;
    size_t cell_count;
} tty_state = {
    .backend = TTY_BACKEND_LEGACY,
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
    ANSI_STATE_CSI
} ansi_state_t;

static struct {
    ansi_state_t state;
    int params[8];
    size_t param_count;
    bool param_in_progress;
    bool private_mode;
} ansi_parser = {
    .state = ANSI_STATE_NORMAL,
    .params = {0},
    .param_count = 0,
    .param_in_progress = false,
    .private_mode = false,
};

static const graphics_color_t tty_palette[16] = {
    {0, 0, 0, 255},         // Black
    {0, 0, 170, 255},       // Blue
    {0, 170, 0, 255},       // Green
    {0, 170, 170, 255},     // Cyan
    {170, 0, 0, 255},       // Red
    {170, 0, 170, 255},     // Magenta
    {170, 85, 0, 255},      // Brown/Yellowish
    {170, 170, 170, 255},   // Light gray
    {85, 85, 85, 255},      // Dark gray (bright black)
    {85, 85, 255, 255},     // Bright blue
    {85, 255, 85, 255},     // Bright green
    {85, 255, 255, 255},    // Bright cyan
    {255, 85, 85, 255},     // Bright red
    {255, 85, 255, 255},    // Bright magenta
    {255, 255, 85, 255},    // Yellow
    {255, 255, 255, 255},   // White
};

static graphics_color_t tty_color_from_nibble(uint8_t nibble) {
    return tty_palette[nibble & 0x0F];
}

static uint8_t tty_palette_best_match(uint8_t r, uint8_t g, uint8_t b, bool allow_bright) {
    uint32_t best_error = UINT32_MAX;
    uint8_t best_index = 7; // default to light gray
    uint8_t limit = allow_bright ? 16 : 8;

    for (uint8_t i = 0; i < limit; i++) {
        int32_t dr = (int32_t)r - (int32_t)tty_palette[i].r;
        int32_t dg = (int32_t)g - (int32_t)tty_palette[i].g;
        int32_t db = (int32_t)b - (int32_t)tty_palette[i].b;
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

    if (mode.is_text_mode) {
        tty_state.cols = (uint16_t)mode.width;
        tty_state.rows = (uint16_t)mode.height;
        return;
    }

    // Derive terminal columns/rows from the active pixel mode using the
    // system font metrics (fallback to classic 8x16 if unavailable).
    uint16_t char_w = 8;
    uint16_t char_h = 16;
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
    if (cols > 0 && rows > 0) {
        tty_state.cols = cols;
        tty_state.rows = rows;
    }
}

static void tty_apply_cursor(void) {
    if (tty_state.backend == TTY_BACKEND_GRAPHICS_TEXT) {
        graphics_set_cursor_pos(tty_state.cursor_x, tty_state.cursor_y);
    }
}

static inline size_t tty_cell_index(uint16_t x, uint16_t y) {
    return (size_t)y * (size_t)tty_state.cols + x;
}

static void tty_render_cell(uint16_t x, uint16_t y, char ch, uint8_t attr) {
    if (tty_state.backend == TTY_BACKEND_GRAPHICS_TEXT) {
        graphics_write_char(x, y, ch, attr);
    } else {
        int fg = attr & 0x0F;
        int bg = (attr >> 4) & 0x0F;
        tui_set_char_at((int)x, (int)y, ch, fg, bg);
    }
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
    } else if (tty_state.backend == TTY_BACKEND_GRAPHICS_TEXT) {
        graphics_scroll_screen(1);
        tty_state.cursor_y = tty_state.rows - 1;
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            graphics_write_char(x, tty_state.cursor_y, ' ', attr);
        }
    } else {
        int fg = attr & 0x0F;
        int bg = (attr >> 4) & 0x0F;
        set_screen_color(fg, bg);
        scrollUp(1);
        tty_state.cursor_y = tty_state.rows - 1;
        for (uint16_t x = 0; x < tty_state.cols; x++) {
            tui_set_char_at((int)x, (int)tty_state.cursor_y, ' ', fg, bg);
        }
    }
}

static void tty_handle_control(char c) {
    switch (c) {
        case '\n':
            tty_state.cursor_x = 0;
            tty_state.cursor_y++;
            tty_scroll_if_needed();
            break;
        case '\r':
            tty_state.cursor_x = 0;
            break;
        case '\b':
            if (tty_state.cursor_x > 0) {
                tty_state.cursor_x--;
                tty_backend_put(' ');
            }
            break;
        case '\t':
            tty_state.cursor_x = (uint16_t)((tty_state.cursor_x + 8) & ~(uint16_t)(8 - 1));
            if (tty_state.cursor_x >= tty_state.cols) {
                tty_state.cursor_x = 0;
                tty_state.cursor_y++;
                tty_scroll_if_needed();
            }
            break;
        default:
            tty_backend_put(c);
            tty_state.cursor_x++;
            if (tty_state.cursor_x >= tty_state.cols) {
                tty_state.cursor_x = 0;
                tty_state.cursor_y++;
                tty_scroll_if_needed();
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
    memset(ansi_parser.params, 0, sizeof(ansi_parser.params));
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
        tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
        tty_state.bold = false;
        tty_state.faint = false;
        tty_state.underline = false;
        tty_state.blink = false;
        tty_state.inverse = false;
        return;
    }

    for (size_t i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) {
            tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
            tty_state.bold = false;
            tty_state.faint = false;
            tty_state.underline = false;
            tty_state.blink = false;
            tty_state.inverse = false;
            tty_state.conceal = false;
            tty_state.italic = false;
            tty_state.strike = false;
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
        } else if (p == 5) {
            tty_state.blink = true;
        } else if (p == 7) {
            tty_state.inverse = true;
        } else if (p == 8) {
            tty_state.conceal = true;
        } else if (p == 9) {
            tty_state.strike = true;
        } else if (p == 22) {
            tty_state.bold = false;
            tty_state.faint = false;
        } else if (p == 23) {
            tty_state.italic = false;
        } else if (p == 24) {
            tty_state.underline = false;
        } else if (p == 25) {
            tty_state.blink = false;
        } else if (p == 27) {
            tty_state.inverse = false;
        } else if (p == 28) {
            tty_state.conceal = false;
        } else if (p == 29) {
            tty_state.strike = false;
        } else if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97)) {
            uint8_t nibble = tty_color_nibble_from_ansi(p);
            if (nibble != 0xFF) {
                tty_state.fg = nibble;
            }
        } else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
            uint8_t nibble = tty_color_nibble_from_ansi(p);
            if (nibble != 0xFF) {
                tty_state.bg = nibble & 0x07; // backgrounds limited to base palette
            }
        } else if (p == 39) {
            tty_state.fg = TEXT_ATTR_LIGHT_GRAY;
        } else if (p == 49) {
            tty_state.bg = TEXT_ATTR_BLACK;
        } else if (p == 38 || p == 48) {
            bool is_bg = (p == 48);
            if (i + 1 < count) {
                int mode = params[i + 1];
                if (mode == 5 && (i + 2) < count) {
                    uint8_t idx = (uint8_t)params[i + 2];
                    uint8_t mapped = tty_map_256_color(idx, is_bg);
                    if (is_bg) {
                        tty_state.bg = mapped & 0x07;
                    } else {
                        tty_state.fg = mapped;
                    }
                    i += 2;
                } else if (mode == 2 && (i + 4) < count) {
                    uint8_t r = (uint8_t)params[i + 2];
                    uint8_t g = (uint8_t)params[i + 3];
                    uint8_t b = (uint8_t)params[i + 4];
                    uint8_t mapped = tty_map_rgb_to_attr(r, g, b, is_bg);
                    if (is_bg) {
                        tty_state.bg = mapped & 0x07;
                    } else {
                        tty_state.fg = mapped;
                    }
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
            if (ansi_parser.params[i] == 25) {
                tty_state.cursor_visible = (command == 'h');
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

    if (command == 'A' || command == 'B' || command == 'C' || command == 'D') {
        int amount = (ansi_parser.param_count > 0 && ansi_parser.params[0] > 0) ? ansi_parser.params[0] : 1;
        switch (command) {
            case 'A':
                tty_state.cursor_y = (tty_state.cursor_y >= amount) ? (tty_state.cursor_y - amount) : 0;
                break;
            case 'B':
                tty_state.cursor_y = (tty_state.cursor_y + amount < tty_state.rows) ? (tty_state.cursor_y + amount) : (tty_state.rows - 1);
                break;
            case 'C':
                tty_state.cursor_x = (tty_state.cursor_x + amount < tty_state.cols) ? (tty_state.cursor_x + amount) : (tty_state.cols - 1);
                break;
            case 'D':
                tty_state.cursor_x = (tty_state.cursor_x >= amount) ? (tty_state.cursor_x - amount) : 0;
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
        // 0: cursor to end, 1: start to cursor, 2: entire screen
        int mode = (ansi_parser.param_count > 0) ? ansi_parser.params[0] : 0;
        if (mode == 2) {
            tty_clear();
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
            } else if (c == ';') {
                if (ansi_parser.param_in_progress) {
                    ansi_parser.param_count++;
                    ansi_parser.param_in_progress = false;
                } else {
                    // Empty parameter defaults to zero
                    ansi_parser.params[ansi_parser.param_count++] = 0;
                }
            } else {
                if (ansi_parser.param_in_progress || ansi_parser.param_count == 0) {
                    ansi_parser.param_count++;
                }
                tty_handle_csi_command(c);
                tty_reset_ansi_parser();
            }
            break;
    }
}

void tty_init(void) {
    // Always make sure the legacy console is ready so that we have a safe
    // fallback even if the graphics stack fails later on.
    if (!tty_state.initialized) {
        console_init();
        tty_state.cols = screen_width;
        tty_state.rows = screen_height;
    }

    // Try to activate graphics text mode if the subsystem is available
    if (graphics_is_initialized()) {
        if (graphics_set_text_mode(tty_state.cols, tty_state.rows) == GRAPHICS_SUCCESS) {
            tty_state.backend = TTY_BACKEND_GRAPHICS_TEXT;
            tty_update_dimensions_from_graphics();
        } else {
            debuglog(DEBUG_WARN, "TTY: graphics text mode unavailable, using legacy console\n");
            tty_state.backend = TTY_BACKEND_LEGACY;
        }
    } else {
        debuglog(DEBUG_INFO, "TTY: graphics subsystem not initialized, using legacy console\n");
        tty_state.backend = TTY_BACKEND_LEGACY;
    }

    tty_set_dimensions(tty_state.cols, tty_state.rows);
    tty_clear();
    tty_reset_ansi_parser();
    tty_state.initialized = true;
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

    if (tty_state.backend == TTY_BACKEND_GRAPHICS_TEXT) {
        graphics_color_t bg = tty_color_from_nibble((attr >> 4) & 0x0F);
        graphics_clear_screen(bg);
    } else {
        int fg = attr & 0x0F;
        int bg = (attr >> 4) & 0x0F;
        for (uint16_t y = 0; y < tty_state.rows; y++) {
            for (uint16_t x = 0; x < tty_state.cols; x++) {
                tui_set_char_at((int)x, (int)y, ' ', fg, bg);
            }
        }
    }

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
    if (tty_state.backend == TTY_BACKEND_LEGACY) {
        set_screen_color(tty_state.fg, tty_state.bg);
    }
}

uint8_t tty_get_attr(void) {
    return tty_current_attr();
}

bool tty_uses_graphics_backend(void) {
    return tty_state.backend == TTY_BACKEND_GRAPHICS_TEXT;
}

bool tty_try_enable_graphics_backend(void) {
    if (!graphics_is_initialized()) {
        return false;
    }

    // First try a native text mode; if the driver does not support it, fall
    // back to a well-known graphics resolution and let the font renderer draw
    // glyphs into the framebuffer.
    graphics_result_t result = graphics_set_text_mode(tty_state.cols, tty_state.rows);
    if (result != GRAPHICS_SUCCESS) {
        uint32_t fallback_width = 800;
        uint32_t fallback_height = 600;
        uint32_t fallback_bpp = 32;
        result = graphics_set_mode(fallback_width, fallback_height, fallback_bpp, 60);
        if (result != GRAPHICS_SUCCESS) {
            // As a final attempt, choose a more conservative VGA-like mode.
            result = graphics_set_mode(640, 480, 32, 60);
        }
    }

    if (result != GRAPHICS_SUCCESS) {
        return false;
    }

    tty_state.backend = TTY_BACKEND_GRAPHICS_TEXT;
    tty_update_dimensions_from_graphics();
    tty_set_dimensions(tty_state.cols, tty_state.rows);
    tty_flush_screen();
    return true;
}
