#ifndef DKS_DRAW_H
#define DKS_DRAW_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "dks_theme.h"

// Drawing context flags
typedef enum {
    DRAW_FLAG_NONE = 0,
    DRAW_FLAG_FILLED = (1 << 0),
    DRAW_FLAG_ANTIALIAS = (1 << 1),
    DRAW_FLAG_GRADIENT_H = (1 << 2),
    DRAW_FLAG_GRADIENT_V = (1 << 3)
} draw_flags_t;

// Gradient direction
typedef enum {
    GRADIENT_HORIZONTAL,
    GRADIENT_VERTICAL,
    GRADIENT_DIAGONAL_TL,
    GRADIENT_DIAGONAL_TR
} gradient_dir_t;

// Basic drawing primitives
void dks_draw_pixel(graphics_surface_t* surface, int32_t x, int32_t y, graphics_color_t color);
void dks_draw_line(graphics_surface_t* surface, int32_t x1, int32_t y1, int32_t x2, int32_t y2, graphics_color_t color);
void dks_draw_line_thick(graphics_surface_t* surface, int32_t x1, int32_t y1, int32_t x2, int32_t y2, graphics_color_t color, uint32_t thickness);
void dks_draw_hline(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, graphics_color_t color);
void dks_draw_vline(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, graphics_color_t color);

// Rectangle drawing
void dks_draw_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, bool filled);
void dks_draw_rect_outline(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t thickness);
void dks_fill_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color);
void dks_clear_rect(graphics_surface_t* surface, const graphics_rect_t* rect);

// Rounded rectangle drawing
void dks_draw_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius, bool filled);
void dks_draw_rounded_rect_outline(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius, uint32_t thickness);
void dks_fill_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color, uint32_t radius);

// Gradient rectangles
void dks_draw_gradient_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color1, graphics_color_t color2, gradient_dir_t direction);
void dks_draw_gradient_rounded_rect(graphics_surface_t* surface, const graphics_rect_t* rect, graphics_color_t color1, graphics_color_t color2, gradient_dir_t direction, uint32_t radius);

// Circle and ellipse drawing
void dks_draw_circle(graphics_surface_t* surface, int32_t cx, int32_t cy, uint32_t radius, graphics_color_t color, bool filled);
void dks_draw_ellipse(graphics_surface_t* surface, int32_t cx, int32_t cy, uint32_t rx, uint32_t ry, graphics_color_t color, bool filled);

// Shadow drawing (simplified - no actual blur, uses offset rectangles)
void dks_draw_shadow(graphics_surface_t* surface, const graphics_rect_t* rect, uint32_t blur_radius, int32_t offset_x, int32_t offset_y, graphics_color_t shadow_color, uint32_t corner_radius);

// Box shadow (multi-layer for softer effect)
void dks_draw_box_shadow(graphics_surface_t* surface, const graphics_rect_t* rect, const dks_theme_t* theme);

// Text drawing
void dks_draw_text(graphics_surface_t* surface, int32_t x, int32_t y, const char* text, graphics_color_t color);
void dks_draw_text_centered(graphics_surface_t* surface, const graphics_rect_t* bounds, const char* text, graphics_color_t color);
void dks_draw_text_clipped(graphics_surface_t* surface, const graphics_rect_t* bounds, const char* text, graphics_color_t color, alignment_t align);

// Measure text dimensions
void dks_measure_text(const char* text, uint32_t* width, uint32_t* height);
uint32_t dks_text_width(const char* text);
uint32_t dks_text_height(void);
uint32_t dks_char_width(void);

// Icon/image drawing
void dks_draw_icon(graphics_surface_t* surface, const bmp_image_t* icon, int32_t x, int32_t y, uint32_t size);
void dks_draw_icon_centered(graphics_surface_t* surface, const graphics_rect_t* bounds, const bmp_image_t* icon, uint32_t size);

// Common UI elements
void dks_draw_button(graphics_surface_t* surface, const graphics_rect_t* rect, const char* text, uint32_t state, const dks_theme_t* theme);
void dks_draw_checkbox(graphics_surface_t* surface, int32_t x, int32_t y, bool checked, uint32_t state, const dks_theme_t* theme);
void dks_draw_radio(graphics_surface_t* surface, int32_t x, int32_t y, bool selected, uint32_t state, const dks_theme_t* theme);
void dks_draw_textinput(graphics_surface_t* surface, const graphics_rect_t* rect, const char* text, uint32_t cursor_pos, bool focused, const dks_theme_t* theme);
void dks_draw_scrollbar_v(graphics_surface_t* surface, const graphics_rect_t* track, int32_t thumb_pos, int32_t thumb_size, bool hovered, const dks_theme_t* theme);
void dks_draw_scrollbar_h(graphics_surface_t* surface, const graphics_rect_t* track, int32_t thumb_pos, int32_t thumb_size, bool hovered, const dks_theme_t* theme);

// Window control buttons (close, minimize, maximize)
void dks_draw_close_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, uint32_t state, const dks_theme_t* theme);
void dks_draw_minimize_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, uint32_t state, const dks_theme_t* theme);
void dks_draw_maximize_button(graphics_surface_t* surface, int32_t x, int32_t y, uint32_t size, bool maximized, uint32_t state, const dks_theme_t* theme);

// Separator/divider
void dks_draw_separator_h(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, const dks_theme_t* theme);
void dks_draw_separator_v(graphics_surface_t* surface, int32_t x, int32_t y, int32_t length, const dks_theme_t* theme);

// Focus ring
void dks_draw_focus_ring(graphics_surface_t* surface, const graphics_rect_t* rect, const dks_theme_t* theme);

// Surface operations
void dks_surface_clear(graphics_surface_t* surface, graphics_color_t color);
void dks_surface_blit(graphics_surface_t* dest, int32_t dx, int32_t dy, const graphics_surface_t* src, const graphics_rect_t* src_rect);
void dks_surface_blit_scaled(graphics_surface_t* dest, const graphics_rect_t* dest_rect, const graphics_surface_t* src, const graphics_rect_t* src_rect);

// Clipping
void dks_set_clip_rect(graphics_surface_t* surface, const graphics_rect_t* rect);
void dks_clear_clip_rect(graphics_surface_t* surface);

#endif // DKS_DRAW_H
