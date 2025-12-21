#include "include/panicui_gfx.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"

void panic_draw_window(int x, int y, int width, int height, const char* title) {
    // Draw the main window background
    graphics_rect_t bg_rect = {x, y, width, height};
    graphics_draw_rect(&bg_rect, PANIC_BG_COLOR, true);

    // Draw the border
    graphics_rect_t border_rect = {x, y, width, height};
    graphics_draw_rect(&border_rect, PANIC_BORDER_COLOR, false);

    // Draw the title bar
    graphics_rect_t title_bar_rect = {x + 1, y + 1, width - 2, 20};
    graphics_draw_rect(&title_bar_rect, PANIC_BORDER_COLOR, true);

    // Draw the title text
    graphics_draw_text(x + 5, y + 5, title, NULL, PANIC_TITLE_COLOR);
}

void panic_draw_button(int x, int y, int width, int height, const char* text, bool pressed) {
    graphics_color_t top_color;
    graphics_color_t bottom_color;
    if (pressed) {
        top_color = (graphics_color_t){64, 64, 64, 255};
        bottom_color = (graphics_color_t){224, 224, 224, 255};
    } else {
        top_color = (graphics_color_t){224, 224, 224, 255};
        bottom_color = (graphics_color_t){64, 64, 64, 255};
    }
    graphics_color_t bg_color = {192, 192, 192, 255};

    // Draw background
    graphics_rect_t rect = {x, y, width, height};
    graphics_draw_rect(&rect, bg_color, true);

    // Draw 3D effect
    graphics_draw_line(x, y, x + width - 1, y, top_color);
    graphics_draw_line(x, y, x, y + height - 1, top_color);
    graphics_draw_line(x + width - 1, y, x + width - 1, y + height - 1, bottom_color);
    graphics_draw_line(x, y + height - 1, x + width - 1, y + height - 1, bottom_color);

    // Draw text
    uint32_t text_width, text_height;
    graphics_get_text_bounds(text, NULL, &text_width, &text_height);
    int text_x = x + (width - text_width) / 2;
    int text_y = y + (height - text_height) / 2;
    graphics_draw_text(text_x, text_y, text, NULL, (graphics_color_t){0, 0, 0, 255});
}

void panic_draw_textbox(int x, int y, int width, int height, const char* text) {
    graphics_rect_t rect = {x, y, width, height};

    // Draw background
    graphics_draw_rect(&rect, (graphics_color_t){255, 255, 255, 255}, true);

    // Draw border
    graphics_draw_rect(&rect, (graphics_color_t){0, 0, 0, 255}, false);

    // Draw text
    graphics_draw_text(x + 5, y + 5, text, NULL, (graphics_color_t){0, 0, 0, 255});
}
