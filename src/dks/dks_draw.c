/*
 * DKS Drawing Utilities
 * Modern drawing primitives with rounded corners, gradients, and shadows
 */

#include "../include/dks/dks_draw.h"
#include "../include/dks/dks_theme.h"
#include <string.h>

// External font rendering (from existing graphics system)
extern void graphics_draw_char_at(int x, int y, char c, uint32_t color);
extern void graphics_draw_string_at(int x, int y, const char* str, uint32_t color);

// Font dimensions (8x8 or 8x16 depending on font)
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

// Helper to set pixel with bounds checking
static inline void set_pixel(graphics_surface_t* surface, int32_t x, int32_t y, graphics_color_t color) {
    if (!surface || !surface->pixels) return;
    if (x < 0 || y < 0 || (uint32_t)x >= surface->width || (uint32_t)y >= surface->height) return;

    uint8_t* pixels = (uint8_t*)surface->pixels;
    uint32_t offset = y * surface->pitch + x * (surface->bpp / 8);

    switch (surface->format) {
        case PIXEL_FORMAT_BGRA_8888:
            pixels[offset + 0] = color.b;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.r;
            pixels[offset + 3] = color.a;
            break;

        case PIXEL_FORMAT_RGBA_8888:
            pixels[offset + 0] = color.r;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.b;
            pixels[offset + 3] = color.a;
            break;

        case PIXEL_FORMAT_BGR_888:
            pixels[offset + 0] = color.b;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.r;
            break;

        case PIXEL_FORMAT_RGB_888:
            pixels[offset + 0] = color.r;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.b;
            break;

        case PIXEL_FORMAT_RGB_565: {
            uint16_t* p = (uint16_t*)(pixels + offset);
            *p = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
            break;
        }

        default:
            // Assume 32-bit BGRA
            pixels[offset + 0] = color.b;
            pixels[offset + 1] = color.g;
            pixels[offset + 2] = color.r;
            pixels[offset + 3] = color.a;
            break;
    }
}

// Alpha blend a pixel onto the surface
static inline void blend_pixel(graphics_surface_t* surface, int32_t x, int32_t y, graphics_color_t color) {
    if (color.a == 255) {
        set_pixel(surface, x, y, color);
        return;
    }
    if (color.a == 0) return;

    if (!surface || !surface->pixels) return;
    if (x < 0 || y < 0 || (uint32_t)x >= surface->width || (uint32_t)y >= surface->height) return;

    uint8_t* pixels = (uint8_t*)surface->pixels;
    uint32_t offset = y * surface->pitch + x * (surface->bpp / 8);

    // Get background color
    graphics_color_t bg;
    switch (surface->format) {
        case PIXEL_FORMAT_BGRA_8888:
            bg.b = pixels[offset + 0];
            bg.g = pixels[offset + 1];
            bg.r = pixels[offset + 2];
            bg.a = pixels[offset + 3];
            break;
        default:
            bg.b = pixels[offset + 0];
            bg.g = pixels[offset + 1];
            bg.r = pixels[offset + 2];
            bg.a = 255;
            break;
    }

    // Alpha blend
    uint32_t a = color.a;
    uint32_t inv_a = 255 - a;
    graphics_color_t result;
    result.r = (uint8_t)((color.r * a + bg.r * inv_a) / 255);
    result.g = (uint8_t)((color.g * a + bg.g * inv_a) / 255);
    result.b = (uint8_t)((color.b * a + bg.b * inv_a) / 255);
    result.a = 255;

    set_pixel(surface, x, y, result);
}

// Basic drawing primitives

void dks_draw_pixel(graphics_surface_t* surface, int32_t x, int32_t y, graphics_color_t color) {
    blend_pixel(surface, x, y, color);
}

void dks_draw_hline(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, graphics_color_t color) {
    if (length < 0) {
        x += length;
        length = -length;
    }
    for (int32_t i = 0; i < length; i++) {
        blend_pixel(surface, x + i, y, color);
    }
}

void dks_draw_vline(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, graphics_color_t color) {
    if (length < 0) {
        y += length;
        length = -length;
    }
    for (int32_t i = 0; i < length; i++) {
        blend_pixel(surface, x, y + i, color);
    }
}

void dks_draw_line(graphics_surface_t* surface, int32_t x1, int32_t y1, int32_t x2, int32_t y2, graphics_color_t color) {
    // Bresenham's line algorithm
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    int32_t sx = (dx > 0) ? 1 : -1;
    int32_t sy = (dy > 0) ? 1 : -1;

    dx = (dx > 0) ? dx : -dx;
    dy = (dy > 0) ? dy : -dy;

    int32_t err = dx - dy;

    while (1) {
        blend_pixel(surface, x1, y1, color);

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
}

void dks_draw_line_thick(graphics_surface_t* surface, int32_t x1, int32_t y1, int32_t x2, int32_t y2, graphics_color_t color, uint32_t thickness) {
    if (thickness <= 1) {
        dks_draw_line(surface, x1, y1, x2, y2, color);
        return;
    }

    // Draw multiple parallel lines for thickness
    int32_t half = (int32_t)(thickness / 2);
    for (int32_t i = -half; i <= half; i++) {
        // Determine if line is more horizontal or vertical
        int32_t dx = x2 - x1;
        int32_t dy = y2 - y1;

        if ((dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy)) {
            // More horizontal, offset in y
            dks_draw_line(surface, x1, y1 + i, x2, y2 + i, color);
        } else {
            // More vertical, offset in x
            dks_draw_line(surface, x1 + i, y1, x2 + i, y2, color);
        }
    }
}

// Rectangle drawing

void dks_draw_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, bool filled) {
    if (filled) {
        dks_fill_rect(surface, rect, color);
    } else {
        dks_draw_rect_outline(surface, rect, color, 1);
    }
}

void dks_draw_rect_outline(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t thickness) {
    for (uint32_t t = 0; t < thickness; t++) {
        int32_t x = rect->x + t;
        int32_t y = rect->y + t;
        int32_t w = rect->width - 2 * t;
        int32_t h = rect->height - 2 * t;

        if (w <= 0 || h <= 0) break;

        dks_draw_hline(surface, x, y, w, color);
        dks_draw_hline(surface, x, y + h - 1, w, color);
        dks_draw_vline(surface, x, y, h, color);
        dks_draw_vline(surface, x + w - 1, y, h, color);
    }
}

void dks_fill_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color) {
    for (uint32_t y = 0; y < rect->height; y++) {
        dks_draw_hline(surface, rect->x, rect->y + y, rect->width, color);
    }
}

void dks_clear_rect(graphics_surface_t* surface, const graphics_rect_t* rect) {
    graphics_color_t clear = {0, 0, 0, 0};
    for (uint32_t y = 0; y < rect->height; y++) {
        for (uint32_t x = 0; x < rect->width; x++) {
            set_pixel(surface, rect->x + x, rect->y + y, clear);
        }
    }
}

// Rounded rectangle drawing

static void draw_rounded_corner(graphics_surface_t* surface, int32_t cx, int32_t cy,
                                uint32_t radius, int32_t quadrant, graphics_color_t color, bool filled) {
    // Simple circle algorithm for corners
    int32_t r = (int32_t)radius;
    int32_t x = r;
    int32_t y = 0;
    int32_t err = 1 - r;

    while (x >= y) {
        // Each quadrant: 0=top-left, 1=top-right, 2=bottom-right, 3=bottom-left
        int32_t px, py;

        switch (quadrant) {
            case 0: // top-left
                if (filled) {
                    dks_draw_hline(surface, cx - x, cy - y, x, color);
                    if (y != 0) dks_draw_hline(surface, cx - y, cy - x, y, color);
                } else {
                    blend_pixel(surface, cx - x, cy - y, color);
                    blend_pixel(surface, cx - y, cy - x, color);
                }
                break;
            case 1: // top-right
                if (filled) {
                    dks_draw_hline(surface, cx, cy - y, x + 1, color);
                    if (y != 0) dks_draw_hline(surface, cx, cy - x, y + 1, color);
                } else {
                    blend_pixel(surface, cx + x, cy - y, color);
                    blend_pixel(surface, cx + y, cy - x, color);
                }
                break;
            case 2: // bottom-right
                if (filled) {
                    dks_draw_hline(surface, cx, cy + y, x + 1, color);
                    if (y != 0) dks_draw_hline(surface, cx, cy + x, y + 1, color);
                } else {
                    blend_pixel(surface, cx + x, cy + y, color);
                    blend_pixel(surface, cx + y, cy + x, color);
                }
                break;
            case 3: // bottom-left
                if (filled) {
                    dks_draw_hline(surface, cx - x, cy + y, x, color);
                    if (y != 0) dks_draw_hline(surface, cx - y, cy + x, y, color);
                } else {
                    blend_pixel(surface, cx - x, cy + y, color);
                    blend_pixel(surface, cx - y, cy + x, color);
                }
                break;
        }

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void dks_draw_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius, bool filled) {
    if (radius == 0) {
        dks_draw_rect(surface, rect, color, filled);
        return;
    }

    // Clamp radius to half the smaller dimension
    uint32_t max_radius = (rect->width < rect->height ? rect->width : rect->height) / 2;
    if (radius > max_radius) radius = max_radius;

    int32_t x = rect->x;
    int32_t y = rect->y;
    int32_t w = rect->width;
    int32_t h = rect->height;
    int32_t r = (int32_t)radius;

    if (filled) {
        // Fill center rectangle
        graphics_rect_t center = {x, y + r, w, h - 2 * r};
        dks_fill_rect(surface, &center, color);

        // Fill top and bottom strips
        graphics_rect_t top = {x + r, y, w - 2 * r, r};
        dks_fill_rect(surface, &top, color);
        graphics_rect_t bottom = {x + r, y + h - r, w - 2 * r, r};
        dks_fill_rect(surface, &bottom, color);

        // Fill corners
        draw_rounded_corner(surface, x + r, y + r, radius, 0, color, true);
        draw_rounded_corner(surface, x + w - r - 1, y + r, radius, 1, color, true);
        draw_rounded_corner(surface, x + w - r - 1, y + h - r - 1, radius, 2, color, true);
        draw_rounded_corner(surface, x + r, y + h - r - 1, radius, 3, color, true);
    } else {
        // Draw edges
        dks_draw_hline(surface, x + r, y, w - 2 * r, color);
        dks_draw_hline(surface, x + r, y + h - 1, w - 2 * r, color);
        dks_draw_vline(surface, x, y + r, h - 2 * r, color);
        dks_draw_vline(surface, x + w - 1, y + r, h - 2 * r, color);

        // Draw corners
        draw_rounded_corner(surface, x + r, y + r, radius, 0, color, false);
        draw_rounded_corner(surface, x + w - r - 1, y + r, radius, 1, color, false);
        draw_rounded_corner(surface, x + w - r - 1, y + h - r - 1, radius, 2, color, false);
        draw_rounded_corner(surface, x + r, y + h - r - 1, radius, 3, color, false);
    }
}

void dks_draw_rounded_rect_outline(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius, uint32_t thickness) {
    for (uint32_t t = 0; t < thickness; t++) {
        graphics_rect_t inner = {
            rect->x + t,
            rect->y + t,
            rect->width - 2 * t,
            rect->height - 2 * t
        };
        if (inner.width <= 0 || inner.height <= 0) break;
        dks_draw_rounded_rect(surface, &inner, color, radius > t ? radius - t : 0, false);
    }
}

void dks_fill_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius) {
    dks_draw_rounded_rect(surface, rect, color, radius, true);
}

// Gradient rectangles

void dks_draw_gradient_rect(graphics_surface_t* surface, const graphics_rect_t* rect,
                            graphics_color_t color1, graphics_color_t color2, gradient_dir_t direction) {
    if (direction == GRADIENT_HORIZONTAL) {
        for (uint32_t x = 0; x < rect->width; x++) {
            uint8_t t = (uint8_t)((x * 255) / (rect->width > 1 ? rect->width - 1 : 1));
            graphics_color_t c = dks_color_blend(color2, color1, t);
            dks_draw_vline(surface, rect->x + x, rect->y, rect->height, c);
        }
    } else { // GRADIENT_VERTICAL
        for (uint32_t y = 0; y < rect->height; y++) {
            uint8_t t = (uint8_t)((y * 255) / (rect->height > 1 ? rect->height - 1 : 1));
            graphics_color_t c = dks_color_blend(color2, color1, t);
            dks_draw_hline(surface, rect->x, rect->y + y, rect->width, c);
        }
    }
}

void dks_draw_gradient_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect,
                                     graphics_color_t color1, graphics_color_t color2,
                                     gradient_dir_t direction, uint32_t radius) {
    // Simple implementation: draw gradient, then clip with transparency
    // For a proper implementation, we'd need to check each pixel against the rounded rect mask
    // For now, just draw a regular rounded rect and fake the gradient
    dks_fill_rounded_rect(surface, rect, color1, radius);
}

// Circle drawing

void dks_draw_circle(graphics_surface_t* surface, int32_t cx, int32_t cy, uint32_t radius, graphics_color_t color, bool filled) {
    int32_t r = (int32_t)radius;
    int32_t x = r;
    int32_t y = 0;
    int32_t err = 1 - r;

    while (x >= y) {
        if (filled) {
            dks_draw_hline(surface, cx - x, cy + y, 2 * x + 1, color);
            dks_draw_hline(surface, cx - x, cy - y, 2 * x + 1, color);
            dks_draw_hline(surface, cx - y, cy + x, 2 * y + 1, color);
            dks_draw_hline(surface, cx - y, cy - x, 2 * y + 1, color);
        } else {
            blend_pixel(surface, cx + x, cy + y, color);
            blend_pixel(surface, cx - x, cy + y, color);
            blend_pixel(surface, cx + x, cy - y, color);
            blend_pixel(surface, cx - x, cy - y, color);
            blend_pixel(surface, cx + y, cy + x, color);
            blend_pixel(surface, cx - y, cy + x, color);
            blend_pixel(surface, cx + y, cy - x, color);
            blend_pixel(surface, cx - y, cy - x, color);
        }

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void dks_draw_ellipse(graphics_surface_t* surface, int32_t cx, int32_t cy, uint32_t rx, uint32_t ry, graphics_color_t color, bool filled) {
    // Midpoint ellipse algorithm
    int32_t a = rx, b = ry;
    int32_t x = 0, y = b;
    int32_t a2 = a * a, b2 = b * b;
    int32_t d1 = b2 - a2 * b + a2 / 4;

    while (a2 * y > b2 * x) {
        if (filled) {
            dks_draw_hline(surface, cx - x, cy + y, 2 * x + 1, color);
            dks_draw_hline(surface, cx - x, cy - y, 2 * x + 1, color);
        } else {
            blend_pixel(surface, cx + x, cy + y, color);
            blend_pixel(surface, cx - x, cy + y, color);
            blend_pixel(surface, cx + x, cy - y, color);
            blend_pixel(surface, cx - x, cy - y, color);
        }

        x++;
        if (d1 < 0) {
            d1 += b2 * (2 * x + 1);
        } else {
            y--;
            d1 += b2 * (2 * x + 1) - 2 * a2 * y;
        }
    }

    int32_t d2 = b2 * (x + 1) * (x + 1) + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        if (filled) {
            dks_draw_hline(surface, cx - x, cy + y, 2 * x + 1, color);
            dks_draw_hline(surface, cx - x, cy - y, 2 * x + 1, color);
        } else {
            blend_pixel(surface, cx + x, cy + y, color);
            blend_pixel(surface, cx - x, cy + y, color);
            blend_pixel(surface, cx + x, cy - y, color);
            blend_pixel(surface, cx - x, cy - y, color);
        }

        y--;
        if (d2 > 0) {
            d2 -= 2 * a2 * y + a2;
        } else {
            x++;
            d2 += 2 * b2 * x - 2 * a2 * y + a2;
        }
    }
}

// Shadow drawing

void dks_draw_shadow(graphics_surface_t* surface, const graphics_rect_t* rect,
                     uint32_t blur_radius, int32_t offset_x, int32_t offset_y,
                     graphics_color_t shadow_color, uint32_t corner_radius) {
    // Simple layered shadow effect (not true blur)
    if (blur_radius == 0) {
        graphics_rect_t shadow_rect = {
            rect->x + offset_x,
            rect->y + offset_y,
            rect->width,
            rect->height
        };
        dks_fill_rounded_rect(surface, &shadow_rect, shadow_color, corner_radius);
        return;
    }

    // Draw multiple layers with decreasing opacity
    uint32_t layers = blur_radius;
    for (uint32_t i = 0; i < layers; i++) {
        graphics_rect_t shadow_rect = {
            rect->x + offset_x - (int32_t)i,
            rect->y + offset_y - (int32_t)i,
            rect->width + 2 * i,
            rect->height + 2 * i
        };

        graphics_color_t layer_color = shadow_color;
        layer_color.a = (uint8_t)(shadow_color.a * (layers - i) / (layers * 2));

        dks_draw_rounded_rect(surface, &shadow_rect, layer_color, corner_radius + i, false);
    }
}

void dks_draw_box_shadow(graphics_surface_t* surface, const graphics_rect_t* rect, const dks_theme_t* theme) {
    if (!theme->enable_shadows) return;

    dks_draw_shadow(surface, rect, theme->shadow_blur,
                    theme->shadow_offset_x, theme->shadow_offset_y,
                    theme->shadow_color, theme->corner_radius);
}

// Text drawing

void dks_draw_text(graphics_surface_t* surface, int32_t x, int32_t y, const char* text, graphics_color_t color) {
    if (!text) return;

    uint32_t pixel_color = dks_color_to_pixel(color, surface->format);

    // Use external graphics function
    while (*text) {
        graphics_draw_char_at(x, y, *text, pixel_color);
        x += FONT_WIDTH;
        text++;
    }
}

void dks_draw_text_centered(graphics_surface_t* surface, const graphics_rect_t* bounds, const char* text, graphics_color_t color) {
    if (!text) return;

    uint32_t text_width = strlen(text) * FONT_WIDTH;
    uint32_t text_height = FONT_HEIGHT;

    int32_t x = bounds->x + (bounds->width - text_width) / 2;
    int32_t y = bounds->y + (bounds->height - text_height) / 2;

    dks_draw_text(surface, x, y, text, color);
}

void dks_draw_text_clipped(graphics_surface_t* surface, const graphics_rect_t* bounds, const char* text, graphics_color_t color, alignment_t align) {
    if (!text) return;

    uint32_t text_width = strlen(text) * FONT_WIDTH;
    int32_t x;

    switch (align) {
        case ALIGN_CENTER:
            x = bounds->x + (bounds->width - text_width) / 2;
            break;
        case ALIGN_END:
            x = bounds->x + bounds->width - text_width;
            break;
        default:
            x = bounds->x;
            break;
    }

    int32_t y = bounds->y + (bounds->height - FONT_HEIGHT) / 2;

    // TODO: Actual clipping
    dks_draw_text(surface, x, y, text, color);
}

// Text measurement

void dks_measure_text(const char* text, uint32_t* width, uint32_t* height) {
    if (width) *width = strlen(text) * FONT_WIDTH;
    if (height) *height = FONT_HEIGHT;
}

uint32_t dks_text_width(const char* text) {
    return strlen(text) * FONT_WIDTH;
}

uint32_t dks_text_height(void) {
    return FONT_HEIGHT;
}

uint32_t dks_char_width(void) {
    return FONT_WIDTH;
}

// Icon drawing

void dks_draw_icon(graphics_surface_t* surface, const bmp_image_t* icon, int32_t x, int32_t y, uint32_t size) {
    if (!icon) return;

    // Use BMP drawing with scaling if size differs from icon size
    // For now, just draw at position
    // TODO: Call into bmp_draw_image_scaled
}

void dks_draw_icon_centered(graphics_surface_t* surface, const graphics_rect_t* bounds, const bmp_image_t* icon, uint32_t size) {
    if (!icon) return;

    int32_t x = bounds->x + (bounds->width - size) / 2;
    int32_t y = bounds->y + (bounds->height - size) / 2;

    dks_draw_icon(surface, icon, x, y, size);
}

// Common UI elements

void dks_draw_button(graphics_surface_t* surface, const graphics_rect_t* rect, const char* text, uint32_t state, const dks_theme_t* theme) {
    graphics_color_t bg, border;

    if (state & WIDGET_STATE_DISABLED) {
        bg = theme->button_disabled;
        border = theme->border_color;
    } else if (state & WIDGET_STATE_PRESSED) {
        bg = theme->button_pressed;
        border = theme->primary_color;
    } else if (state & WIDGET_STATE_HOVERED) {
        bg = theme->button_hover;
        border = theme->primary_color;
    } else {
        bg = theme->button_background;
        border = theme->button_border;
    }

    // Draw button background
    dks_fill_rounded_rect(surface, rect, bg, theme->corner_radius);

    // Draw border
    dks_draw_rounded_rect(surface, rect, border, theme->corner_radius, false);

    // Draw text
    graphics_color_t text_color = (state & WIDGET_STATE_DISABLED) ? theme->text_secondary : theme->button_text;
    dks_draw_text_centered(surface, rect, text, text_color);
}

void dks_draw_checkbox(graphics_surface_t* surface, int32_t x, int32_t y, bool checked, uint32_t state, const dks_theme_t* theme) {
    graphics_rect_t box = {x, y, 16, 16};
    graphics_color_t bg = checked ? theme->primary_color : theme->input_background;
    graphics_color_t border = (state & WIDGET_STATE_FOCUSED) ? theme->input_border_focus : theme->input_border;

    // Draw checkbox background
    dks_fill_rounded_rect(surface, &box, bg, 3);
    dks_draw_rounded_rect(surface, &box, border, 3, false);

    // Draw checkmark if checked
    if (checked) {
        graphics_color_t check = {255, 255, 255, 255};
        dks_draw_line(surface, x + 3, y + 8, x + 6, y + 11, check);
        dks_draw_line(surface, x + 6, y + 11, x + 12, y + 5, check);
    }
}

void dks_draw_radio(graphics_surface_t* surface, int32_t x, int32_t y, bool selected, uint32_t state, const dks_theme_t* theme) {
    graphics_color_t bg = theme->input_background;
    graphics_color_t border = (state & WIDGET_STATE_FOCUSED) ? theme->input_border_focus : theme->input_border;

    // Draw radio button circle
    dks_draw_circle(surface, x + 8, y + 8, 7, border, false);
    dks_draw_circle(surface, x + 8, y + 8, 6, bg, true);

    // Draw inner circle if selected
    if (selected) {
        dks_draw_circle(surface, x + 8, y + 8, 4, theme->primary_color, true);
    }
}

void dks_draw_textinput(graphics_surface_t* surface, const graphics_rect_t* rect, const char* text, uint32_t cursor_pos, bool focused, const dks_theme_t* theme) {
    graphics_color_t bg = theme->input_background;
    graphics_color_t border = focused ? theme->input_border_focus : theme->input_border;

    // Draw background
    dks_fill_rounded_rect(surface, rect, bg, theme->corner_radius / 2);
    dks_draw_rounded_rect(surface, rect, border, theme->corner_radius / 2, false);

    // Draw text
    int32_t text_x = rect->x + 4;
    int32_t text_y = rect->y + (rect->height - FONT_HEIGHT) / 2;
    dks_draw_text(surface, text_x, text_y, text ? text : "", theme->input_text);

    // Draw cursor if focused
    if (focused) {
        int32_t cursor_x = text_x + cursor_pos * FONT_WIDTH;
        dks_draw_vline(surface, cursor_x, text_y, FONT_HEIGHT, theme->text_color);
    }
}

void dks_draw_scrollbar_v(graphics_surface_t* surface, const graphics_rect_t* track, int32_t thumb_pos, int32_t thumb_size, bool hovered, const dks_theme_t* theme) {
    // Draw track
    dks_fill_rounded_rect(surface, track, theme->scrollbar_track, 4);

    // Draw thumb
    if (thumb_size > 0) {
        graphics_rect_t thumb = {
            track->x + 2,
            track->y + thumb_pos,
            track->width - 4,
            (uint32_t)thumb_size
        };
        graphics_color_t thumb_color = hovered ? theme->scrollbar_thumb_hover : theme->scrollbar_thumb;
        dks_fill_rounded_rect(surface, &thumb, thumb_color, 3);
    }
}

void dks_draw_scrollbar_h(graphics_surface_t* surface, const graphics_rect_t* track, int32_t thumb_pos, int32_t thumb_size, bool hovered, const dks_theme_t* theme) {
    // Draw track
    dks_fill_rounded_rect(surface, track, theme->scrollbar_track, 4);

    // Draw thumb
    if (thumb_size > 0) {
        graphics_rect_t thumb = {
            track->x + thumb_pos,
            track->y + 2,
            (uint32_t)thumb_size,
            track->height - 4
        };
        graphics_color_t thumb_color = hovered ? theme->scrollbar_thumb_hover : theme->scrollbar_thumb;
        dks_fill_rounded_rect(surface, &thumb, thumb_color, 3);
    }
}

// Window control buttons

void dks_draw_close_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, uint32_t state, const dks_theme_t* theme) {
    graphics_rect_t rect = {x, y, size, size};
    graphics_color_t bg = (state & WIDGET_STATE_HOVERED) ? theme->close_button_hover : theme->close_button_color;

    dks_fill_rounded_rect(surface, &rect, bg, 3);

    // Draw X
    graphics_color_t x_color = {255, 255, 255, 255};
    int32_t margin = size / 4;
    dks_draw_line(surface, x + margin, y + margin, x + size - margin - 1, y + size - margin - 1, x_color);
    dks_draw_line(surface, x + size - margin - 1, y + margin, x + margin, y + size - margin - 1, x_color);
}

void dks_draw_minimize_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, uint32_t state, const dks_theme_t* theme) {
    graphics_rect_t rect = {x, y, size, size};
    graphics_color_t bg = (state & WIDGET_STATE_HOVERED) ? theme->minimize_button_hover : theme->minimize_button_color;

    dks_fill_rounded_rect(surface, &rect, bg, 3);

    // Draw horizontal line
    graphics_color_t line_color = {255, 255, 255, 255};
    int32_t margin = size / 4;
    dks_draw_hline(surface, x + margin, y + size / 2, size - 2 * margin, line_color);
}

void dks_draw_maximize_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, bool maximized, uint32_t state, const dks_theme_t* theme) {
    graphics_rect_t rect = {x, y, size, size};
    graphics_color_t bg = (state & WIDGET_STATE_HOVERED) ? theme->maximize_button_hover : theme->maximize_button_color;

    dks_fill_rounded_rect(surface, &rect, bg, 3);

    // Draw square (or overlapping squares if maximized)
    graphics_color_t line_color = {255, 255, 255, 255};
    int32_t margin = size / 4;

    if (maximized) {
        // Draw two overlapping squares for "restore" icon
        graphics_rect_t sq1 = {x + margin + 2, y + margin, size - 2 * margin - 2, size - 2 * margin - 2};
        graphics_rect_t sq2 = {x + margin, y + margin + 2, size - 2 * margin - 2, size - 2 * margin - 2};
        dks_draw_rect(surface, &sq1, line_color, false);
        dks_draw_rect(surface, &sq2, line_color, false);
    } else {
        graphics_rect_t sq = {x + margin, y + margin, size - 2 * margin, size - 2 * margin};
        dks_draw_rect(surface, &sq, line_color, false);
    }
}

// Separators

void dks_draw_separator_h(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, const dks_theme_t* theme) {
    dks_draw_hline(surface, x, y, length, theme->menu_separator);
}

void dks_draw_separator_v(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, const dks_theme_t* theme) {
    dks_draw_vline(surface, x, y, length, theme->menu_separator);
}

// Focus ring

void dks_draw_focus_ring(graphics_surface_t* surface, const graphics_rect_t* rect, const dks_theme_t* theme) {
    graphics_rect_t outer = {
        rect->x - 2,
        rect->y - 2,
        rect->width + 4,
        rect->height + 4
    };
    graphics_color_t ring_color = theme->primary_color;
    ring_color.a = 128;
    dks_draw_rounded_rect(surface, &outer, ring_color, theme->corner_radius + 2, false);
}

// Surface operations

void dks_surface_clear(graphics_surface_t* surface, graphics_color_t color) {
    if (!surface || !surface->pixels) return;

    graphics_rect_t full = {0, 0, surface->width, surface->height};
    dks_fill_rect(surface, &full, color);
}

void dks_surface_blit(graphics_surface_t* dest, int32_t dx, int32_t dy, const graphics_surface_t* src, const graphics_rect_t* src_rect) {
    if (!dest || !src || !dest->pixels || !src->pixels) return;

    int32_t sx = src_rect ? src_rect->x : 0;
    int32_t sy = src_rect ? src_rect->y : 0;
    uint32_t sw = src_rect ? src_rect->width : src->width;
    uint32_t sh = src_rect ? src_rect->height : src->height;

    for (uint32_t y = 0; y < sh; y++) {
        for (uint32_t x = 0; x < sw; x++) {
            // Get source pixel
            if (sx + x >= src->width || sy + y >= src->height) continue;

            uint32_t src_offset = (sy + y) * src->pitch + (sx + x) * (src->bpp / 8);
            uint8_t* sp = (uint8_t*)src->pixels + src_offset;

            graphics_color_t c;
            c.b = sp[0];
            c.g = sp[1];
            c.r = sp[2];
            c.a = (src->bpp == 32) ? sp[3] : 255;

            blend_pixel(dest, dx + x, dy + y, c);
        }
    }
}

void dks_surface_blit_scaled(graphics_surface_t* dest, const graphics_rect_t* dest_rect, const graphics_surface_t* src, const graphics_rect_t* src_rect) {
    if (!dest || !src || !dest->pixels || !src->pixels) return;

    int32_t sx = src_rect ? src_rect->x : 0;
    int32_t sy = src_rect ? src_rect->y : 0;
    uint32_t sw = src_rect ? src_rect->width : src->width;
    uint32_t sh = src_rect ? src_rect->height : src->height;

    // Nearest neighbor scaling
    for (uint32_t y = 0; y < dest_rect->height; y++) {
        for (uint32_t x = 0; x < dest_rect->width; x++) {
            uint32_t src_x = sx + (x * sw) / dest_rect->width;
            uint32_t src_y = sy + (y * sh) / dest_rect->height;

            if (src_x >= src->width || src_y >= src->height) continue;

            uint32_t src_offset = src_y * src->pitch + src_x * (src->bpp / 8);
            uint8_t* sp = (uint8_t*)src->pixels + src_offset;

            graphics_color_t c;
            c.b = sp[0];
            c.g = sp[1];
            c.r = sp[2];
            c.a = (src->bpp == 32) ? sp[3] : 255;

            blend_pixel(dest, dest_rect->x + x, dest_rect->y + y, c);
        }
    }
}
