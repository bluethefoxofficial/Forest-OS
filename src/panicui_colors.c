#include "include/panicui.h"
#include "include/graphics/graphics_manager.h"
#include "include/math.h"
#include "include/libc/stdio.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

// =============================================================================
// COLOR PANEL IMPLEMENTATION
// =============================================================================

void panicui_init_colors_panel(void) {
    panicui_panel_t* panel = &panicui_get_context()->panels[PANICUI_PANEL_COLORS];
    
    panel->content.colors.hsv_square_size = 200;
    panel->content.colors.selected_color = 0xFF0000; // Red
    panel->content.colors.hue = 0.0f;
    panel->content.colors.saturation = 1.0f;
    panel->content.colors.value = 1.0f;
    panel->content.colors.show_ansi_colors = true;
    panel->content.colors.interactive_mode = true;
    
    // Generate ANSI color palette
    panicui_generate_ansi_palette(panel->content.colors.color_palette);
}

void panicui_draw_colors_panel(void* content, graphics_rect_t area) {
    struct {
        uint32_t hsv_square_size;
        uint32_t selected_color;
        float hue;
        float saturation;
        float value;
        bool show_ansi_colors;
        bool interactive_mode;
        graphics_color_t color_palette[256];
    } *colors = content;
    
    panicui_context_t* ctx = panicui_get_context();
    
    int32_t y = area.y;
    int32_t margin = 20;
    int32_t hsv_size = colors->hsv_square_size;
    
    if (ctx->font_normal) {
        panicui_draw_text_with_shadow(area.x, y, "HSV Color Picker & ANSI Visualization", 
                                     ctx->font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += 30;
        
        // Draw HSV color square
        graphics_rect_t hsv_bounds = {area.x, y, hsv_size, hsv_size};
        panicui_draw_hsv_square(hsv_bounds, colors->hue, &colors->saturation, &colors->value);
        
        // Draw hue bar
        graphics_rect_t hue_bounds = {area.x + hsv_size + margin, y, 30, hsv_size};
        panicui_draw_hue_bar(hue_bounds, &colors->hue);
        
        // Draw color preview
        graphics_rect_t preview_bounds = {area.x + hsv_size + margin + 40, y, 80, 80};
        graphics_color_t current_color = panicui_hsv_to_rgb(colors->hue, colors->saturation, colors->value);
        panicui_draw_color_preview(preview_bounds, current_color);
        
        y += hsv_size + margin;
        
        // Draw ANSI color grid
        panicui_draw_text_with_shadow(area.x, y, "ANSI Color Palette:", 
                                     ctx->font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += 25;
        
        graphics_rect_t ansi_bounds = {area.x, y, area.width - margin, 120};
        panicui_draw_ansi_color_grid(ansi_bounds);
        
        // Draw color information
        y += 140;
        char color_info[256];
        sprintf(color_info, "HSV: H=%.1f° S=%.2f V=%.2f", 
                colors->hue, colors->saturation, colors->value);
        panicui_draw_text_with_shadow(area.x, y, color_info, 
                                     ctx->font_small, PANICUI_COLOR_TEXT_SECONDARY);
        y += 20;
        
        sprintf(color_info, "RGB: #%02X%02X%02X", 
                current_color.r, current_color.g, current_color.b);
        panicui_draw_text_with_shadow(area.x, y, color_info, 
                                     ctx->font_small, PANICUI_COLOR_TEXT_SECONDARY);
    }
}

void panicui_draw_hsv_square(graphics_rect_t bounds, float hue, float* selected_s, float* selected_v) {
    // Draw HSV color square with saturation on X-axis and value on Y-axis
    for (uint32_t y = 0; y < bounds.height; y++) {
        for (uint32_t x = 0; x < bounds.width; x++) {
            float s = (float)x / bounds.width;
            float v = 1.0f - ((float)y / bounds.height);
            
            graphics_color_t color = panicui_hsv_to_rgb(hue, s, v);
            graphics_draw_pixel(bounds.x + x, bounds.y + y, color);
        }
    }
    
    // Draw selection indicator
    int sel_x = bounds.x + (int)(*selected_s * bounds.width);
    int sel_y = bounds.y + (int)((1.0f - *selected_v) * bounds.height);
    
    // Draw crosshair selection indicator
    graphics_color_t indicator_color = {255, 255, 255, 255};
    graphics_color_t shadow_color = {0, 0, 0, 128};
    
    // Draw shadow
    graphics_draw_line(sel_x - 8, sel_y + 1, sel_x + 8, sel_y + 1, shadow_color);
    graphics_draw_line(sel_x + 1, sel_y - 8, sel_x + 1, sel_y + 8, shadow_color);
    
    // Draw indicator
    graphics_draw_line(sel_x - 8, sel_y, sel_x + 8, sel_y, indicator_color);
    graphics_draw_line(sel_x, sel_y - 8, sel_x, sel_y + 8, indicator_color);
    
    // Draw border around HSV square
    graphics_rect_t border = bounds;
    panicui_draw_rect_with_border(border, COLOR_TRANSPARENT, PANICUI_COLOR_BORDER, 2);
}

void panicui_draw_hue_bar(graphics_rect_t bounds, float* selected_hue) {
    // Draw vertical hue bar
    for (uint32_t y = 0; y < bounds.height; y++) {
        float hue = ((float)y / bounds.height) * 360.0f;
        graphics_color_t color = panicui_hsv_to_rgb(hue, 1.0f, 1.0f);
        
        for (uint32_t x = 0; x < bounds.width; x++) {
            graphics_draw_pixel(bounds.x + x, bounds.y + y, color);
        }
    }
    
    // Draw hue selection indicator
    int sel_y = bounds.y + (int)((*selected_hue / 360.0f) * bounds.height);
    
    // Draw selection triangle
    graphics_color_t indicator_color = {255, 255, 255, 255};
    graphics_color_t shadow_color = {0, 0, 0, 128};
    
    // Left triangle (shadow)
    graphics_draw_line(bounds.x - 5, sel_y, bounds.x, sel_y - 4, shadow_color);
    graphics_draw_line(bounds.x, sel_y - 4, bounds.x, sel_y + 4, shadow_color);
    graphics_draw_line(bounds.x, sel_y + 4, bounds.x - 5, sel_y, shadow_color);
    
    // Left triangle (main)
    graphics_draw_line(bounds.x - 6, sel_y, bounds.x - 1, sel_y - 4, indicator_color);
    graphics_draw_line(bounds.x - 1, sel_y - 4, bounds.x - 1, sel_y + 4, indicator_color);
    graphics_draw_line(bounds.x - 1, sel_y + 4, bounds.x - 6, sel_y, indicator_color);
    
    // Right triangle (shadow)
    graphics_draw_line(bounds.x + bounds.width + 5, sel_y, bounds.x + bounds.width, sel_y - 4, shadow_color);
    graphics_draw_line(bounds.x + bounds.width, sel_y - 4, bounds.x + bounds.width, sel_y + 4, shadow_color);
    graphics_draw_line(bounds.x + bounds.width, sel_y + 4, bounds.x + bounds.width + 5, sel_y, shadow_color);
    
    // Right triangle (main)
    graphics_draw_line(bounds.x + bounds.width + 6, sel_y, bounds.x + bounds.width + 1, sel_y - 4, indicator_color);
    graphics_draw_line(bounds.x + bounds.width + 1, sel_y - 4, bounds.x + bounds.width + 1, sel_y + 4, indicator_color);
    graphics_draw_line(bounds.x + bounds.width + 1, sel_y + 4, bounds.x + bounds.width + 6, sel_y, indicator_color);
    
    // Draw border
    panicui_draw_rect_with_border(bounds, COLOR_TRANSPARENT, PANICUI_COLOR_BORDER, 2);
}

void panicui_draw_ansi_color_grid(graphics_rect_t bounds) {
    panicui_context_t* ctx = panicui_get_context();
    
    int cols = 16;
    int rows = 16;
    int cell_width = bounds.width / cols;
    int cell_height = bounds.height / rows;
    
    // Standard ANSI colors (0-15)
    graphics_color_t ansi_colors[16] = {
        {0, 0, 0, 255},         // 0: Black
        {128, 0, 0, 255},       // 1: Dark Red
        {0, 128, 0, 255},       // 2: Dark Green
        {128, 128, 0, 255},     // 3: Dark Yellow
        {0, 0, 128, 255},       // 4: Dark Blue
        {128, 0, 128, 255},     // 5: Dark Magenta
        {0, 128, 128, 255},     // 6: Dark Cyan
        {192, 192, 192, 255},   // 7: Light Gray
        {128, 128, 128, 255},   // 8: Dark Gray
        {255, 0, 0, 255},       // 9: Bright Red
        {0, 255, 0, 255},       // 10: Bright Green
        {255, 255, 0, 255},     // 11: Bright Yellow
        {0, 0, 255, 255},       // 12: Bright Blue
        {255, 0, 255, 255},     // 13: Bright Magenta
        {0, 255, 255, 255},     // 14: Bright Cyan
        {255, 255, 255, 255}    // 15: White
    };
    
    // Draw standard ANSI colors
    for (int i = 0; i < 16; i++) {
        int x = (i % cols) * cell_width;
        int y = 0;
        
        graphics_rect_t cell = {bounds.x + x, bounds.y + y, cell_width - 1, cell_height - 1};
        graphics_draw_rect(&cell, ansi_colors[i], true);
        
        // Draw border
        graphics_draw_rect(&cell, PANICUI_COLOR_BORDER, false);
        
        // Draw color number
        char color_num[4];
        sprintf(color_num, "%d", i);
        
        // Use contrasting text color
        graphics_color_t text_color = (ansi_colors[i].r + ansi_colors[i].g + ansi_colors[i].b > 384) ? 
                                     (graphics_color_t){0, 0, 0, 255} : (graphics_color_t){255, 255, 255, 255};
        
        if (ctx->font_small) {
            graphics_draw_text(cell.x + 2, cell.y + 2, color_num, ctx->font_small, text_color);
        }
    }
    
    // Draw 216 color cube (colors 16-231)
    for (int i = 0; i < 216; i++) {
        int color_index = 16 + i;
        int row = (i / cols) + 1;
        int col = i % cols;
        
        if (row >= rows) break;
        
        int x = col * cell_width;
        int y = row * cell_height;
        
        // Calculate RGB values for 216-color cube
        int cube_index = i;
        int r = (cube_index / 36) * 51;
        int g = ((cube_index % 36) / 6) * 51;
        int b = (cube_index % 6) * 51;
        
        graphics_color_t color = {r, g, b, 255};
        graphics_rect_t cell = {bounds.x + x, bounds.y + y, cell_width - 1, cell_height - 1};
        graphics_draw_rect(&cell, color, true);
        graphics_draw_rect(&cell, PANICUI_COLOR_BORDER, false);
        
        // Show color index for first few rows
        if (row < 4 && ctx->font_small) {
            char color_num[4];
            sprintf(color_num, "%d", color_index);
            graphics_color_t text_color = (r + g + b > 384) ? 
                                         (graphics_color_t){0, 0, 0, 255} : (graphics_color_t){255, 255, 255, 255};
            graphics_draw_text(cell.x + 1, cell.y + 1, color_num, ctx->font_small, text_color);
        }
    }
    
    // Draw grayscale colors (232-255)
    int gray_start_row = 14;
    if (gray_start_row < rows) {
        for (int i = 0; i < 24 && i < cols; i++) {
            int gray_value = 8 + (i * 10);
            int x = i * cell_width;
            int y = gray_start_row * cell_height;
            
            graphics_color_t gray = {gray_value, gray_value, gray_value, 255};
            graphics_rect_t cell = {bounds.x + x, bounds.y + y, cell_width - 1, cell_height - 1};
            graphics_draw_rect(&cell, gray, true);
            graphics_draw_rect(&cell, PANICUI_COLOR_BORDER, false);
            
            // Show color index
            if (ctx->font_small) {
                char color_num[4];
                sprintf(color_num, "%d", 232 + i);
                graphics_color_t text_color = (gray_value > 128) ? 
                                             (graphics_color_t){0, 0, 0, 255} : (graphics_color_t){255, 255, 255, 255};
                graphics_draw_text(cell.x + 1, cell.y + 1, color_num, ctx->font_small, text_color);
            }
        }
    }
}

void panicui_draw_color_preview(graphics_rect_t bounds, graphics_color_t color) {
    panicui_context_t* ctx = panicui_get_context();
    
    // Draw color preview with enhanced styling
    panicui_draw_rect_with_border(bounds, color, PANICUI_COLOR_BORDER, 3);
    
    // Add highlight effect
    graphics_rect_t highlight = {bounds.x + 5, bounds.y + 5, bounds.width - 25, bounds.height - 25};
    graphics_color_t highlight_color = panicui_lighten_color(color, 60);
    highlight_color.a = 128;
    graphics_draw_rect(&highlight, highlight_color, true);
    
    // Add text overlay showing hex value
    char hex_text[10];
    sprintf(hex_text, "#%02X%02X%02X", color.r, color.g, color.b);
    
    graphics_color_t text_color = (color.r + color.g + color.b > 384) ? 
                                 (graphics_color_t){0, 0, 0, 255} : (graphics_color_t){255, 255, 255, 255};
    
    if (ctx->font_small) {
        uint32_t text_width, text_height;
        graphics_get_text_bounds(hex_text, ctx->font_small, &text_width, &text_height);
        int text_x = bounds.x + (bounds.width - text_width) / 2;
        int text_y = bounds.y + bounds.height - text_height - 5;
        panicui_draw_text_with_shadow(text_x, text_y, hex_text, ctx->font_small, text_color);
    }
}

#if ARCH_64BIT
graphics_color_t panicui_hsv_to_rgb(float h, float s, float v) {
    (void)h; (void)s; (void)v;
    /* Simplified fallback without heavy floating point while SSE is disabled */
    return (graphics_color_t){255, 0, 0, 255};
}

void panicui_rgb_to_hsv(graphics_color_t rgb, float* h, float* s, float* v) {
    if (h) *h = 0.0f;
    if (s) *s = 0.0f;
    if (v) *v = ((float)rgb.r) / 255.0f;
}
#else
graphics_color_t panicui_hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
    float m = v - c;
    
    float r1, g1, b1;
    
    if (h >= 0 && h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h >= 60 && h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h >= 120 && h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h >= 180 && h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h >= 240 && h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }
    
    graphics_color_t result;
    result.r = (uint8_t)((r1 + m) * 255);
    result.g = (uint8_t)((g1 + m) * 255);
    result.b = (uint8_t)((b1 + m) * 255);
    result.a = 255;
    
    return result;
}

void panicui_rgb_to_hsv(graphics_color_t rgb, float* h, float* s, float* v) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    
    float max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    float min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    float delta = max - min;
    
    *v = max;
    *s = (max == 0) ? 0 : (delta / max);
    
    if (delta == 0) {
        *h = 0;
    } else if (max == r) {
        *h = 60 * fmod(((g - b) / delta), 6);
    } else if (max == g) {
        *h = 60 * (((b - r) / delta) + 2);
    } else {
        *h = 60 * (((r - g) / delta) + 4);
    }
    
    if (*h < 0) *h += 360;
}
#endif

void panicui_generate_ansi_palette(graphics_color_t* palette) {
    // Generate full 256-color ANSI palette
    // Standard colors (0-15)
    graphics_color_t standard_colors[16] = {
        {0, 0, 0, 255}, {128, 0, 0, 255}, {0, 128, 0, 255}, {128, 128, 0, 255},
        {0, 0, 128, 255}, {128, 0, 128, 255}, {0, 128, 128, 255}, {192, 192, 192, 255},
        {128, 128, 128, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}, {255, 255, 0, 255},
        {0, 0, 255, 255}, {255, 0, 255, 255}, {0, 255, 255, 255}, {255, 255, 255, 255}
    };
    
    for (int i = 0; i < 16; i++) {
        palette[i] = standard_colors[i];
    }
    
    // 216-color cube (16-231)
    for (int i = 0; i < 216; i++) {
        int r = (i / 36) * 51;
        int g = ((i % 36) / 6) * 51;
        int b = (i % 6) * 51;
        palette[16 + i] = (graphics_color_t){r, g, b, 255};
    }
    
    // Grayscale (232-255)
    for (int i = 0; i < 24; i++) {
        int gray = 8 + (i * 10);
        palette[232 + i] = (graphics_color_t){gray, gray, gray, 255};
    }
}

void panicui_handle_color_panel_click(int32_t x, int32_t y) {
    panicui_context_t* ctx = panicui_get_context();
    panicui_panel_t* panel = &ctx->panels[PANICUI_PANEL_COLORS];
    
    if (!panel->base.visible || !panel->active) return;
    
    // Calculate panel content area
    graphics_rect_t content_area = {
        panel->base.bounds.x + 8,
        panel->base.bounds.y + 8 + 30,  // Account for title
        panel->base.bounds.width - 16,
        panel->base.bounds.height - 16
    };
    
    int32_t panel_y = content_area.y;
    int32_t margin = 20;
    int32_t hsv_size = panel->content.colors.hsv_square_size;
    
    // Check for HSV square click
    graphics_rect_t hsv_bounds = {content_area.x, panel_y, hsv_size, hsv_size};
    if (panicui_point_in_rect(x, y, hsv_bounds)) {
        // Update saturation and value based on click position
        float new_s = (float)(x - hsv_bounds.x) / hsv_bounds.width;
        float new_v = 1.0f - ((float)(y - hsv_bounds.y) / hsv_bounds.height);
        
        // Clamp values
        if (new_s < 0.0f) new_s = 0.0f;
        if (new_s > 1.0f) new_s = 1.0f;
        if (new_v < 0.0f) new_v = 0.0f;
        if (new_v > 1.0f) new_v = 1.0f;
        
        panel->content.colors.saturation = new_s;
        panel->content.colors.value = new_v;
        
        // Update selected color
        graphics_color_t new_color = panicui_hsv_to_rgb(
            panel->content.colors.hue,
            panel->content.colors.saturation,
            panel->content.colors.value
        );
        panel->content.colors.selected_color = (new_color.r << 16) | (new_color.g << 8) | new_color.b;
        
        ctx->need_redraw = true;
        return;
    }
    
    // Check for hue bar click
    graphics_rect_t hue_bounds = {content_area.x + hsv_size + margin, panel_y, 30, hsv_size};
    if (panicui_point_in_rect(x, y, hue_bounds)) {
        // Update hue based on click position
        float new_h = ((float)(y - hue_bounds.y) / hue_bounds.height) * 360.0f;
        
        // Clamp hue
        if (new_h < 0.0f) new_h = 0.0f;
        if (new_h >= 360.0f) new_h = 359.9f;
        
        panel->content.colors.hue = new_h;
        
        // Update selected color
        graphics_color_t new_color = panicui_hsv_to_rgb(
            panel->content.colors.hue,
            panel->content.colors.saturation,
            panel->content.colors.value
        );
        panel->content.colors.selected_color = (new_color.r << 16) | (new_color.g << 8) | new_color.b;
        
        ctx->need_redraw = true;
        return;
    }
    
    // Check for ANSI color grid clicks
    int32_t ansi_y = panel_y + hsv_size + margin + 25; // After title
    graphics_rect_t ansi_bounds = {content_area.x, ansi_y, content_area.width - margin, 120};
    
    if (panicui_point_in_rect(x, y, ansi_bounds)) {
        int cols = 16;
        int rows = 16;
        int cell_width = ansi_bounds.width / cols;
        int cell_height = ansi_bounds.height / rows;
        
        int clicked_col = (x - ansi_bounds.x) / cell_width;
        int clicked_row = (y - ansi_bounds.y) / cell_height;
        
        if (clicked_col >= 0 && clicked_col < cols && clicked_row >= 0 && clicked_row < rows) {
            graphics_color_t selected_ansi_color;
            
            if (clicked_row == 0) {
                // Standard ANSI colors (0-15)
                graphics_color_t ansi_colors[16] = {
                    {0, 0, 0, 255}, {128, 0, 0, 255}, {0, 128, 0, 255}, {128, 128, 0, 255},
                    {0, 0, 128, 255}, {128, 0, 128, 255}, {0, 128, 128, 255}, {192, 192, 192, 255},
                    {128, 128, 128, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}, {255, 255, 0, 255},
                    {0, 0, 255, 255}, {255, 0, 255, 255}, {0, 255, 255, 255}, {255, 255, 255, 255}
                };
                selected_ansi_color = ansi_colors[clicked_col];
            } else if (clicked_row >= 1 && clicked_row < 14) {
                // 216-color cube
                int cube_index = (clicked_row - 1) * cols + clicked_col;
                if (cube_index < 216) {
                    int r = (cube_index / 36) * 51;
                    int g = ((cube_index % 36) / 6) * 51;
                    int b = (cube_index % 6) * 51;
                    selected_ansi_color = (graphics_color_t){r, g, b, 255};
                }
            } else if (clicked_row == 14 && clicked_col < 24) {
                // Grayscale
                int gray = 8 + (clicked_col * 10);
                selected_ansi_color = (graphics_color_t){gray, gray, gray, 255};
            }
            
            // Convert selected ANSI color to HSV and update
            panicui_rgb_to_hsv(selected_ansi_color, 
                              &panel->content.colors.hue,
                              &panel->content.colors.saturation,
                              &panel->content.colors.value);
            
            panel->content.colors.selected_color = (selected_ansi_color.r << 16) | 
                                                  (selected_ansi_color.g << 8) | 
                                                  selected_ansi_color.b;
            
            ctx->need_redraw = true;
        }
    }
}
