#ifndef DKS_THEME_H
#define DKS_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"

// Theme preset types
typedef enum {
    DKS_THEME_DARK,
    DKS_THEME_LIGHT,
    DKS_THEME_HIGH_CONTRAST,
    DKS_THEME_CUSTOM
} dks_theme_preset_t;

// Complete theme structure
typedef struct dks_theme {
    // Color palette - Main colors
    graphics_color_t primary_color;       // Accent color (buttons, highlights)
    graphics_color_t secondary_color;     // Secondary accent
    graphics_color_t background_color;    // Desktop/window background
    graphics_color_t surface_color;       // Widget surfaces, cards
    graphics_color_t text_color;          // Primary text
    graphics_color_t text_secondary;      // Secondary/disabled text
    graphics_color_t border_color;        // Widget borders
    graphics_color_t shadow_color;        // Shadow color (with alpha)

    // Window decoration colors
    graphics_color_t titlebar_active;
    graphics_color_t titlebar_inactive;
    graphics_color_t titlebar_text;
    graphics_color_t close_button_color;
    graphics_color_t close_button_hover;
    graphics_color_t minimize_button_color;
    graphics_color_t minimize_button_hover;
    graphics_color_t maximize_button_color;
    graphics_color_t maximize_button_hover;

    // Panel/taskbar colors
    graphics_color_t panel_background;
    graphics_color_t panel_text;
    graphics_color_t panel_hover;
    graphics_color_t panel_active;
    graphics_color_t panel_border;

    // Menu colors
    graphics_color_t menu_background;
    graphics_color_t menu_hover;
    graphics_color_t menu_text;
    graphics_color_t menu_text_disabled;
    graphics_color_t menu_separator;
    graphics_color_t menu_border;

    // Button colors
    graphics_color_t button_background;
    graphics_color_t button_hover;
    graphics_color_t button_pressed;
    graphics_color_t button_disabled;
    graphics_color_t button_text;
    graphics_color_t button_border;

    // Input field colors
    graphics_color_t input_background;
    graphics_color_t input_border;
    graphics_color_t input_border_focus;
    graphics_color_t input_text;
    graphics_color_t input_placeholder;
    graphics_color_t input_selection;

    // Scrollbar colors
    graphics_color_t scrollbar_track;
    graphics_color_t scrollbar_thumb;
    graphics_color_t scrollbar_thumb_hover;

    // Desktop icon colors
    graphics_color_t icon_text;
    graphics_color_t icon_text_shadow;
    graphics_color_t icon_selection;

    // Geometry settings
    uint32_t corner_radius;               // 0 for sharp, 4-8 for rounded
    uint32_t shadow_blur;                 // Shadow blur radius
    int32_t shadow_offset_x;
    int32_t shadow_offset_y;
    uint32_t border_width;
    uint32_t titlebar_height;
    uint32_t panel_height;
    uint32_t menu_item_height;
    uint32_t button_padding;
    uint32_t widget_spacing;

    // Font settings
    char font_name[32];
    uint8_t font_size;
    uint8_t title_font_size;
    uint8_t small_font_size;

    // Feature flags
    bool enable_shadows;
    bool enable_gradients;
    bool enable_transparency;
    bool enable_animations;

} dks_theme_t;

// Theme API
void dks_theme_init(void);
dks_theme_t* dks_theme_get_current(void);
void dks_theme_set(const dks_theme_t* theme);
void dks_theme_load_preset(dks_theme_preset_t preset);
void dks_theme_set_accent_color(graphics_color_t color);
const dks_theme_t* dks_theme_get_preset(dks_theme_preset_t preset);

// Color utility functions
graphics_color_t dks_color_rgb(uint8_t r, uint8_t g, uint8_t b);
graphics_color_t dks_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
graphics_color_t dks_color_blend(graphics_color_t fg, graphics_color_t bg, uint8_t alpha);
graphics_color_t dks_color_lighten(graphics_color_t color, uint8_t amount);
graphics_color_t dks_color_darken(graphics_color_t color, uint8_t amount);
uint32_t dks_color_to_pixel(graphics_color_t color, pixel_format_t format);

#endif // DKS_THEME_H
