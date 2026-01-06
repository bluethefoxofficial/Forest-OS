#include "include/dks.h"
#include "include/shell_loader.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/kb.h"
#include "include/memory.h"
#include "include/panic.h"
#include "include/hardware.h"
#include "include/power.h"
#include "include/ramdisk.h"
#include "include/vfs.h"
#include "include/task.h"
#include "include/string.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/graphics_types.h"
#include "include/graphics/font_renderer.h"
#include "include/tty.h"
#include "include/bmp.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_mouse.h"

static int atoi(const char* s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        n = 10 * n + (*s++ - '0');
    }
    return neg ? -n : n;
}

// Simple trigonometric approximations
static double dks_sin(double angle_rad) {
    // Taylor series approximation: sin(x) ≈ x - x³/6 + x⁵/120
    double x = angle_rad;
    while (x > 3.14159) x -= 2 * 3.14159;
    while (x < -3.14159) x += 2 * 3.14159;
    double x2 = x * x;
    double x3 = x2 * x;
    double x5 = x3 * x2;
    return x - x3/6.0 + x5/120.0;
}

static double dks_cos(double angle_rad) {
    // cos(x) = sin(x + π/2)
    return dks_sin(angle_rad + 1.5708);
}

static char* strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* new_s = malloc(len);
    if (new_s == NULL) return NULL;
    return (char*)memcpy(new_s, s, len);
}

#define putchar printch

#define MAX_CWD_LEN 256
static char dks_cwd[MAX_CWD_LEN] = "";

#define HISTORY_SIZE 16
static char* history_buffer[HISTORY_SIZE];
static int history_index = 0;
static int history_count = 0;
static int history_nav_index = -1; // Current position in history navigation

#define DKS_INPUT_MAX 256
#define DKS_CLIPBOARD_MAX 1024

static volatile bool dks_paste_requested = false;
static char dks_clipboard[DKS_CLIPBOARD_MAX];
static size_t dks_clipboard_len = 0;

static uint16_t dks_tty_cols = 80;
static uint16_t dks_tty_rows = 25;
static uint16_t dks_cell_width = 8;
static uint16_t dks_cell_height = 16;
static bool dks_metrics_ready = false;

static bool dks_selection_active = false;
static bool dks_selection_has_range = false;
static uint16_t dks_sel_start_col = 0;
static uint16_t dks_sel_start_row = 0;
static uint16_t dks_sel_end_col = 0;
static uint16_t dks_sel_end_row = 0;

static bool dks_context_menu_visible = false;
static bool dks_context_menu_request = false;
static int32_t dks_context_menu_request_x = 0;
static int32_t dks_context_menu_request_y = 0;
static int32_t dks_context_menu_x = 0;
static int32_t dks_context_menu_y = 0;
static int32_t dks_context_menu_width = 0;
static int32_t dks_context_menu_height = 0;
static uint32_t dks_context_menu_item_count = 0;
static int32_t dks_context_menu_item_height = 0;
static uint32_t dks_context_menu_padding = 0;
static int32_t dks_context_menu_hover = -1;
static uint32_t dks_taskbar_height = 32;
static bool dks_start_menu_visible = false;
static int32_t dks_start_menu_hover = -1;
static int32_t dks_start_menu_width = 220;
static int32_t dks_start_menu_height = 200;
static graphics_rect_t dks_start_button_rect = {0, 0, 0, 0};
static int32_t dks_start_menu_item_height = 0;
static bool dks_context_menu_request_desktop = false;

static bool dks_left_button_down = false;
static bool dks_right_button_down = false;
static uint32_t dks_gfx_screen_width = 0;
static uint32_t dks_gfx_screen_height = 0;
static font_t* default_font = NULL; // Shared font handle
static bool dks_ensure_default_font(void);
#define DKS_MAX_WINDOWS 16
#define DKS_MAX_WIDGETS_PER_WINDOW 16
static void dks_draw_taskbar(void);

static void add_to_history(const char* command) {
    if (history_count > 0 && strcmp(command, history_buffer[(history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE]) == 0) {
        return; // Don't add duplicate consecutive commands
    }
    if (history_buffer[history_index]) {
        free(history_buffer[history_index]);
    }
    history_buffer[history_index] = strdup(command);
    history_index = (history_index + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

static void dks_print_history(void) {
    int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
    for (int i = 0; i < history_count; i++) {
        print_dec(i + 1);
        print("  ");
        print(history_buffer[(start + i) % HISTORY_SIZE]);
        print("\n");
    }
}


static void dks_print_help(void) {
    print("Commands:\n");
    print("  help, cat, cd, clear, cls, cpuid, echo, halt, head, history, kill, ls, mem\n");
    print("  panic, ps, pwd, reboot, run, shell, shutdown, sleep, tail, tui, wc, whoami\n");
    print("\nGraphics - Basic:\n");
    print("  pixel, rect, line, circle, triangle, draw_ellipse, draw_arc\n");
    print("  fill_screen, clear_area, mouse, draw_string, draw_window\n");
    print("  window_move, window_add_button, window_add_text, window_add_input, guimode\n");
    print("\nGraphics - Advanced:\n");
    print("  draw_polygon, draw_gradient, draw_rounded_rect, draw_image\n");
    print("  draw_bezier, draw_star, draw_thick_line, draw_dotted_line\n");
    print("  draw_crosshair, draw_grid, draw_checker\n");
    print("\nResolution:\n");
    print("  res_list         - List supported video modes\n");
    print("  get_resolution   - Show current resolution\n");
    print("  set_resolution   - Change resolution\n");
    print("\nKeyboard:\n");
    print("  kb_locale_change <US|GB> - Change keyboard layout\n");
    print("  kb_locale_list           - List available layouts\n");
    print("\nBuffering:\n");
    print("  swap_buffers, enable_double_buffering, wait_vsync\n");
    print("\nArrow Keys:\n");
    print("  Up/Down: Navigate command history\n");
    print("  Left/Right: Move cursor in input line\n");
}

static void dks_update_metrics_from_tty(void) {
    uint16_t cols = dks_tty_cols;
    uint16_t rows = dks_tty_rows;
    uint16_t cw = dks_cell_width;
    uint16_t ch = dks_cell_height;

    if (tty_get_dimensions(&cols, &rows)) {
        dks_tty_cols = cols;
        dks_tty_rows = rows;
    }
    if (tty_get_cell_metrics(&cw, &ch)) {
        dks_cell_width = cw;
        dks_cell_height = ch;
        dks_metrics_ready = true;
        return;
    }

    video_mode_t mode;
    if (graphics_get_current_mode(&mode) == GRAPHICS_SUCCESS) {
        if (cw == 0) {
            cw = 8;
        }
        if (ch == 0) {
            ch = 16;
        }
        dks_tty_cols = (mode.width > 0 && cw > 0) ? (uint16_t)(mode.width / cw) : dks_tty_cols;
        dks_tty_rows = (mode.height > 0 && ch > 0) ? (uint16_t)(mode.height / ch) : dks_tty_rows;
        dks_cell_width = cw;
        dks_cell_height = ch;
        dks_metrics_ready = true;
    }
}

static void dks_clamp_cell(uint16_t* col, uint16_t* row) {
    if (!col || !row) {
        return;
    }
    if (!dks_metrics_ready) {
        dks_update_metrics_from_tty();
    }
    if (dks_tty_cols == 0) {
        dks_tty_cols = 80;
    }
    if (dks_tty_rows == 0) {
        dks_tty_rows = 25;
    }
    if (*col >= dks_tty_cols) {
        *col = dks_tty_cols - 1;
    }
    if (*row >= dks_tty_rows) {
        *row = dks_tty_rows - 1;
    }
}

static void dks_pixel_to_cell(int32_t x, int32_t y, uint16_t* col_out, uint16_t* row_out) {
    if (!dks_metrics_ready) {
        dks_update_metrics_from_tty();
    }
    uint16_t col = 0;
    uint16_t row = 0;
    if (dks_cell_width > 0) {
        col = (uint16_t)(x / (int32_t)dks_cell_width);
    }
    if (dks_cell_height > 0) {
        row = (uint16_t)(y / (int32_t)dks_cell_height);
    }
    dks_clamp_cell(&col, &row);
    if (col_out) {
        *col_out = col;
    }
    if (row_out) {
        *row_out = row;
    }
}

static void dks_normalize_selection(uint16_t* sx, uint16_t* sy, uint16_t* ex, uint16_t* ey) {
    if (!sx || !sy || !ex || !ey) {
        return;
    }
    if (*sx > *ex) {
        uint16_t tmp = *sx;
        *sx = *ex;
        *ex = tmp;
    }
    if (*sy > *ey) {
        uint16_t tmp = *sy;
        *sy = *ey;
        *ey = tmp;
    }
}

static void dks_clear_selection_highlight(void) {
    if (!dks_selection_has_range || !tty_is_ready()) {
        return;
    }

    uint16_t sx = dks_sel_start_col;
    uint16_t sy = dks_sel_start_row;
    uint16_t ex = dks_sel_end_col;
    uint16_t ey = dks_sel_end_row;
    dks_normalize_selection(&sx, &sy, &ex, &ey);

    uint16_t width = (uint16_t)(ex - sx + 1);
    uint16_t height = (uint16_t)(ey - sy + 1);
    tty_redraw_region(sx, sy, width, height);
}

static void dks_reset_selection(void) {
    dks_clear_selection_highlight();
    dks_selection_active = false;
    dks_selection_has_range = false;
    dks_sel_start_col = 0;
    dks_sel_start_row = 0;
    dks_sel_end_col = 0;
    dks_sel_end_row = 0;
}

static void dks_highlight_cell(uint16_t col, uint16_t row) {
    if (!graphics_is_initialized() || !tty_is_ready()) {
        return;
    }

    if (!dks_metrics_ready) {
        dks_update_metrics_from_tty();
    }

    font_t* sys_font = NULL;
    if (font_get_system_font(&sys_font) != GRAPHICS_SUCCESS || !sys_font) {
        return;
    }

    char ch = ' ';
    uint8_t attr = MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    tty_get_cell(col, row, &ch, &attr);

    framebuffer_t* fb = graphics_get_framebuffer();
    if (!fb || !fb->virtual_addr) {
        return;
    }

    graphics_surface_t surface;
    surface.pixels = fb->virtual_addr;
    surface.width = fb->width;
    surface.height = fb->height;
    surface.pitch = fb->pitch;
    surface.format = fb->format;
    surface.bpp = fb->bpp;

    int32_t px = (int32_t)col * (int32_t)dks_cell_width;
    int32_t py = (int32_t)row * (int32_t)dks_cell_height;

    graphics_rect_t rect = {px, py, dks_cell_width, dks_cell_height};
    graphics_color_t bg = graphics_make_color(60, 110, 200, 255);
    graphics_color_t fg = COLOR_WHITE;
    graphics_draw_rect(&rect, bg, true);

    text_style_t style = {
        .foreground = fg,
        .background = bg,
        .has_background = false,
        .bold = true,
        .italic = false,
        .underline = false,
        .strikethrough = false,
        .shadow_offset = 0,
        .shadow_color = COLOR_BLACK
    };

    uint32_t codepoint = (uint32_t)(ch ? ch : ' ');
    font_render_char(sys_font, &surface, px, py, codepoint, &style);
}

static void dks_draw_selection(void) {
    if (!dks_selection_has_range) {
        return;
    }

    uint16_t sx = dks_sel_start_col;
    uint16_t sy = dks_sel_start_row;
    uint16_t ex = dks_sel_end_col;
    uint16_t ey = dks_sel_end_row;
    dks_normalize_selection(&sx, &sy, &ex, &ey);

    for (uint16_t row = sy; row <= ey; row++) {
        for (uint16_t col = sx; col <= ex; col++) {
            dks_highlight_cell(col, row);
            if (col == UINT16_MAX) {
                break;
            }
        }
        if (row == UINT16_MAX) {
            break;
        }
    }
}

static void dks_set_selection_anchor(uint16_t col, uint16_t row) {
    dks_reset_selection();
    dks_selection_active = true;
    dks_selection_has_range = true;
    dks_sel_start_col = col;
    dks_sel_start_row = row;
    dks_sel_end_col = col;
    dks_sel_end_row = row;
    dks_draw_selection();
}

static void dks_update_selection_endpoint(uint16_t col, uint16_t row) {
    if (!dks_selection_active) {
        return;
    }
    dks_clear_selection_highlight();
    dks_sel_end_col = col;
    dks_sel_end_row = row;
    dks_draw_selection();
}

static void dks_copy_selection_to_clipboard(void) {
    if (!dks_selection_has_range || !tty_is_ready()) {
        return;
    }

    if (!dks_metrics_ready) {
        dks_update_metrics_from_tty();
    }

    uint16_t sx = dks_sel_start_col;
    uint16_t sy = dks_sel_start_row;
    uint16_t ex = dks_sel_end_col;
    uint16_t ey = dks_sel_end_row;
    dks_normalize_selection(&sx, &sy, &ex, &ey);

    size_t out_idx = 0;
    char line_buffer[DKS_CLIPBOARD_MAX];

    for (uint16_t row = sy; row <= ey && out_idx < DKS_CLIPBOARD_MAX - 1; row++) {
        uint16_t row_end = ex;
        if (row_end >= dks_tty_cols && dks_tty_cols > 0) {
            row_end = (uint16_t)(dks_tty_cols - 1);
        }

        size_t line_len = 0;
        int last_non_space = -1;

        for (uint16_t col = sx; col <= row_end && line_len < sizeof(line_buffer) - 1; col++) {
            char ch;
            uint8_t attr;
            if (!tty_get_cell(col, row, &ch, &attr) || ch == '\0') {
                ch = ' ';
            }
            line_buffer[line_len++] = ch;
            if (ch != ' ') {
                last_non_space = (int)line_len - 1;
            }
            if (col == UINT16_MAX) {
                break;
            }
        }

        size_t copy_len = (last_non_space >= 0) ? (size_t)last_non_space + 1 : 0;
        if (copy_len > 0 && out_idx + copy_len < DKS_CLIPBOARD_MAX) {
            memcpy(dks_clipboard + out_idx, line_buffer, copy_len);
            out_idx += copy_len;
        }

        if (row != ey && out_idx < DKS_CLIPBOARD_MAX - 1) {
            dks_clipboard[out_idx++] = '\n';
        }
        if (row == UINT16_MAX) {
            break;
        }
    }

    dks_clipboard[out_idx] = '\0';
    dks_clipboard_len = out_idx;
    print("[DKS] Selection copied to clipboard.\n");
}

static const char* dks_context_menu_entries[] = {
    "Copy selection",
    "Paste",
    "Clear selection",
    "Cancel"
};
static const uint32_t DKS_CONTEXT_MENU_ENTRY_COUNT = sizeof(dks_context_menu_entries) / sizeof(dks_context_menu_entries[0]);

static const char* dks_start_menu_entries[] = {
    "Open Shell",
    "New Window",
    "Refresh Desktop",
    "Settings (stub)",
    "Shutdown (stub)"
};
static const uint32_t DKS_START_MENU_ENTRY_COUNT = sizeof(dks_start_menu_entries) / sizeof(dks_start_menu_entries[0]);

static void dks_draw_context_menu(font_t* font) {
    if (!dks_context_menu_visible || dks_context_menu_item_height <= 0 || !font) {
        return;
    }

    uint8_t shade = 70;
    graphics_color_t bg = graphics_make_color(shade, shade + 10, shade + 20, 235);
    graphics_rect_t rect = {dks_context_menu_x, dks_context_menu_y, (uint32_t)dks_context_menu_width, (uint32_t)dks_context_menu_height};
    graphics_draw_rect(&rect, bg, true);
    graphics_draw_rect(&rect, graphics_make_color(210, 220, 240, 255), false);

    for (uint32_t i = 0; i < dks_context_menu_item_count; i++) {
        int32_t item_y = dks_context_menu_y + (int32_t)dks_context_menu_padding + (int32_t)dks_context_menu_item_height * (int32_t)i;
        graphics_rect_t row_rect = {dks_context_menu_x + 1, item_y, (uint32_t)dks_context_menu_width - 2, (uint32_t)dks_context_menu_item_height};
        bool hovered = ((int32_t)i == dks_context_menu_hover);
        graphics_color_t row_bg = hovered ? graphics_make_color(110, 150, 220, 255)
                                          : graphics_make_color(shade + 15, shade + 20, shade + 30, 240);
        graphics_draw_rect(&row_rect, row_bg, true);

        int32_t text_y = item_y;
        graphics_draw_text(dks_context_menu_x + (int32_t)dks_context_menu_padding, text_y, dks_context_menu_entries[i], font, COLOR_WHITE);
    }
}

static void dks_close_context_menu(void) {
    if (!dks_context_menu_visible) {
        return;
    }

    if (tty_is_ready()) {
        if (!dks_metrics_ready) {
            dks_update_metrics_from_tty();
        }

        uint16_t start_col = 0, start_row = 0;
        uint16_t end_col = 0, end_row = 0;
        dks_pixel_to_cell(dks_context_menu_x, dks_context_menu_y, &start_col, &start_row);
        dks_pixel_to_cell(dks_context_menu_x + dks_context_menu_width, dks_context_menu_y + dks_context_menu_height, &end_col, &end_row);
        dks_normalize_selection(&start_col, &start_row, &end_col, &end_row);

        uint16_t width_cells = (uint16_t)(end_col - start_col + 1);
        uint16_t height_cells = (uint16_t)(end_row - start_row + 1);
        tty_redraw_region(start_col, start_row, width_cells, height_cells);
    }

    dks_context_menu_visible = false;
    dks_context_menu_item_height = 0;
    dks_context_menu_item_count = 0;
    dks_context_menu_hover = -1;

    if (dks_selection_has_range) {
        dks_draw_selection();
    }
}

static void dks_show_context_menu(int32_t px, int32_t py) {
    if (!graphics_is_initialized()) {
        return;
    }

    dks_close_context_menu();

    if (!dks_metrics_ready) {
        dks_update_metrics_from_tty();
    }

    font_t* sys_font = NULL;
    if (font_get_system_font(&sys_font) != GRAPHICS_SUCCESS || !sys_font) {
        // Fall back to default font if system font is unavailable
        if (!dks_ensure_default_font()) {
            return;
        }
        sys_font = default_font;
    }

    uint32_t max_text_width = 0;
    uint32_t max_text_height = dks_cell_height;

    for (uint32_t i = 0; i < DKS_CONTEXT_MENU_ENTRY_COUNT; i++) {
        uint32_t w = 0, h = 0;
        if (graphics_get_text_bounds(dks_context_menu_entries[i], sys_font, &w, &h) != GRAPHICS_SUCCESS) {
            w = (uint32_t)strlen(dks_context_menu_entries[i]) * dks_cell_width;
            h = dks_cell_height;
        }
        if (w > max_text_width) {
            max_text_width = w;
        }
        if (h > max_text_height) {
            max_text_height = h;
        }
    }

    uint32_t padding = 8;
    uint32_t item_height = max_text_height + 6;
    if (item_height == 0) {
        item_height = 16;
    }
    int32_t menu_w = (int32_t)(max_text_width + padding * 2);
    int32_t menu_h = (int32_t)(item_height * DKS_CONTEXT_MENU_ENTRY_COUNT + padding * 2);

    // Keep menu on-screen
    if (dks_gfx_screen_width == 0 || dks_gfx_screen_height == 0) {
        video_mode_t mode;
        if (graphics_get_current_mode(&mode) == GRAPHICS_SUCCESS) {
            dks_gfx_screen_width = mode.width;
            dks_gfx_screen_height = mode.height;
        }
    }
    if (dks_gfx_screen_width == 0 || dks_gfx_screen_height == 0) {
        return;
    }

    int32_t x = px;
    int32_t y = py;
    if (x + menu_w > (int32_t)dks_gfx_screen_width) {
        x = (int32_t)dks_gfx_screen_width - menu_w;
    }
    if (y + menu_h > (int32_t)dks_gfx_screen_height) {
        y = (int32_t)dks_gfx_screen_height - menu_h;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    dks_context_menu_x = x;
    dks_context_menu_y = y;
    dks_context_menu_width = menu_w;
    dks_context_menu_height = menu_h;
    dks_context_menu_item_count = DKS_CONTEXT_MENU_ENTRY_COUNT;
    dks_context_menu_item_height = (int32_t)item_height;
    dks_context_menu_padding = padding;

    dks_draw_context_menu(sys_font);
    dks_context_menu_visible = true;
    dks_context_menu_hover = -1;
}

static void dks_draw_taskbar(void) {
    if (dks_gfx_screen_width == 0 || dks_gfx_screen_height == 0) {
        return;
    }

    uint32_t bar_h = dks_taskbar_height;
    graphics_rect_t bar = {0, (int32_t)(dks_gfx_screen_height - bar_h), dks_gfx_screen_width, bar_h};
    graphics_draw_rect(&bar, graphics_make_color(30, 30, 30, 255), true);
    graphics_draw_rect(&bar, graphics_make_color(80, 80, 80, 255), false);

    graphics_rect_t start_btn = {8, (int32_t)(dks_gfx_screen_height - bar_h + 4), 88, bar_h - 8};
    dks_start_button_rect = start_btn;
    graphics_draw_rect(&start_btn, graphics_make_color(70, 130, 220, 255), true);
    graphics_draw_rect(&start_btn, graphics_make_color(220, 240, 255, 255), false);
    if (dks_ensure_default_font()) {
        graphics_draw_text(start_btn.x + 14, start_btn.y + 6, "Start", default_font, COLOR_WHITE);
    }

    // Fake status icons on the right side
    int32_t icon_y = (int32_t)(dks_gfx_screen_height - bar_h + 6);
    int32_t icon_x = (int32_t)dks_gfx_screen_width - 24;
    graphics_color_t icon_color = graphics_make_color(180, 200, 220, 255);
    for (int i = 0; i < 3; i++) {
        graphics_rect_t icon = {icon_x - (i * 18), icon_y, 12, 12};
        graphics_draw_rect(&icon, icon_color, true);
    }

    // Simple time placeholder
    if (dks_ensure_default_font()) {
        const char* time_str = "12:00";
        uint32_t tw = 0, th = 0;
        graphics_get_text_bounds(time_str, default_font, &tw, &th);
        graphics_draw_text((int32_t)dks_gfx_screen_width - (int32_t)tw - 70, icon_y, time_str, default_font, COLOR_WHITE);
    }
}

static void dks_draw_start_menu(font_t* font) {
    if (!dks_start_menu_visible || !font || dks_gfx_screen_width == 0 || dks_gfx_screen_height == 0) {
        return;
    }

    int32_t menu_w = dks_start_menu_width;
    int32_t menu_h = dks_start_menu_height;
    int32_t x = dks_start_button_rect.x;
    int32_t y = (int32_t)dks_gfx_screen_height - (int32_t)dks_taskbar_height - menu_h;
    if (y < 0) y = 0;

    graphics_rect_t panel = {x, y, (uint32_t)menu_w, (uint32_t)menu_h};
    graphics_draw_rect(&panel, graphics_make_color(32, 40, 56, 245), true);
    graphics_draw_rect(&panel, graphics_make_color(90, 120, 170, 255), false);

    uint32_t padding = 10;
    uint32_t item_h = 0;
    for (uint32_t i = 0; i < DKS_START_MENU_ENTRY_COUNT; i++) {
        uint32_t w = 0, h = 0;
        graphics_get_text_bounds(dks_start_menu_entries[i], font, &w, &h);
        if (h > item_h) {
            item_h = h + 6;
        }
    }
    if (item_h == 0) item_h = 18;
    dks_start_menu_item_height = (int32_t)item_h;

    for (uint32_t i = 0; i < DKS_START_MENU_ENTRY_COUNT; i++) {
        int32_t item_y = y + (int32_t)padding + (int32_t)(i * item_h);
        graphics_rect_t row = {x + 6, item_y, (uint32_t)menu_w - 12, item_h};
        bool hovered = ((int32_t)i == dks_start_menu_hover);
        graphics_color_t row_bg = hovered ? graphics_make_color(70, 110, 180, 255) : graphics_make_color(45, 55, 70, 255);
        graphics_draw_rect(&row, row_bg, true);
        graphics_draw_rect(&row, graphics_make_color(25, 25, 25, 80), false);
        graphics_draw_text(row.x + 10, row.y + 4, dks_start_menu_entries[i], font, COLOR_WHITE);
    }
}

static void dks_handle_context_menu_click(int32_t px, int32_t py) {
    if (!dks_context_menu_visible) {
        return;
    }

    if (px < dks_context_menu_x || py < dks_context_menu_y ||
        px > dks_context_menu_x + dks_context_menu_width ||
        py > dks_context_menu_y + dks_context_menu_height) {
        dks_close_context_menu();
        return;
    }

    if (dks_context_menu_item_height <= 0 || dks_context_menu_item_count == 0) {
        dks_close_context_menu();
        return;
    }

    int32_t local_y = py - (dks_context_menu_y + (int32_t)dks_context_menu_padding);
    if (local_y < 0) {
        dks_close_context_menu();
        return;
    }

    uint32_t item_index = (uint32_t)(local_y / dks_context_menu_item_height);
    if (item_index >= dks_context_menu_item_count) {
        dks_close_context_menu();
        return;
    }

    switch (item_index) {
        case 0:
            dks_copy_selection_to_clipboard();
            break;
        case 1:
            if (dks_clipboard_len > 0) {
                dks_paste_requested = true;
            } else {
                print("[DKS] Clipboard is empty.\n");
            }
            break;
        case 2:
            dks_reset_selection();
            break;
        default:
            break;
    }

    // Keep menu persistent unless user chooses Cancel
    if (item_index == 3) {
        dks_close_context_menu();
    } else {
        dks_context_menu_hover = -1;
        dks_draw_context_menu(default_font ? default_font : NULL);
    }
}


static void dks_dump_memory(void) {
    memory_stats_t stats = memory_get_stats();
    print("Total memory: ");
    print_dec(stats.total_memory_kb);
    print(" KB\n");
}

static void dks_tui_demo(void) {
    clearScreen();
    tui_draw_status_bar(0, "Forest OS - Enhanced TUI Demonstration", "Press any key to continue", 0x0F, 0x01);
    tui_draw_window(2, 2, 35, 8, "System Information", 0x0F, 0x02);
    tui_print_at(4, 4, "Forest OS v1.0", 0x0F, 0x02);
    readStr();
    clearScreen();
}

static void dks_show_cpuid(void) {
    const cpuid_info_t* info = hardware_get_cpuid_info();
    print("[CPUID] Hardware detection:\n");
    print("  Vendor: "); print(info->vendor_id); print("\n");
    print("  Brand:  "); print(info->brand_string); print("\n");
}



static const char* task_state_to_str(task_state_t state) {
    switch(state) {
        case TASK_STATE_RUNNING: return "RUNNING";
        case TASK_STATE_READY: return "READY";
        case TASK_STATE_WAITING: return "WAITING";
        case TASK_STATE_TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

static void dks_ps(void) {
    print("PID  State       Name\n");
    if (ready_queue_head) {
        task_t* current = ready_queue_head;
        do {
            print_dec(current->id);
            print("    ");
            print(task_state_to_str(current->state));
            print("     ");
            print(current->name);
            if (current == current_task) {
                print(" (current)");
            }
            print("\n");
            current = current->next;
        } while (current != ready_queue_head);
    }
}

static void dks_ls(char* path) {
    char target_path[MAX_CWD_LEN];
    if (path) {
        if (path[0] == '/') {
            strcpy(target_path, path + 1);
        } else {
            strcpy(target_path, dks_cwd);
            strcat(target_path, path);
        }
    } else {
        strcpy(target_path, dks_cwd);
    }

    print("Listing for /"); print(target_path); print(":\n");

    uint32 count = ramdisk_file_count();
    for (uint32 i = 0; i < count; i++) {
        const ramdisk_file_t* file = ramdisk_get(i);
        if (file) {
            const char* name = file->name;
            if (strlen(target_path) > 0 && strncmp(name, target_path, strlen(target_path)) != 0) {
                continue;
            }
            
            const char* subpath = name + strlen(target_path);
            
            char* next_slash = strchr(subpath, '/');
            if (!next_slash || (next_slash == subpath + strlen(subpath) - 1)) {
                print(subpath);
                 if (file->is_dir) {
                    print(" [DIR]");
                } else {
                    print(" (");
                    print_dec(file->size);
                    print(" bytes)");
                }
                print("\n");
            }
        }
    }
}

static void dks_cd(char* dir) {
    if (!dir || strlen(dir) == 0 || strcmp(dir, "/") == 0) {
        dks_cwd[0] = '\0';
        return;
    }

    char new_cwd[MAX_CWD_LEN];

    if (strcmp(dir, "..") == 0) {
        if (strlen(dks_cwd) > 0) {
            int i = strlen(dks_cwd) - 2;
            for (; i >= 0; i--) {
                if (dks_cwd[i] == '/') {
                    dks_cwd[i+1] = '\0';
                    return;
                }
            }
            dks_cwd[0] = '\0';
        }
        return;
    } else if (strcmp(dir, ".") == 0) {
        return;
    } else if (dir[0] == '/') {
        strcpy(new_cwd, dir + 1);
    } else {
        strcpy(new_cwd, dks_cwd);
        strcat(new_cwd, dir);
    }
    
    // Ensure path ends with a slash for consistent directory representation
    bool had_trailing_slash_in_input = (strlen(dir) > 0 && dir[strlen(dir)-1] == '/');
    if (strlen(new_cwd) > 0 && new_cwd[strlen(new_cwd)-1] != '/') {
        strcat(new_cwd, "/");
    }

    const ramdisk_file_t* found_file = ramdisk_find(new_cwd);
    char path_without_slash[MAX_CWD_LEN];

    // If not found with trailing slash, try without it
    if (!found_file) {
        if (strlen(new_cwd) > 0 && new_cwd[strlen(new_cwd)-1] == '/') {
            strncpy(path_without_slash, new_cwd, strlen(new_cwd) - 1);
            path_without_slash[strlen(new_cwd) - 1] = '\0';
            found_file = ramdisk_find(path_without_slash);
        }
    }

    // Validate that the found item is actually a directory
    if (found_file && found_file->is_dir) {
        // If the path was found without a trailing slash, ensure new_cwd has one for consistency
        if (!had_trailing_slash_in_input && new_cwd[strlen(new_cwd)-1] != '/') {
            strcat(new_cwd, "/");
        }
        strcpy(dks_cwd, new_cwd);
        return;
    }

    print("Directory not found: "); print(dir); print("\n");
}


char* resolve_path(const char* path) {
    static char fullpath[MAX_CWD_LEN];
    if (path[0] == '/') {
        strcpy(fullpath, path + 1);
    } else {
        strcpy(fullpath, dks_cwd);
        strcat(fullpath, path);
    }
    return fullpath;
}


static void dks_cat(char* args) {
    if (!args) { print("Usage: cat <filename>\n"); return; }
    const uint8* data;
    uint32 size;
    if (vfs_read_file(resolve_path(args), &data, &size)) {
        for (uint32 i = 0; i < size; i++) {
            putchar(data[i]);
        }
        print("\n");
    } else {
        print("File not found: "); print(args); print("\n");
    }
}

static void dks_wait(uint32 pid) {
    print("Waiting for PID "); print_dec(pid); print("...\n");
    while(task_exists(pid)) {
        // Yield CPU
        sleep_interruptible(10);
    }
    print("PID "); print_dec(pid); print(" terminated.\n");
}


static void dks_run_program(char* args) {
    if (!args) { print("Usage: run <path_to_executable> [&]\n"); return; }

    bool background = false;
    char* ampersand = strchr(args, '&');
    if (ampersand) {
        background = true;
        *ampersand = '\0'; // Remove the '&' from the path
        // Trim trailing spaces
        int len = strlen(args);
        while (len > 0 && args[len-1] == ' ') {
            args[--len] = '\0';
        }
    }

    const uint8* elf_data;
    uint32 elf_size;
    if (!vfs_read_file(resolve_path(args), &elf_data, &elf_size)) {
        print_colored("ERROR: ELF not found.\n", 0x0C, 0x00);
        return;
    }
    task_t* task = task_create_elf(elf_data, elf_size, args);
    if (!task) {
        print_colored("ERROR: Failed to create task.\n", 0x0C, 0x00);
        return;
    }
    print("Task created with ID: "); print(int_to_string(task->id)); print("\n");

    if (!background) {
        dks_wait(task->id);
    }
}

static void dks_pwd(void) {
    print(dks_cwd);
    print("\n");
}



static void dks_kill(char* arg) {
    if (!arg) { print("Usage: kill <pid>\n"); return; }
    uint32 pid = atoi(arg);
    if (pid == 0) { print("Invalid PID.\n"); return; }
    task_kill(pid);
    print("Sent SIGKILL to PID "); print_dec(pid); print("\n");
}

static void dks_head(char* arg) {
    if (!arg) { print("Usage: head <file>\n"); return; }
    const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }
    int lines = 0;
    for (uint32 i = 0; i < size && lines < 10; i++) {
        putchar(data[i]);
        if (data[i] == '\n') {
            lines++;
        }
    }
}

static void dks_tail(char* arg) {
    if (!arg) { print("Usage: tail <file>\n"); return; }
     const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }

    int lines = 0;
    int i = size -1;
    for (; i >= 0 && lines < 11; i--) {
        if (data[i] == '\n') {
            lines++;
        }
    }
    i++; 
    if(data[i] == '\n') i++;

    for(; (uint32)i < size; i++) {
        putchar(data[i]);
    }
}

static void dks_wc(char* arg) {
    if (!arg) { print("Usage: wc <file>\n"); return; }
    const uint8* data;
    uint32 size;
    if (!vfs_read_file(resolve_path(arg), &data, &size)) {
        print("File not found\n");
        return;
    }
    uint32 lines = 0, words = 0, chars = size;
    bool in_word = false;
    for (uint32 i = 0; i < size; i++) {
        if (data[i] == '\n') lines++;
        if (data[i] == ' ' || data[i] == '\n' || data[i] == '\t') {
            in_word = false;
        } else if (!in_word) {
            words++;
            in_word = true;
        }
    }
    print("Lines: "); print_dec(lines);
    print(" Words: "); print_dec(words);
    print(" Chars: "); print_dec(chars);
    print("\n");
}

static void dks_pixel(char* args) {
    if (!args) { print("Usage: pixel <x> <y> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x_str || !y_str || !r_str || !g_str || !b_str) {
        print("Usage: pixel <x> <y> <r> <g> <b>\n"); return;
    }
    int x = atoi(x_str);
    int y = atoi(y_str);
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    graphics_draw_pixel(x, y, graphics_make_color(r, g, b, 255));
}

static void dks_rect(char* args) {
    if (!args) { print("Usage: rect <x1> <y1> <x2> <y2> <r> <g> <b> [filled]\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    if (!x1_str || !y1_str || !x2_str || !y2_str || !r_str || !g_str || !b_str) {
        print("Usage: rect <x1> <y1> <x2> <y2> <r> <g> <b> [filled]\n"); return;
    }
    graphics_rect_t rect = {atoi(x1_str), atoi(y1_str), atoi(x2_str) - atoi(x1_str), atoi(y2_str) - atoi(y1_str)};
    graphics_color_t color = graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    graphics_draw_rect(&rect, color, filled);
}

static void dks_line(char* args) {
    if (!args) { print("Usage: line <x1> <y1> <x2> <y2> <r> <g> <b>\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x1_str || !y1_str || !x2_str || !y2_str || !r_str || !g_str || !b_str) {
        print("Usage: line <x1> <y1> <x2> <y2> <r> <g> <b>\n"); return;
    }
    graphics_draw_line(atoi(x1_str), atoi(y1_str), atoi(x2_str), atoi(y2_str), graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255));
}

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 16

#define CURSOR_MASK_TRANSPARENT 0
#define CURSOR_MASK_OUTLINE 1
#define CURSOR_MASK_FILL 2

// Arrow cursor mask (O=outline, F=fill, .=transparent)
static const uint8_t arrow_cursor_mask[CURSOR_WIDTH * CURSOR_HEIGHT] = {
    // Row 0:  O...........
    CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 1:  OO..........
    CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 2:  OFO.........
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 3:  OFFO........
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 4:  OFFFO.......
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 5:  OFFFFO......
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 6:  OFFFFFO.....
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 7:  OFFFFFFO....
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 8:  OFFFFFFFO...
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 9:  OFFFFFFFFO..
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 10: OFFFFF OOOOO.
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT,
    // Row 11: OFFO FFO.....
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 12: OFO.OFFO....
    CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 13: OO...OFFO...
    CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 14: O.....OFFO..
    CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_OUTLINE, CURSOR_MASK_FILL, CURSOR_MASK_FILL, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
    // Row 15: .......OOO..
    CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_OUTLINE, CURSOR_MASK_TRANSPARENT, CURSOR_MASK_TRANSPARENT,
};

static graphics_surface_t* mouse_cursor_surface = NULL; // Will be initialized once
static bool cursor_visible = false;
static int32_t cursor_x = 0, cursor_y = 0;
static graphics_color_t saved_background_pixels[CURSOR_WIDTH * CURSOR_HEIGHT]; // Buffer to save pixels under cursor
static bool background_saved = false;
static graphics_color_t cursor_outline_color = COLOR_BLACK;
static graphics_color_t cursor_fill_color = COLOR_WHITE;
static uint8_t cursor_button_mask = 0;
bool dks_guimode_active = false;
static bool dks_dragging_window = false;
static int dks_drag_window_id = -1;
static int32_t dks_drag_offset_x = 0;
static int32_t dks_drag_offset_y = 0;
static uint8_t dks_prev_button_mask = 0;

// Forward declare window type and helpers used before full definitions
typedef struct dks_window dks_window_t;
// Forward declarations for GUI helpers (used by mouse handler)
static dks_window_t* dks_find_window(int id);
static void dks_bring_to_front(dks_window_t* window);
static dks_window_t* dks_window_at_point(int32_t x, int32_t y);
static void dks_redraw_windows(void);
static void draw_software_cursor(int32_t x, int32_t y, bool show);

static bool dks_ensure_default_font(void) {
    if (default_font) {
        return true;
    }
    return graphics_load_font("default_8x16", 16, &default_font) == GRAPHICS_SUCCESS;
}

typedef enum {
    DKS_WIDGET_LABEL = 0,
    DKS_WIDGET_BUTTON,
    DKS_WIDGET_TEXT_INPUT
} dks_widget_type_t;

typedef struct {
    dks_widget_type_t type;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    char text[64];
} dks_widget_t;

struct dks_window {
    bool in_use;
    int id;
    int32_t z;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    graphics_color_t border_color;
    graphics_color_t fill_color;
    char title[64];
    dks_widget_t widgets[DKS_MAX_WIDGETS_PER_WINDOW];
    uint32_t widget_count;
};

static dks_window_t dks_windows[DKS_MAX_WINDOWS];
static int dks_next_window_id = 1;
static graphics_color_t dks_desktop_color = COLOR_DARK_GRAY;
static int32_t dks_next_window_z = 1;

static dks_window_t* dks_allocate_window(void);

static void dks_create_demo_window(void) {
    dks_window_t* window = dks_allocate_window();
    if (!window) {
        print("ERROR: Maximum window count reached.\n");
        return;
    }
    int base = (window->id % 5) * 20;
    window->x = 40 + base;
    window->y = 60 + base;
    window->width = 240;
    window->height = 140;
    window->border_color = graphics_make_color(90, 120, 200, 255);
    window->fill_color = graphics_make_color(25 + base, 35 + base, 55 + base, 220);
    strncpy(window->title, "Demo Window", sizeof(window->title) - 1);
    window->widget_count = 0;
    dks_redraw_windows();
}

static dks_window_t* dks_find_window(int id) {
    for (int i = 0; i < DKS_MAX_WINDOWS; i++) {
        if (dks_windows[i].in_use && dks_windows[i].id == id) {
            return &dks_windows[i];
        }
    }
    return NULL;
}

static dks_window_t* dks_allocate_window(void) {
    for (int i = 0; i < DKS_MAX_WINDOWS; i++) {
        if (!dks_windows[i].in_use) {
            memset(&dks_windows[i], 0, sizeof(dks_windows[i]));
            dks_windows[i].in_use = true;
            dks_windows[i].id = dks_next_window_id++;
            dks_windows[i].z = dks_next_window_z++;
            dks_windows[i].widget_count = 0;
            return &dks_windows[i];
        }
    }
    return NULL;
}

static void dks_bring_to_front(dks_window_t* window) {
    if (!window) {
        return;
    }
    window->z = dks_next_window_z++;
}

static void dks_render_widget(const dks_window_t* window, const dks_widget_t* widget) {
    if (!window || !widget) {
        return;
    }

    int32_t base_x = window->x + widget->x;
    int32_t base_y = window->y + widget->y;

    switch (widget->type) {
        case DKS_WIDGET_LABEL:
            if (dks_ensure_default_font()) {
                graphics_draw_text(base_x, base_y, widget->text, default_font, window->border_color);
            }
            break;
        case DKS_WIDGET_BUTTON: {
            graphics_rect_t rect = {base_x, base_y, widget->width, widget->height};
            graphics_color_t button_fill = graphics_make_color(90, 110, 180, 255);
            graphics_color_t button_border = graphics_make_color(40, 50, 90, 255);
            graphics_color_t text_color = graphics_make_color(240, 240, 240, 255);
            graphics_draw_rect(&rect, button_fill, true);
            graphics_draw_rect(&rect, button_border, false);

            if (dks_ensure_default_font()) {
                uint32_t text_w = 0, text_h = 0;
                graphics_get_text_bounds(widget->text, default_font, &text_w, &text_h);
                int32_t text_x = base_x + (int32_t)(widget->width / 2) - (int32_t)(text_w / 2);
                int32_t text_y = base_y + (int32_t)(widget->height / 2) - (int32_t)(text_h / 2);
                graphics_draw_text(text_x, text_y, widget->text, default_font, text_color);
            }
            break;
        }
        case DKS_WIDGET_TEXT_INPUT: {
            graphics_rect_t rect = {base_x, base_y, widget->width, widget->height};
            graphics_color_t input_fill = graphics_make_color(245, 245, 245, 255);
            graphics_color_t input_border = graphics_make_color(90, 90, 90, 255);
            graphics_color_t placeholder = graphics_make_color(120, 120, 120, 255);
            graphics_draw_rect(&rect, input_fill, true);
            graphics_draw_rect(&rect, input_border, false);

            if (dks_ensure_default_font() && widget->text[0] != '\0') {
                uint32_t text_w = 0, text_h = 0;
                graphics_get_text_bounds(widget->text, default_font, &text_w, &text_h);
                int32_t text_x = base_x + 4;
                int32_t text_y = base_y + (int32_t)(widget->height / 2) - (int32_t)(text_h / 2);
                graphics_draw_text(text_x, text_y, widget->text, default_font, placeholder);
            }
            break;
        }
        default:
            break;
    }
}

static void dks_render_window(const dks_window_t* window) {
    if (!window || !window->in_use) {
        return;
    }

    graphics_rect_t fill_rect = {window->x, window->y, window->width, window->height};
    graphics_draw_rect(&fill_rect, window->fill_color, true);
    graphics_draw_rect(&fill_rect, window->border_color, false);

    // Simple title bar
    uint32_t title_height = (window->height < 18) ? window->height : 18;
    graphics_rect_t title_bar = {window->x, window->y, window->width, title_height};
    graphics_draw_rect(&title_bar, window->border_color, true);

    if (dks_ensure_default_font()) {
        uint32_t text_width = 0, text_height = 0;
        graphics_get_text_bounds(window->title, default_font, &text_width, &text_height);
        int32_t text_x = window->x + (int32_t)(window->width / 2) - (int32_t)(text_width / 2);
        int32_t text_y = window->y + (int32_t)(title_height / 2) - (int32_t)(text_height / 2);
        graphics_draw_text(text_x, text_y, window->title, default_font, COLOR_WHITE);
    }

    for (uint32_t i = 0; i < window->widget_count; i++) {
        dks_render_widget(window, &window->widgets[i]);
    }
}

static dks_window_t* dks_window_at_point(int32_t x, int32_t y) {
    dks_window_t* best = NULL;
    for (int i = 0; i < DKS_MAX_WINDOWS; i++) {
        if (!dks_windows[i].in_use) {
            continue;
        }
        dks_window_t* w = &dks_windows[i];
        if (x >= w->x && x < w->x + (int32_t)w->width &&
            y >= w->y && y < w->y + (int32_t)w->height) {
            if (!best || w->z > best->z) {
                best = w;
            }
        }
    }
    return best;
}

static void dks_redraw_windows(void) {
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS || mode.is_text_mode) {
        return;
    }

    bool cursor_was_visible = cursor_visible;
    if (cursor_was_visible) {
        draw_software_cursor(cursor_x, cursor_y, false);
    }

    graphics_clear_screen(dks_desktop_color);
    dks_draw_taskbar();

    dks_window_t* sorted[DKS_MAX_WINDOWS];
    int count = 0;
    for (int i = 0; i < DKS_MAX_WINDOWS; i++) {
        if (dks_windows[i].in_use) {
            sorted[count++] = &dks_windows[i];
        }
    }

    // Simple insertion sort by z (back to front)
    for (int i = 1; i < count; i++) {
        dks_window_t* key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j]->z > key->z) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    for (int i = 0; i < count; i++) {
        dks_render_window(sorted[i]);
    }

    if (dks_context_menu_visible) {
        dks_draw_context_menu(default_font ? default_font : NULL);
    }
    if (dks_start_menu_visible) {
        dks_draw_start_menu(default_font ? default_font : NULL);
    }

    if (cursor_was_visible) {
        draw_software_cursor(cursor_x, cursor_y, true);
    }

    graphics_swap_buffers();
}


// Draw cursor directly to framebuffer (software cursor)
static void draw_software_cursor(int32_t x, int32_t y, bool show) {
    // Remember old position so we restore the right background before moving
    int32_t prev_x = cursor_x;
    int32_t prev_y = cursor_y;

    // Restore old background if cursor was visible and background was saved
    if (cursor_visible && background_saved) {
        for (int row = 0; row < CURSOR_HEIGHT; row++) {
            for (int col = 0; col < CURSOR_WIDTH; col++) {
                graphics_draw_pixel(prev_x + col, prev_y + row,
                                    saved_background_pixels[row * CURSOR_WIDTH + col]);
            }
        }
    }

    if (!show) {
        cursor_visible = false;
        background_saved = false;
        return;
    }

    // Only draw if graphics mode is active (allow drawing even if TTY is ready)
    video_mode_t current_mode;
    if (graphics_get_current_mode(&current_mode) != GRAPHICS_SUCCESS || current_mode.is_text_mode) {
        cursor_visible = false;
        background_saved = false;
        return;
    }

    cursor_visible = true;

    // Boundary checks (fill cached resolution if needed)
    if (dks_gfx_screen_width == 0 || dks_gfx_screen_height == 0) { // First time or resolution changed
        video_mode_t mode;
        if (graphics_get_current_mode(&mode) == GRAPHICS_SUCCESS) {
            dks_gfx_screen_width = mode.width;
            dks_gfx_screen_height = mode.height;
        }
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (int32_t)dks_gfx_screen_width - CURSOR_WIDTH) {
        x = (int32_t)dks_gfx_screen_width - CURSOR_WIDTH;
    }
    if (y > (int32_t)dks_gfx_screen_height - CURSOR_HEIGHT) {
        y = (int32_t)dks_gfx_screen_height - CURSOR_HEIGHT;
    }

    cursor_x = x;
    cursor_y = y;

    // Save background before drawing new cursor
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            graphics_get_pixel(cursor_x + col, cursor_y + row,
                               &saved_background_pixels[row * CURSOR_WIDTH + col]);
        }
    }
    background_saved = true;

    // Draw the arrow cursor directly using pixels
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            uint8_t mask = arrow_cursor_mask[row * CURSOR_WIDTH + col];
            if (mask == CURSOR_MASK_TRANSPARENT) {
                continue;
            }
            graphics_color_t color = (mask == CURSOR_MASK_OUTLINE) ? cursor_outline_color : cursor_fill_color;
            graphics_draw_pixel(cursor_x + col, cursor_y + row, color);
        }
    }
}

static void dks_update_cursor_palette(uint8_t button_mask) {
    if (button_mask == cursor_button_mask) {
        return;
    }

    cursor_button_mask = button_mask;
    cursor_outline_color = COLOR_BLACK;
    cursor_fill_color = COLOR_WHITE;

    switch (button_mask) {
        case 1: // Left
            cursor_fill_color = graphics_make_color(220, 80, 80, 255);
            break;
        case 2: // Right
            cursor_fill_color = graphics_make_color(80, 140, 220, 255);
            break;
        case 4: // Middle
            cursor_fill_color = graphics_make_color(90, 200, 120, 255);
            break;
        case 3: // Left + Right
            cursor_fill_color = graphics_make_color(200, 120, 220, 255);
            break;
        case 5: // Left + Middle
            cursor_fill_color = graphics_make_color(230, 190, 90, 255);
            break;
        case 6: // Middle + Right
            cursor_fill_color = graphics_make_color(90, 200, 200, 255);
            break;
        case 7: // All buttons
            cursor_fill_color = graphics_make_color(255, 255, 255, 255);
            cursor_outline_color = graphics_make_color(50, 50, 50, 255);
            break;
        default:
            break;
    }

    if (cursor_visible) {
        draw_software_cursor(cursor_x, cursor_y, true);
    }
}

// New mouse event handler for DKS
static void dks_mouse_event_handler(const ps2_mouse_event_t* event) {
    uint8_t button_mask = 0;
    if (event->left_button) {
        button_mask |= 1;
    }
    if (event->right_button) {
        button_mask |= 2;
    }
    if (event->middle_button) {
        button_mask |= 4;
    }

    dks_update_cursor_palette(button_mask);

    // GUI mode interactions: dragging windows by title bar
    if (dks_guimode_active) {
        bool left_pressed = (button_mask & 1) != 0;
        bool left_pressed_prev = (dks_prev_button_mask & 1) != 0;

        if (left_pressed && !left_pressed_prev) {
            dks_window_t* hit = dks_window_at_point(cursor_x, cursor_y);
            if (hit) {
                dks_bring_to_front(hit);
                uint32_t title_height = (hit->height < 18) ? hit->height : 18;
                if (cursor_y >= hit->y && cursor_y < hit->y + (int32_t)title_height) {
                    dks_dragging_window = true;
                    dks_drag_window_id = hit->id;
                    dks_drag_offset_x = cursor_x - hit->x;
                    dks_drag_offset_y = cursor_y - hit->y;
                }
                dks_redraw_windows();
            }
        } else if (!left_pressed && left_pressed_prev && dks_dragging_window) {
            dks_dragging_window = false;
            dks_drag_window_id = -1;
        }

        if (dks_dragging_window) {
            dks_window_t* drag_win = dks_find_window(dks_drag_window_id);
            if (drag_win) {
                int32_t new_x = cursor_x - dks_drag_offset_x;
                int32_t new_y = cursor_y - dks_drag_offset_y;

                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;
                if (dks_gfx_screen_width > 0 && new_x + (int32_t)drag_win->width > (int32_t)dks_gfx_screen_width) {
                    new_x = (int32_t)dks_gfx_screen_width - (int32_t)drag_win->width;
                }
                if (dks_gfx_screen_height > 0 && new_y + (int32_t)drag_win->height > (int32_t)dks_gfx_screen_height) {
                    new_y = (int32_t)dks_gfx_screen_height - (int32_t)drag_win->height;
                }

                drag_win->x = new_x;
                drag_win->y = new_y;
                dks_redraw_windows();
            }
        }

        dks_prev_button_mask = button_mask;
    }

    // Only update cursor if DKS is active and cursor is meant to be visible
    if (cursor_visible) {
        int32_t target_x = cursor_x + event->dx;
        int32_t target_y = cursor_y + event->dy;

        // Redraw cursor at new position; draw_software_cursor will handle bounds and restoring old pixels
        draw_software_cursor(target_x, target_y, true);
    }

    if (!dks_guimode_active) {
        uint16_t cell_col = 0;
        uint16_t cell_row = 0;
        dks_pixel_to_cell(cursor_x, cursor_y, &cell_col, &cell_row);

        if (dks_context_menu_visible && !event->left_button && !event->right_button) {
            // Update hover when moving with no buttons
            int32_t local_y = cursor_y - (dks_context_menu_y + (int32_t)dks_context_menu_padding);
            if (local_y >= 0 && dks_context_menu_item_height > 0) {
                int32_t hover_idx = (int32_t)(local_y / dks_context_menu_item_height);
                if (hover_idx >= 0 && hover_idx < (int32_t)dks_context_menu_item_count && hover_idx != dks_context_menu_hover) {
                    dks_context_menu_hover = hover_idx;
                    dks_draw_context_menu(default_font);
                }
            }
        }

        if (event->left_button && !dks_left_button_down) {
            if (dks_context_menu_visible) {
                dks_handle_context_menu_click(cursor_x, cursor_y);
            } else {
                dks_set_selection_anchor(cell_col, cell_row);
            }
        } else if (!event->left_button && dks_left_button_down && dks_selection_active) {
            dks_update_selection_endpoint(cell_col, cell_row);
            dks_selection_active = false;
        } else if (event->left_button && dks_selection_active) {
            dks_update_selection_endpoint(cell_col, cell_row);
        }

        if (event->right_button && !dks_right_button_down && graphics_is_initialized()) {
            // Only schedule context menu if fonts/graphics are available
            if (dks_ensure_default_font()) {
                dks_context_menu_request = true;
                dks_context_menu_request_x = cursor_x;
                dks_context_menu_request_y = cursor_y;
                dks_context_menu_hover = -1;
            }
        }
    } else {
        // GUI mode interactions (desktop)
        if (event->right_button && !dks_right_button_down && graphics_is_initialized()) {
            if (dks_ensure_default_font()) {
                dks_context_menu_request = true;
                dks_context_menu_request_x = cursor_x;
                dks_context_menu_request_y = cursor_y;
                dks_context_menu_hover = -1;
                dks_start_menu_visible = false;
            }
        }

        // Start menu hover/update
        if (dks_start_menu_visible && !event->left_button && dks_start_menu_item_height > 0) {
            int32_t menu_x = dks_start_button_rect.x;
            int32_t menu_y = (int32_t)dks_gfx_screen_height - (int32_t)dks_taskbar_height - dks_start_menu_height;
            if (menu_y < 0) menu_y = 0;
            int32_t local_y = cursor_y - menu_y - 10;
            if (local_y >= 0) {
                int32_t idx = local_y / dks_start_menu_item_height;
                if (idx >= 0 && idx < (int32_t)DKS_START_MENU_ENTRY_COUNT && idx != dks_start_menu_hover) {
                    dks_start_menu_hover = idx;
                    dks_redraw_windows();
                }
            }
        }
    }

    dks_left_button_down = event->left_button;
    dks_right_button_down = event->right_button;
}


static void dks_mouse_pointer(char* args) {
    if (!args) { print("Usage: mouse <x> <y> [show|hide]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* show_hide_str = strtok(NULL, " ");

    if (!x_str || !y_str) {
        print("Usage: mouse <x> <y> [show|hide]\n"); return;
    }

    int x = atoi(x_str);
    int y = atoi(y_str);
    bool show = true;

    if (show_hide_str) {
        if (strcmp(show_hide_str, "hide") == 0) {
            show = false;
        } else if (strcmp(show_hide_str, "show") == 0) {
            show = true;
        } else {
            print("Invalid argument for show/hide. Use 'show' or 'hide'.\n");
            return;
        }
    }

    // Draw cursor directly to framebuffer (software cursor)
    draw_software_cursor(x, y, show);

    print("Mouse cursor "); print(show ? "shown" : "hidden");
    print(" at ("); print_dec(x); print(", "); print_dec(y); print(")\n");
}

static void dks_fill_screen(char* args) {
    if (!args) { print("Usage: fill_screen <r> <g> <b>\n"); return; }
    char* r_str = strtok(args, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!r_str || !g_str || !b_str) {
        print("Usage: fill_screen <r> <g> <b>\n"); return;
    }
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    graphics_clear_screen(graphics_make_color(r, g, b, 255));
}

static void dks_clear_area(char* args) {
    if (!args) { print("Usage: clear_area <x> <y> <width> <height> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* width_str = strtok(NULL, " ");
    char* height_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    if (!x_str || !y_str || !width_str || !height_str || !r_str || !g_str || !b_str) {
        print("Usage: clear_area <x> <y> <width> <height> <r> <g> <b>\n"); return;
    }
    graphics_rect_t rect = {atoi(x_str), atoi(y_str), atoi(width_str), atoi(height_str)};
    graphics_color_t color = graphics_make_color(atoi(r_str), atoi(g_str), atoi(b_str), 255);
    graphics_draw_rect(&rect, color, true); // Always filled
}

static void dks_draw_circle(char* args) {
    if (!args) { print("Usage: circle <center_x> <center_y> <radius> <r> <g> <b> [filled]\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* r_str_radius = strtok(NULL, " ");
    char* r_str_color = strtok(NULL, " ");
    char* g_str_color = strtok(NULL, " ");
    char* b_str_color = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");

    if (!cx_str || !cy_str || !r_str_radius || !r_str_color || !g_str_color || !b_str_color) {
        print("Usage: circle <center_x> <center_y> <radius> <r> <g> <b> [filled]\n"); return;
    }

    int32_t center_x = atoi(cx_str);
    int32_t center_y = atoi(cy_str);
    int32_t radius = atoi(r_str_radius);
    uint8_t r = atoi(r_str_color);
    uint8_t g = atoi(g_str_color);
    uint8_t b = atoi(b_str_color);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;

    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Filled circle
        for (int32_t y = -radius; y <= radius; y++) {
            for (int32_t x = -radius; x <= radius; x++) {
                if (x*x + y*y <= radius*radius) {
                    graphics_draw_pixel(center_x + x, center_y + y, color);
                }
            }
        }
    } else {
        // Outline circle using midpoint algorithm
        for (int angle = 0; angle < 360; angle++) {
            int32_t x = center_x + (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
            int32_t y = center_y + (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x, y, color);
        }
    }
}

static void dks_draw_triangle(char* args) {
    if (!args) { print("Usage: triangle <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* x3_str = strtok(NULL, " ");
    char* y3_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");

    if (!x1_str || !y1_str || !x2_str || !y2_str || !x3_str || !y3_str || !r_str || !g_str || !b_str) {
        print("Usage: triangle <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return;
    }

    int32_t x1 = atoi(x1_str);
    int32_t y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str);
    int32_t y2 = atoi(y2_str);
    int32_t x3 = atoi(x3_str);
    int32_t y3 = atoi(y3_str);
    uint8_t r = atoi(r_str);
    uint8_t g = atoi(g_str);
    uint8_t b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;

    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Simple filled triangle using scanline algorithm
        // Sort vertices by y
        int32_t tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2, tx3 = x3, ty3 = y3;
        // Bubble sort by y
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        if (ty2 > ty3) { int32_t t = tx2; tx2 = tx3; tx3 = t; t = ty2; ty2 = ty3; ty3 = t; }
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        
        // Draw filled triangle
        for (int32_t y = ty1; y <= ty3; y++) {
            int32_t x_start, x_end;
            if (y < ty2) {
                // Top half
                x_start = tx1 + (tx2 - tx1) * (y - ty1) / (ty2 - ty1);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            } else {
                // Bottom half
                x_start = tx2 + (tx3 - tx2) * (y - ty2) / (ty3 - ty2);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            }
            if (x_start > x_end) { int32_t t = x_start; x_start = x_end; x_end = t; }
            for (int32_t x = x_start; x <= x_end; x++) {
                graphics_draw_pixel(x, y, color);
            }
        }
    } else {
        // Outline triangle
        graphics_draw_line(x1, y1, x2, y2, color);
        graphics_draw_line(x2, y2, x3, y3, color);
        graphics_draw_line(x3, y3, x1, y1, color);
    }
}

static void dks_draw_string(char* args) {
    if (!args) { print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return; }

    // Find the quote FIRST, before any strtok calls modify the string
    char* quote_start = strchr(args, '\"');
    if (!quote_start) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b> - Text must be quoted.\n"); return;
    }

    // Parse x and y manually from the part before the quote
    char* p = args;
    while (*p == ' ') p++; // Skip leading spaces

    // Parse x
    char* x_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == x_start) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return;
    }
    char x_buf[16] = {0};
    size_t x_len = p - x_start;
    if (x_len >= sizeof(x_buf)) x_len = sizeof(x_buf) - 1;
    memcpy(x_buf, x_start, x_len);
    int32_t x = atoi(x_buf);

    while (*p == ' ') p++; // Skip spaces

    // Parse y
    char* y_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == y_start) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return;
    }
    char y_buf[16] = {0};
    size_t y_len = p - y_start;
    if (y_len >= sizeof(y_buf)) y_len = sizeof(y_buf) - 1;
    memcpy(y_buf, y_start, y_len);
    int32_t y = atoi(y_buf);

    // Now extract the quoted text
    char* text_start = quote_start + 1;
    char* text_end = strchr(text_start, '\"');
    if (!text_end) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b> - Mismatched quotes.\n"); return;
    }
    *text_end = '\0';
    char* text_str = text_start;

    // Parse colors after the closing quote
    char* color_args = text_end + 1;
    while (*color_args == ' ') color_args++;

    char* r_str = strtok(color_args, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!r_str || !g_str || !b_str) {
        print("Usage: draw_string <x> <y> \"<text>\" <r> <g> <b>\n"); return;
    }

    uint8_t r = atoi(r_str);
    uint8_t g = atoi(g_str);
    uint8_t b = atoi(b_str);

    if (!dks_ensure_default_font()) {
        print("ERROR: Failed to load default font for draw_string. (e.g., 'default_8x16')\n");
        return;
    }

    graphics_color_t color = graphics_make_color(r, g, b, 255);
    graphics_draw_text(x, y, text_str, default_font, color);
}


static void dks_draw_window(char* args) {
    if (!args) { print("Usage: draw_window <x> <y> <width> <height> \"<title>\" <border_r> <border_g> <border_b> <fill_r> <fill_g> <fill_b>\n"); return; }

    // Find the quote FIRST, before any strtok calls modify the string
    char* quote_start = strchr(args, '\"');
    if (!quote_start) {
        print("Usage: draw_window ... \"<title>\" ... - Title must be quoted.\n"); return;
    }

    // Parse x, y, width, height manually from the part before the quote
    char* p = args;
    while (*p == ' ') p++; // Skip leading spaces

    // Parse x
    char* x_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == x_start) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" ...\n"); return;
    }
    char x_buf[16] = {0};
    size_t x_len = p - x_start;
    if (x_len >= sizeof(x_buf)) x_len = sizeof(x_buf) - 1;
    memcpy(x_buf, x_start, x_len);
    int32_t x = atoi(x_buf);

    while (*p == ' ') p++;

    // Parse y
    char* y_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == y_start) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" ...\n"); return;
    }
    char y_buf[16] = {0};
    size_t y_len = p - y_start;
    if (y_len >= sizeof(y_buf)) y_len = sizeof(y_buf) - 1;
    memcpy(y_buf, y_start, y_len);
    int32_t y = atoi(y_buf);

    while (*p == ' ') p++;

    // Parse width
    char* w_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == w_start) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" ...\n"); return;
    }
    char w_buf[16] = {0};
    size_t w_len = p - w_start;
    if (w_len >= sizeof(w_buf)) w_len = sizeof(w_buf) - 1;
    memcpy(w_buf, w_start, w_len);
    uint32_t width = atoi(w_buf);

    while (*p == ' ') p++;

    // Parse height
    char* h_start = p;
    while (*p && *p != ' ' && p < quote_start) p++;
    if (p == h_start) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" ...\n"); return;
    }
    char h_buf[16] = {0};
    size_t h_len = p - h_start;
    if (h_len >= sizeof(h_buf)) h_len = sizeof(h_buf) - 1;
    memcpy(h_buf, h_start, h_len);
    uint32_t height = atoi(h_buf);

    // Now extract the quoted title
    char* title_start = quote_start + 1;
    char* title_end = strchr(title_start, '\"');
    if (!title_end) {
        print("Usage: draw_window ... \"<title>\" ... - Mismatched quotes.\n"); return;
    }
    *title_end = '\0';
    char* title_str = title_start;

    // Parse colors after the closing quote
    char* color_args = title_end + 1;
    while (*color_args == ' ') color_args++;

    char* border_r_str = strtok(color_args, " ");
    char* border_g_str = strtok(NULL, " ");
    char* border_b_str = strtok(NULL, " ");
    char* fill_r_str = strtok(NULL, " ");
    char* fill_g_str = strtok(NULL, " ");
    char* fill_b_str = strtok(NULL, " ");

    if (!border_r_str || !border_g_str || !border_b_str || !fill_r_str || !fill_g_str || !fill_b_str) {
        print("Usage: draw_window <x> <y> <width> <height> \"<title>\" <border_r> <border_g> <border_b> <fill_r> <fill_g> <fill_b>\n"); return;
    }

    graphics_color_t border_color = graphics_make_color(atoi(border_r_str), atoi(border_g_str), atoi(border_b_str), 255);
    graphics_color_t fill_color = graphics_make_color(atoi(fill_r_str), atoi(fill_g_str), atoi(fill_b_str), 255);

    if (width == 0 || height == 0) {
        print("ERROR: Window width/height must be greater than zero.\n");
        return;
    }

    dks_window_t* window = dks_allocate_window();
    if (!window) {
        print("ERROR: Maximum window count reached.\n");
        return;
    }

    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->border_color = border_color;
    window->fill_color = fill_color;
    window->widget_count = 0;
    strncpy(window->title, title_str, sizeof(window->title) - 1);
    window->title[sizeof(window->title) - 1] = '\0';

    dks_redraw_windows();

    print("Window ");
    print_dec(window->id);
    print(" created at (");
    print_dec(x);
    print(", ");
    print_dec(y);
    print(")\n");
} 

static void dks_window_move(char* args) {
    if (!args) { print("Usage: window_move <id> <x> <y>\n"); return; }

    char* id_str = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");

    if (!id_str || !x_str || !y_str) {
        print("Usage: window_move <id> <x> <y>\n");
        return;
    }

    int id = atoi(id_str);
    dks_window_t* window = dks_find_window(id);
    if (!window) {
        print("Window not found.\n");
        return;
    }

    window->x = atoi(x_str);
    window->y = atoi(y_str);
    dks_bring_to_front(window);
    dks_redraw_windows();

    print("Moved window ");
    print_dec(id);
    print(" to (");
    print_dec(window->x);
    print(", ");
    print_dec(window->y);
    print(")\n");
}

static void dks_window_add_button(char* args) {
    if (!args) { print("Usage: window_add_button <id> <x> <y> <width> <height> \"<label>\"\n"); return; }

    char* id_str = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* label_part = strtok(NULL, "");

    if (!id_str || !x_str || !y_str || !w_str || !h_str || !label_part) {
        print("Usage: window_add_button <id> <x> <y> <width> <height> \"<label>\"\n");
        return;
    }

    char* quote = strchr(label_part, '\"');
    if (!quote) {
        print("Usage: window_add_button ... \"<label>\"\n");
        return;
    }
    char* label_start = quote + 1;
    char* label_end = strchr(label_start, '\"');
    if (!label_end) {
        print("Usage: window_add_button ... \"<label>\" (mismatched quotes)\n");
        return;
    }
    *label_end = '\0';

    dks_window_t* window = dks_find_window(atoi(id_str));
    if (!window) {
        print("Window not found.\n");
        return;
    }
    if (window->widget_count >= DKS_MAX_WIDGETS_PER_WINDOW) {
        print("Window widget limit reached.\n");
        return;
    }

    uint32_t width = (uint32_t)atoi(w_str);
    uint32_t height = (uint32_t)atoi(h_str);
    if (width == 0 || height == 0) {
        print("Button width/height must be greater than zero.\n");
        return;
    }

    dks_widget_t* widget = &window->widgets[window->widget_count++];
    memset(widget, 0, sizeof(*widget));
    widget->type = DKS_WIDGET_BUTTON;
    widget->x = atoi(x_str);
    widget->y = atoi(y_str);
    widget->width = width;
    widget->height = height;
    strncpy(widget->text, label_start, sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';

    dks_redraw_windows();

    print("Added button to window ");
    print_dec(window->id);
    print(".\n");
}

static void dks_window_add_text(char* args) {
    if (!args) { print("Usage: window_add_text <id> <x> <y> \"<text>\"\n"); return; }

    char* id_str = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");
    char* text_part = strtok(NULL, "");

    if (!id_str || !x_str || !y_str || !text_part) {
        print("Usage: window_add_text <id> <x> <y> \"<text>\"\n");
        return;
    }

    char* quote = strchr(text_part, '\"');
    if (!quote) { print("Usage: window_add_text ... \"<text>\"\n"); return; }
    char* text_start = quote + 1;
    char* text_end = strchr(text_start, '\"');
    if (!text_end) { print("Usage: window_add_text ... \"<text>\" (mismatched quotes)\n"); return; }
    *text_end = '\0';

    dks_window_t* window = dks_find_window(atoi(id_str));
    if (!window) {
        print("Window not found.\n");
        return;
    }
    if (window->widget_count >= DKS_MAX_WIDGETS_PER_WINDOW) {
        print("Window widget limit reached.\n");
        return;
    }

    dks_widget_t* widget = &window->widgets[window->widget_count++];
    memset(widget, 0, sizeof(*widget));
    widget->type = DKS_WIDGET_LABEL;
    widget->x = atoi(x_str);
    widget->y = atoi(y_str);
    widget->width = 0;
    widget->height = 0;
    strncpy(widget->text, text_start, sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';

    dks_redraw_windows();

    print("Added text to window ");
    print_dec(window->id);
    print(".\n");
}

static void dks_window_add_input(char* args) {
    if (!args) { print("Usage: window_add_input <id> <x> <y> <width> <height> \"<placeholder>\"\n"); return; }

    char* id_str = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* text_part = strtok(NULL, "");

    if (!id_str || !x_str || !y_str || !w_str || !h_str || !text_part) {
        print("Usage: window_add_input <id> <x> <y> <width> <height> \"<placeholder>\"\n");
        return;
    }

    char* quote = strchr(text_part, '\"');
    if (!quote) { print("Usage: window_add_input ... \"<placeholder>\"\n"); return; }
    char* text_start = quote + 1;
    char* text_end = strchr(text_start, '\"');
    if (!text_end) { print("Usage: window_add_input ... \"<placeholder>\" (mismatched quotes)\n"); return; }
    *text_end = '\0';

    dks_window_t* window = dks_find_window(atoi(id_str));
    if (!window) {
        print("Window not found.\n");
        return;
    }
    if (window->widget_count >= DKS_MAX_WIDGETS_PER_WINDOW) {
        print("Window widget limit reached.\n");
        return;
    }

    uint32_t width = (uint32_t)atoi(w_str);
    uint32_t height = (uint32_t)atoi(h_str);
    if (width == 0 || height == 0) {
        print("Input width/height must be greater than zero.\n");
        return;
    }

    dks_widget_t* widget = &window->widgets[window->widget_count++];
    memset(widget, 0, sizeof(*widget));
    widget->type = DKS_WIDGET_TEXT_INPUT;
    widget->x = atoi(x_str);
    widget->y = atoi(y_str);
    widget->width = width;
    widget->height = height;
    strncpy(widget->text, text_start, sizeof(widget->text) - 1);
    widget->text[sizeof(widget->text) - 1] = '\0';

    dks_redraw_windows();

    print("Added text input to window ");
    print_dec(window->id);
    print(".\n");
}

static void dks_guimode(char* args) {
    (void)args;
    dks_guimode_active = true;
    dks_dragging_window = false;
    dks_drag_window_id = -1;
    dks_prev_button_mask = 0;
    dks_start_menu_visible = false;
    dks_context_menu_visible = false;

    // Ensure we have the latest screen dimensions for clamping
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) == GRAPHICS_SUCCESS) {
        dks_gfx_screen_width = mode.width;
        dks_gfx_screen_height = mode.height;
    }

    graphics_clear_screen(dks_desktop_color);
    dks_draw_taskbar();
    draw_software_cursor(cursor_x, cursor_y, true);
    dks_redraw_windows();

    print("GUI mode active. Drag windows by their title bars. Press ESC or Q to exit.\n");

    while (dks_guimode_active) {
        // Keep mouse input flowing even if interrupts are paused
        ps2_mouse_poll();

        char ch = 0;
        if (keyboard_poll_char(&ch)) {
            if (ch == 0x1B || ch == 'q' || ch == 'Q') {
                dks_guimode_active = false;
                break;
            }
        }

        // Continuous redraw to keep desktop covering any TTY output
        dks_redraw_windows();

        __asm__ __volatile__("hlt");
    }

    dks_dragging_window = false;
    dks_drag_window_id = -1;
    dks_prev_button_mask = 0;
    dks_redraw_windows();
    print("GUI mode exited.\n");
}

static void dks_swap_buffers(char* args) {
    if (args) { print("Usage: swap_buffers\n"); return; }
    graphics_swap_buffers();
    print("Buffers swapped.\n");
}

static void dks_enable_double_buffering(char* args) {
    if (!args) { print("Usage: enable_double_buffering <true|false>\n"); return; }
    bool enable = strcmp(args, "true") == 0;
    graphics_enable_double_buffering(enable);
    print("Double buffering "); print(enable ? "enabled" : "disabled"); print(".\n");
}

static void dks_wait_vsync(char* args) {
    if (args) { print("Usage: wait_vsync\n"); return; }
    graphics_wait_for_vsync();
    print("Waited for VSync.\n");
}

static void dks_set_resolution(char* args) {
    if (!args) { print("Usage: set_resolution <width> <height> <bpp> [refresh_rate]\n"); return; }
    char* width_str = strtok(args, " ");
    char* height_str = strtok(NULL, " ");
    char* bpp_str = strtok(NULL, " ");
    char* refresh_rate_str = strtok(NULL, " ");

    if (!width_str || !height_str || !bpp_str) {
        print("Usage: set_resolution <width> <height> <bpp> [refresh_rate]\n"); return;
    }

    uint32_t width = atoi(width_str);
    uint32_t height = atoi(height_str);
    uint32_t bpp = atoi(bpp_str);
    uint32_t refresh_rate = refresh_rate_str ? atoi(refresh_rate_str) : 0; // 0 for default/don't care

    if (graphics_set_mode(width, height, bpp, refresh_rate) == GRAPHICS_SUCCESS) {
        print("Resolution set to "); print_dec(width); print("x"); print_dec(height);
        print(" @ "); print_dec(bpp); print("bpp.\n");
    } else {
        print("ERROR: Failed to set resolution.\n");
    }
}

static void dks_res_list(void) {
    video_mode_t* modes = NULL;
    uint32_t count = 0;

    graphics_result_t result = graphics_enumerate_modes(&modes, &count);
    if (result != GRAPHICS_SUCCESS || !modes || count == 0) {
        print("Unable to enumerate video modes.\n");

        // At least show the current mode
        video_mode_t current;
        if (graphics_get_current_mode(&current) == GRAPHICS_SUCCESS) {
            print("Current mode: ");
            print_dec(current.width); print("x"); print_dec(current.height);
            print(" @ "); print_dec(current.bpp); print("bpp");
            if (current.refresh_rate > 0) {
                print(" "); print_dec(current.refresh_rate); print("Hz");
            }
            print(current.is_text_mode ? " (text)\n" : " (graphics)\n");
        }
        return;
    }

    print("Supported video modes ("); print_dec(count); print(" modes):\n");
    print("  Width   Height  BPP   Refresh  Type\n");
    print("  -----   ------  ---   -------  ----\n");

    for (uint32_t i = 0; i < count; i++) {
        print("  ");
        // Width (right-aligned in 5 chars)
        if (modes[i].width < 1000) print(" ");
        if (modes[i].width < 100) print(" ");
        print_dec(modes[i].width);
        print("   ");

        // Height (right-aligned in 6 chars)
        if (modes[i].height < 1000) print(" ");
        if (modes[i].height < 100) print(" ");
        print_dec(modes[i].height);
        print("   ");

        // BPP (right-aligned in 3 chars)
        if (modes[i].bpp < 10) print(" ");
        print_dec(modes[i].bpp);
        print("    ");

        // Refresh rate (right-aligned in 7 chars)
        if (modes[i].refresh_rate > 0) {
            if (modes[i].refresh_rate < 100) print(" ");
            if (modes[i].refresh_rate < 10) print(" ");
            print_dec(modes[i].refresh_rate);
            print("Hz");
        } else {
            print("  N/A  ");
        }
        print("   ");

        // Type
        print(modes[i].is_text_mode ? "Text" : "Graphics");
        print("\n");
    }

    // Show current mode
    video_mode_t current;
    if (graphics_get_current_mode(&current) == GRAPHICS_SUCCESS) {
        print("\nCurrent: ");
        print_dec(current.width); print("x"); print_dec(current.height);
        print(" @ "); print_dec(current.bpp); print("bpp\n");
    }
}

static void dks_get_resolution(void) {
    video_mode_t current;
    if (graphics_get_current_mode(&current) == GRAPHICS_SUCCESS) {
        print("Current resolution: ");
        print_dec(current.width); print("x"); print_dec(current.height);
        print(" @ "); print_dec(current.bpp); print("bpp");
        if (current.refresh_rate > 0) {
            print(" "); print_dec(current.refresh_rate); print("Hz");
        }
        print(current.is_text_mode ? " (text mode)\n" : " (graphics mode)\n");
    } else {
        print("ERROR: Failed to get current resolution.\n");
    }
}

// Additional drawing commands
static void dks_draw_bezier(char* args) {
    if (!args) { print("Usage: draw_bezier <x0> <y0> <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b>\n"); return; }
    char* x0_str = strtok(args, " ");
    char* y0_str = strtok(NULL, " ");
    char* x1_str = strtok(NULL, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* x3_str = strtok(NULL, " ");
    char* y3_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!x0_str || !y0_str || !x1_str || !y1_str || !x2_str || !y2_str || !x3_str || !y3_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_bezier <x0> <y0> <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b>\n"); return;
    }

    int32_t x0 = atoi(x0_str), y0 = atoi(y0_str);
    int32_t x1 = atoi(x1_str), y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str), y2 = atoi(y2_str);
    int32_t x3 = atoi(x3_str), y3 = atoi(y3_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    // Cubic Bezier curve using De Casteljau's algorithm
    for (double t = 0.0; t <= 1.0; t += 0.005) {
        double u = 1.0 - t;
        double tt = t * t;
        double uu = u * u;
        double uuu = uu * u;
        double ttt = tt * t;

        int32_t px = (int32_t)(uuu * x0 + 3.0 * uu * t * x1 + 3.0 * u * tt * x2 + ttt * x3);
        int32_t py = (int32_t)(uuu * y0 + 3.0 * uu * t * y1 + 3.0 * u * tt * y2 + ttt * y3);

        graphics_draw_pixel(px, py, color);
    }
    print("Bezier curve drawn.\n");
}

static void dks_draw_star(char* args) {
    if (!args) { print("Usage: draw_star <center_x> <center_y> <outer_r> <inner_r> <points> <r> <g> <b> [filled]\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* or_str = strtok(NULL, " ");
    char* ir_str = strtok(NULL, " ");
    char* pts_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");

    if (!cx_str || !cy_str || !or_str || !ir_str || !pts_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_star <center_x> <center_y> <outer_r> <inner_r> <points> <r> <g> <b> [filled]\n"); return;
    }

    int32_t cx = atoi(cx_str), cy = atoi(cy_str);
    int32_t outer_r = atoi(or_str), inner_r = atoi(ir_str);
    int points = atoi(pts_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;

    if (points < 3) points = 5; // Default to 5-pointed star

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    // Calculate star vertices
    double angle_step = 3.14159 / points;
    int32_t prev_x = cx + (int32_t)(outer_r * dks_cos(-1.5708)); // Start at top
    int32_t prev_y = cy + (int32_t)(outer_r * dks_sin(-1.5708));

    for (int i = 1; i <= points * 2; i++) {
        double angle = -1.5708 + i * angle_step;
        int32_t radius = (i % 2 == 0) ? outer_r : inner_r;
        int32_t curr_x = cx + (int32_t)(radius * dks_cos(angle));
        int32_t curr_y = cy + (int32_t)(radius * dks_sin(angle));

        graphics_draw_line(prev_x, prev_y, curr_x, curr_y, color);
        prev_x = curr_x;
        prev_y = curr_y;
    }

    if (filled) {
        // Simple flood-fill approximation: draw lines from center to each edge point
        for (int i = 0; i < points * 2; i++) {
            double angle = -1.5708 + i * angle_step;
            int32_t radius = (i % 2 == 0) ? outer_r : inner_r;
            int32_t px = cx + (int32_t)(radius * dks_cos(angle));
            int32_t py = cy + (int32_t)(radius * dks_sin(angle));
            graphics_draw_line(cx, cy, px, py, color);
        }
    }
    print("Star drawn.\n");
}

static void dks_draw_thick_line(char* args) {
    if (!args) { print("Usage: draw_thick_line <x1> <y1> <x2> <y2> <thickness> <r> <g> <b>\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* thick_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!x1_str || !y1_str || !x2_str || !y2_str || !thick_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_thick_line <x1> <y1> <x2> <y2> <thickness> <r> <g> <b>\n"); return;
    }

    int32_t x1 = atoi(x1_str), y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str), y2 = atoi(y2_str);
    int32_t thickness = atoi(thick_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    // Draw multiple parallel lines for thickness
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    double len = 1.0;
    if (dx != 0 || dy != 0) {
        len = dx * dx + dy * dy;
        // Simple square root approximation
        double x = len;
        for (int i = 0; i < 10; i++) {
            x = (x + len / x) / 2.0;
        }
        len = x;
    }

    double nx = -dy / len; // Normal vector
    double ny = dx / len;

    for (int32_t i = -thickness / 2; i <= thickness / 2; i++) {
        int32_t offset_x = (int32_t)(nx * i);
        int32_t offset_y = (int32_t)(ny * i);
        graphics_draw_line(x1 + offset_x, y1 + offset_y, x2 + offset_x, y2 + offset_y, color);
    }
    print("Thick line drawn.\n");
}

static void dks_draw_dotted_line(char* args) {
    if (!args) { print("Usage: draw_dotted_line <x1> <y1> <x2> <y2> <gap> <r> <g> <b>\n"); return; }
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* gap_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!x1_str || !y1_str || !x2_str || !y2_str || !gap_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_dotted_line <x1> <y1> <x2> <y2> <gap> <r> <g> <b>\n"); return;
    }

    int32_t x1 = atoi(x1_str), y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str), y2 = atoi(y2_str);
    int32_t gap = atoi(gap_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);

    if (gap < 1) gap = 5;

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    int32_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = (dx > dy ? dx : -dy) / 2;
    int32_t count = 0;

    while (1) {
        if (count % (gap * 2) < gap) {
            graphics_draw_pixel(x1, y1, color);
        }
        count++;
        if (x1 == x2 && y1 == y2) break;
        int32_t e2 = err;
        if (e2 > -dx) { err -= dy; x1 += sx; }
        if (e2 < dy) { err += dx; y1 += sy; }
    }
    print("Dotted line drawn.\n");
}

static void dks_draw_crosshair(char* args) {
    if (!args) { print("Usage: draw_crosshair <x> <y> <size> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* size_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!x_str || !y_str || !size_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_crosshair <x> <y> <size> <r> <g> <b>\n"); return;
    }

    int32_t x = atoi(x_str), y = atoi(y_str);
    int32_t size = atoi(size_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    // Draw horizontal line
    graphics_draw_line(x - size, y, x + size, y, color);
    // Draw vertical line
    graphics_draw_line(x, y - size, x, y + size, color);
    // Draw center circle
    for (int angle = 0; angle < 360; angle += 10) {
        int32_t px = x + (int32_t)(3 * dks_cos(angle * 3.14159 / 180.0));
        int32_t py = y + (int32_t)(3 * dks_sin(angle * 3.14159 / 180.0));
        graphics_draw_pixel(px, py, color);
    }
    print("Crosshair drawn.\n");
}

static void dks_draw_grid(char* args) {
    if (!args) { print("Usage: draw_grid <x> <y> <width> <height> <cell_size> <r> <g> <b>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* cell_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");

    if (!x_str || !y_str || !w_str || !h_str || !cell_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_grid <x> <y> <width> <height> <cell_size> <r> <g> <b>\n"); return;
    }

    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint32_t cell = atoi(cell_str);
    uint8_t r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);

    if (cell < 1) cell = 10;

    graphics_color_t color = graphics_make_color(r, g, b, 255);

    // Draw vertical lines
    for (uint32_t i = 0; i <= w; i += cell) {
        graphics_draw_line(x + i, y, x + i, y + h, color);
    }
    // Draw horizontal lines
    for (uint32_t i = 0; i <= h; i += cell) {
        graphics_draw_line(x, y + i, x + w, y + i, color);
    }
    print("Grid drawn.\n");
}

static void dks_draw_checker(char* args) {
    if (!args) { print("Usage: draw_checker <x> <y> <width> <height> <cell_size> <r1> <g1> <b1> <r2> <g2> <b2>\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* cell_str = strtok(NULL, " ");
    char* r1_str = strtok(NULL, " ");
    char* g1_str = strtok(NULL, " ");
    char* b1_str = strtok(NULL, " ");
    char* r2_str = strtok(NULL, " ");
    char* g2_str = strtok(NULL, " ");
    char* b2_str = strtok(NULL, " ");

    if (!x_str || !y_str || !w_str || !h_str || !cell_str || !r1_str || !g1_str || !b1_str || !r2_str || !g2_str || !b2_str) {
        print("Usage: draw_checker <x> <y> <width> <height> <cell_size> <r1> <g1> <b1> <r2> <g2> <b2>\n"); return;
    }

    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint32_t cell = atoi(cell_str);
    uint8_t r1 = atoi(r1_str), g1 = atoi(g1_str), b1 = atoi(b1_str);
    uint8_t r2 = atoi(r2_str), g2 = atoi(g2_str), b2 = atoi(b2_str);

    if (cell < 1) cell = 10;

    graphics_color_t color1 = graphics_make_color(r1, g1, b1, 255);
    graphics_color_t color2 = graphics_make_color(r2, g2, b2, 255);

    for (uint32_t row = 0; row < h / cell; row++) {
        for (uint32_t col = 0; col < w / cell; col++) {
            graphics_color_t color = ((row + col) % 2 == 0) ? color1 : color2;
            graphics_rect_t rect = {x + col * cell, y + row * cell, cell, cell};
            graphics_draw_rect(&rect, color, true);
        }
    }
    print("Checkerboard drawn.\n");
}

// Keyboard locale change command
static void dks_kb_locale_change(char* args) {
    if (!args) {
        print("Usage: kb_locale_change <locale>\n");
        print("Available locales: US, GB\n");
        return;
    }

    // Skip leading spaces
    while (*args == ' ') args++;

    keyboard_layout_id_t layout;

    if (strcmp(args, "US") == 0 || strcmp(args, "us") == 0) {
        layout = KEYBOARD_LAYOUT_US;
        ps2_keyboard_select_layout(layout);
        print("Keyboard layout changed to US (QWERTY)\n");
    } else if (strcmp(args, "GB") == 0 || strcmp(args, "gb") == 0 ||
               strcmp(args, "UK") == 0 || strcmp(args, "uk") == 0) {
        layout = KEYBOARD_LAYOUT_GB;
        ps2_keyboard_select_layout(layout);
        print("Keyboard layout changed to GB (UK)\n");
    } else {
        print("Unknown keyboard locale: ");
        print(args);
        print("\nAvailable locales: US, GB\n");
    }
}

static void dks_kb_locale_list(void) {
    print("Available keyboard locales:\n");
    print("  US  - United States (QWERTY)\n");
    print("  GB  - Great Britain / UK\n");
    print("\nUsage: kb_locale_change <locale>\n");
}

static void dks_move_cursor_left(int count) {
    for (int i = 0; i < count; i++) {
        print("\x1b[D");
    }
}

static void dks_move_cursor_right(int count) {
    for (int i = 0; i < count; i++) {
        print("\x1b[C");
    }
}

static void dks_redraw_input_line(const char* buffstr, int len, int cursor_pos) {
    if (!buffstr || len < 0 || cursor_pos < 0) {
        return;
    }

    dks_move_cursor_left(cursor_pos);
    for (int i = 0; i < len; i++) {
        printch(' ');
    }
    dks_move_cursor_left(len);
    if (len > 0) {
        print(buffstr);
    }
    if (cursor_pos < len) {
        dks_move_cursor_left(len - cursor_pos);
    }
}

static void dks_insert_text_at_cursor(char* buffstr, int* len, int* cursor_pos, const char* text, size_t text_len) {
    if (!buffstr || !len || !cursor_pos || !text || text_len == 0) {
        return;
    }

    int available = DKS_INPUT_MAX - 1 - *len;
    if (available <= 0) {
        return;
    }

    if ((int)text_len > available) {
        text_len = (size_t)available;
    }

    memmove(buffstr + *cursor_pos + text_len, buffstr + *cursor_pos, (size_t)(*len - *cursor_pos));
    memcpy(buffstr + *cursor_pos, text, text_len);
    *len += (int)text_len;
    *cursor_pos += (int)text_len;
    buffstr[*len] = '\0';
    dks_redraw_input_line(buffstr, *len, *cursor_pos);
}

// Enhanced readStr with arrow key support for history and cursor movement
static string dks_readStr_enhanced(void) {
    string buffstr = (string)malloc(DKS_INPUT_MAX);
    if (!buffstr) {
        return NULL;
    }

    buffstr[0] = '\0';
    int len = 0;
    int cursor_pos = 0;
    history_nav_index = -1; // Reset history navigation
    char* current_edit = NULL; // Buffer for editing current line

    bool interrupts_were_enabled = irq_are_enabled();
    if (!interrupts_were_enabled) {
        irq_enable_safe();
    }

    while (1) {
        char ch = 0;
        bool have_char = keyboard_poll_char(&ch);

        if (dks_paste_requested) {
            if (dks_clipboard_len > 0) {
                dks_insert_text_at_cursor(buffstr, &len, &cursor_pos, dks_clipboard, dks_clipboard_len);
            }
            dks_paste_requested = false;
        }

        if (dks_context_menu_request) {
            dks_context_menu_request = false;
            dks_show_context_menu(dks_context_menu_request_x, dks_context_menu_request_y);
        }

        if (!have_char) {
            // If PS/2 interrupts are being filtered by firmware/VM, poll manually
            ps2_mouse_poll();
            __asm__ __volatile__("hlt");
            continue;
        }

        // Handle ANSI escape sequences for arrow keys
        static enum { NORMAL, ESC, ESC_BRACKET } esc_state = NORMAL;
        static int esc_pos = 0;
        
        if (esc_state == NORMAL && ch == 0x1B) { // ESC
            esc_state = ESC;
            esc_pos = 1;
            continue;
        }
        
        if (esc_state == ESC) {
            if (ch == '[') {
                esc_state = ESC_BRACKET;
                esc_pos = 2;
                continue;
            } else {
                esc_state = NORMAL;
                esc_pos = 0;
            }
        }
        
        if (esc_state == ESC_BRACKET) {
            esc_pos++;
            if (esc_pos >= 3 || (ch >= 'A' && ch <= 'D')) {
                // Process arrow key
                esc_state = NORMAL;
                esc_pos = 0;
                
                if (ch == 'A') { // Up arrow
                    if (history_count > 0) {
                        if (history_nav_index == -1) {
                            if (len > 0) {
                                current_edit = strdup(buffstr);
                            }
                            history_nav_index = history_count - 1;
                        } else if (history_nav_index > 0) {
                            history_nav_index--;
                        }
                        
                        int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                        int idx = (start + history_nav_index) % HISTORY_SIZE;
                        if (history_buffer[idx]) {
                            for (int i = 0; i < len; i++) {
                                printch('\b');
                                printch(' ');
                                printch('\b');
                            }
                            strcpy(buffstr, history_buffer[idx]);
                            len = strlen(buffstr);
                            cursor_pos = len;
                            print(buffstr);
                        }
                    }
                    continue;
                }
                
                if (ch == 'B') { // Down arrow
                    if (history_nav_index >= 0) {
                        if (history_nav_index < history_count - 1) {
                            history_nav_index++;
                            int start = (history_index - history_count + HISTORY_SIZE) % HISTORY_SIZE;
                            int idx = (start + history_nav_index) % HISTORY_SIZE;
                            if (history_buffer[idx]) {
                                for (int i = 0; i < len; i++) {
                                    printch('\b');
                                    printch(' ');
                                    printch('\b');
                                }
                                strcpy(buffstr, history_buffer[idx]);
                                len = strlen(buffstr);
                                cursor_pos = len;
                                print(buffstr);
                            }
                        } else {
                            history_nav_index = -1;
                            for (int i = 0; i < len; i++) {
                                printch('\b');
                                printch(' ');
                                printch('\b');
                            }
                            if (current_edit) {
                                strcpy(buffstr, current_edit);
                                len = strlen(buffstr);
                                cursor_pos = len;
                                print(buffstr);
                                free(current_edit);
                                current_edit = NULL;
                            } else {
                                buffstr[0] = '\0';
                                len = 0;
                                cursor_pos = 0;
                            }
                        }
                    }
                    continue;
                }
                
                if (ch == 'C') { // Right arrow
                    if (cursor_pos < len) {
                        cursor_pos++;
                        dks_move_cursor_right(1);
                    }
                    continue;
                }
                
                if (ch == 'D') { // Left arrow
                    if (cursor_pos > 0) {
                        cursor_pos--;
                        dks_move_cursor_left(1);
                    }
                    continue;
                }
            }
            continue;
        }

        // Handle regular characters
        
        if (ch == '\r' || ch == '\n') {
            printch('\n');
            buffstr[len] = '\0';
            if (current_edit) {
                free(current_edit);
                current_edit = NULL;
            }
            break;
        }

        if (ch == '\b' || ch == 0x7F) { // Backspace or Delete
            if (cursor_pos > 0) {
                // Shift characters left
                for (int i = cursor_pos - 1; i < len; i++) {
                    buffstr[i] = buffstr[i + 1];
                }
                len--;
                cursor_pos--;
                // Redraw line
                printch('\b');
                for (int i = cursor_pos; i < len; i++) {
                    printch(buffstr[i]);
                }
                printch(' '); // Clear last character
                // Move cursor back
                dks_move_cursor_left(len - cursor_pos + 1);
            }
            continue;
        }

        // Ctrl+C
        if (ch == 0x03) {
            print("\n^C\n");
            if (current_edit) {
                free(current_edit);
                current_edit = NULL;
            }
            buffstr[0] = '\0';
            break;
        }

        // Insert character at cursor
        if (len < DKS_INPUT_MAX - 1 && ch >= 32 && ch < 127) {
            // Shift characters right
            for (int i = len; i > cursor_pos; i--) {
                buffstr[i] = buffstr[i - 1];
            }
            buffstr[cursor_pos] = ch;
            len++;
            cursor_pos++;
            // Redraw from cursor position
            for (int i = cursor_pos - 1; i < len; i++) {
                printch(buffstr[i]);
            }
            // Move cursor back to correct position
            dks_move_cursor_left(len - cursor_pos);
            buffstr[len] = '\0';
        }
    }

    if (!interrupts_were_enabled) {
        irq_disable_safe();
    }

    if (current_edit) {
        free(current_edit);
    }

    return buffstr;
}

// Additional draw commands
static void dks_draw_ellipse(char* args) {
    if (!args) { print("Usage: draw_ellipse <center_x> <center_y> <radius_x> <radius_y> <r> <g> <b> [filled]\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* rx_str = strtok(NULL, " ");
    char* ry_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!cx_str || !cy_str || !rx_str || !ry_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_ellipse <center_x> <center_y> <radius_x> <radius_y> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t cx = atoi(cx_str);
    int32_t cy = atoi(cy_str);
    uint32_t rx = atoi(rx_str);
    uint32_t ry = atoi(ry_str);
    uint8 r = atoi(r_str);
    uint8 g = atoi(g_str);
    uint8 b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    // Simple ellipse drawing using midpoint algorithm
    if (filled) {
        for (int32_t y = -ry; y <= (int32_t)ry; y++) {
            for (int32_t x = -rx; x <= (int32_t)rx; x++) {
                // Check if point is inside ellipse: (x/rx)^2 + (y/ry)^2 <= 1
                int64_t dx = (int64_t)x * (int64_t)x * (int64_t)ry * (int64_t)ry;
                int64_t dy = (int64_t)y * (int64_t)y * (int64_t)rx * (int64_t)rx;
                int64_t r2 = (int64_t)rx * (int64_t)rx * (int64_t)ry * (int64_t)ry;
                if (dx + dy <= r2) {
                    graphics_draw_pixel(cx + x, cy + y, color);
                }
            }
        }
    } else {
        // Outline ellipse using parametric equation
        for (int angle = 0; angle < 360; angle++) {
            int32_t x = cx + (int32_t)(rx * dks_cos(angle * 3.14159 / 180.0));
            int32_t y = cy + (int32_t)(ry * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x, y, color);
        }
    }
    print("Ellipse drawn.\n");
}

static void dks_draw_polygon(char* args) {
    if (!args) { print("Usage: draw_polygon <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return; }
    // Simple implementation - draw triangle for now
    char* x1_str = strtok(args, " ");
    char* y1_str = strtok(NULL, " ");
    char* x2_str = strtok(NULL, " ");
    char* y2_str = strtok(NULL, " ");
    char* x3_str = strtok(NULL, " ");
    char* y3_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!x1_str || !y1_str || !x2_str || !y2_str || !x3_str || !y3_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_polygon <x1> <y1> <x2> <y2> <x3> <y3> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t x1 = atoi(x1_str), y1 = atoi(y1_str);
    int32_t x2 = atoi(x2_str), y2 = atoi(y2_str);
    int32_t x3 = atoi(x3_str), y3 = atoi(y3_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    if (filled) {
        // Simple filled triangle using scanline algorithm
        // Sort vertices by y
        int32_t tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2, tx3 = x3, ty3 = y3;
        // Bubble sort by y
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        if (ty2 > ty3) { int32_t t = tx2; tx2 = tx3; tx3 = t; t = ty2; ty2 = ty3; ty3 = t; }
        if (ty1 > ty2) { int32_t t = tx1; tx1 = tx2; tx2 = t; t = ty1; ty1 = ty2; ty2 = t; }
        
        // Draw filled triangle
        for (int32_t y = ty1; y <= ty3; y++) {
            int32_t x_start, x_end;
            if (y < ty2) {
                // Top half
                x_start = tx1 + (tx2 - tx1) * (y - ty1) / (ty2 - ty1);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            } else {
                // Bottom half
                x_start = tx2 + (tx3 - tx2) * (y - ty2) / (ty3 - ty2);
                x_end = tx1 + (tx3 - tx1) * (y - ty1) / (ty3 - ty1);
            }
            if (x_start > x_end) { int32_t t = x_start; x_start = x_end; x_end = t; }
            for (int32_t x = x_start; x <= x_end; x++) {
                graphics_draw_pixel(x, y, color);
            }
        }
    } else {
        graphics_draw_line(x1, y1, x2, y2, color);
        graphics_draw_line(x2, y2, x3, y3, color);
        graphics_draw_line(x3, y3, x1, y1, color);
    }
    print("Polygon drawn.\n");
}

static void dks_draw_gradient(char* args) {
    if (!args) { print("Usage: draw_gradient <x> <y> <width> <height> <r1> <g1> <b1> <r2> <g2> <b2> [vertical]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* r1_str = strtok(NULL, " ");
    char* g1_str = strtok(NULL, " ");
    char* b1_str = strtok(NULL, " ");
    char* r2_str = strtok(NULL, " ");
    char* g2_str = strtok(NULL, " ");
    char* b2_str = strtok(NULL, " ");
    char* vertical_str = strtok(NULL, " ");
    
    if (!x_str || !y_str || !w_str || !h_str || !r1_str || !g1_str || !b1_str || !r2_str || !g2_str || !b2_str) {
        print("Usage: draw_gradient <x> <y> <width> <height> <r1> <g1> <b1> <r2> <g2> <b2> [vertical]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint8 r1 = atoi(r1_str), g1 = atoi(g1_str), b1 = atoi(b1_str);
    uint8 r2 = atoi(r2_str), g2 = atoi(g2_str), b2 = atoi(b2_str);
    bool vertical = vertical_str && strcmp(vertical_str, "vertical") == 0;
    
    if (vertical) {
        for (uint32_t i = 0; i < h; i++) {
            uint8 r = r1 + (r2 - r1) * i / h;
            uint8 g = g1 + (g2 - g1) * i / h;
            uint8 b = b1 + (b2 - b1) * i / h;
            graphics_color_t color = graphics_make_color(r, g, b, 255);
            graphics_draw_line(x, y + i, x + w - 1, y + i, color);
        }
    } else {
        for (uint32_t i = 0; i < w; i++) {
            uint8 r = r1 + (r2 - r1) * i / w;
            uint8 g = g1 + (g2 - g1) * i / w;
            uint8 b = b1 + (b2 - b1) * i / w;
            graphics_color_t color = graphics_make_color(r, g, b, 255);
            graphics_draw_line(x + i, y, x + i, y + h - 1, color);
        }
    }
    print("Gradient drawn.\n");
}

static void dks_draw_image(char* args) {
    if (!args) { print("Usage: draw_image <path> <x> <y> [scale_width] [scale_height]\n"); return; }
    char* path = strtok(args, " ");
    char* x_str = strtok(NULL, " ");
    char* y_str = strtok(NULL, " ");
    char* sw_str = strtok(NULL, " ");
    char* sh_str = strtok(NULL, " ");
    
    if (!path || !x_str || !y_str) {
        print("Usage: draw_image <path> <x> <y> [scale_width] [scale_height]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t sw = sw_str ? atoi(sw_str) : 0;
    uint32_t sh = sh_str ? atoi(sh_str) : 0;
    
    bmp_image_t* image = NULL;
    bmp_result_t result = bmp_load_from_file(path, &image);
    if (result == BMP_SUCCESS && image) {
        if (sw > 0 && sh > 0) {
            bmp_draw_image_scaled(image, x, y, sw, sh);
        } else {
            bmp_draw_image(image, x, y);
        }
        bmp_free(image);
        print("Image drawn.\n");
    } else {
        print("Failed to load image: "); print(path); print("\n");
    }
}

static void dks_draw_rounded_rect(char* args) {
    if (!args) { print("Usage: draw_rounded_rect <x> <y> <width> <height> <radius> <r> <g> <b> [filled]\n"); return; }
    char* x_str = strtok(args, " ");
    char* y_str = strtok(NULL, " ");
    char* w_str = strtok(NULL, " ");
    char* h_str = strtok(NULL, " ");
    char* rad_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    char* filled_str = strtok(NULL, " ");
    
    if (!x_str || !y_str || !w_str || !h_str || !rad_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_rounded_rect <x> <y> <width> <height> <radius> <r> <g> <b> [filled]\n"); return;
    }
    
    int32_t x = atoi(x_str), y = atoi(y_str);
    uint32_t w = atoi(w_str), h = atoi(h_str);
    uint32_t radius = atoi(rad_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    bool filled = filled_str && strcmp(filled_str, "filled") == 0;
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    // Simple rounded rect - draw main rect and rounded corners
    if (filled) {
        // Fill main rectangle
        graphics_rect_t rect = {x + radius, y, w - 2*radius, h};
        graphics_draw_rect(&rect, color, true);
        rect = (graphics_rect_t){x, y + radius, w, h - 2*radius};
        graphics_draw_rect(&rect, color, true);
        // Fill corner circles
        for (uint32_t i = 0; i < radius; i++) {
            for (uint32_t j = 0; j < radius; j++) {
                if (i*i + j*j <= radius*radius) {
                    graphics_draw_pixel(x + radius - i, y + radius - j, color);
                    graphics_draw_pixel(x + w - radius + i, y + radius - j, color);
                    graphics_draw_pixel(x + radius - i, y + h - radius + j, color);
                    graphics_draw_pixel(x + w - radius + i, y + h - radius + j, color);
                }
            }
        }
    } else {
        // Draw outline
        graphics_draw_line(x + radius, y, x + w - radius, y, color);
        graphics_draw_line(x + radius, y + h, x + w - radius, y + h, color);
        graphics_draw_line(x, y + radius, x, y + h - radius, color);
        graphics_draw_line(x + w, y + radius, x + w, y + h - radius, color);
        // Draw rounded corners
        for (int angle = 0; angle < 90; angle++) {
            int32_t dx = (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
            int32_t dy = (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
            graphics_draw_pixel(x + radius - dx, y + radius - dy, color);
            graphics_draw_pixel(x + w - radius + dx, y + radius - dy, color);
            graphics_draw_pixel(x + radius - dx, y + h - radius + dy, color);
            graphics_draw_pixel(x + w - radius + dx, y + h - radius + dy, color);
        }
    }
    print("Rounded rectangle drawn.\n");
}

static void dks_draw_arc(char* args) {
    if (!args) { print("Usage: draw_arc <center_x> <center_y> <radius> <start_angle> <end_angle> <r> <g> <b>\n"); return; }
    char* cx_str = strtok(args, " ");
    char* cy_str = strtok(NULL, " ");
    char* rad_str = strtok(NULL, " ");
    char* sa_str = strtok(NULL, " ");
    char* ea_str = strtok(NULL, " ");
    char* r_str = strtok(NULL, " ");
    char* g_str = strtok(NULL, " ");
    char* b_str = strtok(NULL, " ");
    
    if (!cx_str || !cy_str || !rad_str || !sa_str || !ea_str || !r_str || !g_str || !b_str) {
        print("Usage: draw_arc <center_x> <center_y> <radius> <start_angle> <end_angle> <r> <g> <b>\n"); return;
    }
    
    int32_t cx = atoi(cx_str), cy = atoi(cy_str);
    uint32_t radius = atoi(rad_str);
    int start_angle = atoi(sa_str), end_angle = atoi(ea_str);
    uint8 r = atoi(r_str), g = atoi(g_str), b = atoi(b_str);
    
    graphics_color_t color = graphics_make_color(r, g, b, 255);
    
    for (int angle = start_angle; angle <= end_angle; angle++) {
        int32_t x = cx + (int32_t)(radius * dks_cos(angle * 3.14159 / 180.0));
        int32_t y = cy + (int32_t)(radius * dks_sin(angle * 3.14159 / 180.0));
        graphics_draw_pixel(x, y, color);
    }
    print("Arc drawn.\n");
}


void dks_run(void) {
    print("\n[DKS] Direct Kernel Shell online. Type 'help' for commands.\n");

    // Register mouse event callback
    ps2_mouse_register_event_callback(dks_mouse_event_handler);

    // Initialize cursor position to center of screen
    video_mode_t current_mode;
    if (graphics_get_current_mode(&current_mode) == GRAPHICS_SUCCESS) {
        dks_gfx_screen_width = current_mode.width;
        dks_gfx_screen_height = current_mode.height;
        cursor_x = dks_gfx_screen_width / 2;
        cursor_y = dks_gfx_screen_height / 2;
    }
    
    while (1) {
        char prompt[MAX_CWD_LEN + 6];
        strcpy(prompt, "dks:/");
        strcat(prompt, dks_cwd);
        strcat(prompt, "> ");
        print(prompt);
        
        string input_str_const = dks_readStr_enhanced();
        if (!input_str_const) {
            print("[DKS] Input error.\n");
            continue;
        }
        
        char* input_str = strdup(input_str_const);
        add_to_history(input_str);

        char* command = strtok(input_str, " ");
        char* args = strtok(NULL, "");

        if (!command || strlen(command) == 0) {
            free(input_str);
            continue;
        }

        if (strcmp(command, "help") == 0) dks_print_help();
        else if (strcmp(command, "mem") == 0) dks_dump_memory();
        else if (strcmp(command, "cls") == 0 || strcmp(command, "clear") == 0) {
            if (tty_is_ready()) {
                tty_clear();
            } else {
                graphics_clear_screen(COLOR_BLACK);
            }
        }
        else if (strcmp(command, "ls") == 0) dks_ls(args);
        else if (strcmp(command, "cat") == 0) dks_cat(args);
        else if (strcmp(command, "run") == 0) dks_run_program(args);
        else if (strcmp(command, "tui") == 0) dks_tui_demo();
        else if (strcmp(command, "cpuid") == 0) dks_show_cpuid();
        else if (strcmp(command, "panic") == 0) kernel_panic("User-triggered test panic");
        else if (strcmp(command, "shell") == 0) {
            if (!shell_launch_embedded()) print("[DKS] Shell launch failed.\n");
            else return;
        } else if (strcmp(command, "halt") == 0) {
            print("[DKS] Halting CPU.\n");
            __asm__ __volatile__("cli; hlt");
        } else if (strcmp(command, "shutdown") == 0) {
            if (!power_shutdown()) print("[DKS] Shutdown failed.\n");
        } else if (strcmp(command, "reboot") == 0) {
            if (!power_reboot()) print("[DKS] Reboot failed.\n");
        } else if (strcmp(command, "cd") == 0) dks_cd(args);
        else if (strcmp(command, "pwd") == 0) dks_pwd();
        else if (strcmp(command, "ps") == 0) dks_ps();
        else if (strcmp(command, "kill") == 0) dks_kill(args);
        else if (strcmp(command, "head") == 0) dks_head(args);
        else if (strcmp(command, "tail") == 0) dks_tail(args);
        else if (strcmp(command, "wc") == 0) dks_wc(args);
        else if (strcmp(command, "echo") == 0) { if(args) print(args); print("\n");}
        else if (strcmp(command, "whoami") == 0) print("root\n");
        else if (strcmp(command, "sleep") == 0) { if(args) sleep_interruptible(atoi(args)); }
        else if (strcmp(command, "history") == 0) dks_print_history();
        else if (strcmp(command, "wait") == 0) { if(args) dks_wait(atoi(args)); }
        else if (strcmp(command, "kb_locale_change") == 0) { dks_kb_locale_change(args); }
        else if (strcmp(command, "kb_locale_list") == 0) { dks_kb_locale_list(); }
        else if (strcmp(command, "pixel") == 0) dks_pixel(args);
        else if (strcmp(command, "rect") == 0) dks_rect(args);
        else if (strcmp(command, "line") == 0) dks_line(args);
        else if (strcmp(command, "mouse") == 0) dks_mouse_pointer(args);
        else if (strcmp(command, "fill_screen") == 0) dks_fill_screen(args);
        else if (strcmp(command, "clear_area") == 0) dks_clear_area(args);
        else if (strcmp(command, "circle") == 0) dks_draw_circle(args);
        else if (strcmp(command, "triangle") == 0) dks_draw_triangle(args);
        else if (strcmp(command, "draw_string") == 0) dks_draw_string(args);
        else if (strcmp(command, "draw_window") == 0) dks_draw_window(args);
        else if (strcmp(command, "window_move") == 0) dks_window_move(args);
        else if (strcmp(command, "window_add_button") == 0) dks_window_add_button(args);
        else if (strcmp(command, "window_add_text") == 0) dks_window_add_text(args);
        else if (strcmp(command, "window_add_input") == 0) dks_window_add_input(args);
        else if (strcmp(command, "guimode") == 0 || strcmp(command, "GUIM") == 0 || strcmp(command, "Guimode") == 0) dks_guimode(args);
        else if (strcmp(command, "swap_buffers") == 0) dks_swap_buffers(args);
        else if (strcmp(command, "enable_double_buffering") == 0) dks_enable_double_buffering(args);
        else if (strcmp(command, "wait_vsync") == 0) dks_wait_vsync(args);
        else if (strcmp(command, "set_resolution") == 0) dks_set_resolution(args);
        else if (strcmp(command, "draw_ellipse") == 0) dks_draw_ellipse(args);
        else if (strcmp(command, "draw_polygon") == 0) dks_draw_polygon(args);
        else if (strcmp(command, "draw_gradient") == 0) dks_draw_gradient(args);
        else if (strcmp(command, "draw_image") == 0) dks_draw_image(args);
        else if (strcmp(command, "draw_rounded_rect") == 0) dks_draw_rounded_rect(args);
        else if (strcmp(command, "draw_arc") == 0) dks_draw_arc(args);
        else if (strcmp(command, "res_list") == 0) dks_res_list();
        else if (strcmp(command, "get_resolution") == 0) dks_get_resolution();
        else if (strcmp(command, "draw_bezier") == 0) dks_draw_bezier(args);
        else if (strcmp(command, "draw_star") == 0) dks_draw_star(args);
        else if (strcmp(command, "draw_thick_line") == 0) dks_draw_thick_line(args);
        else if (strcmp(command, "draw_dotted_line") == 0) dks_draw_dotted_line(args);
        else if (strcmp(command, "draw_crosshair") == 0) dks_draw_crosshair(args);
        else if (strcmp(command, "draw_grid") == 0) dks_draw_grid(args);
        else if (strcmp(command, "draw_checker") == 0) dks_draw_checker(args);
        else {
            print("[DKS] Unknown command '");
            print(command);
            print("'. Type 'help'.\n");
        }
        free(input_str);
    }
}
