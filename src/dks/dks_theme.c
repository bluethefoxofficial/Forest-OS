/*
 * DKS Theme Engine
 * Provides theming support for the DKS desktop environment
 */

#include "../include/dks/dks_theme.h"
#include <string.h>

// Current active theme
static dks_theme_t current_theme;
static bool theme_initialized = false;

// Predefined dark theme
static const dks_theme_t dark_theme = {
    // Main colors
    .primary_color = {70, 130, 220, 255},
    .secondary_color = {90, 150, 240, 255},
    .background_color = {30, 30, 35, 255},
    .surface_color = {45, 45, 50, 255},
    .text_color = {240, 240, 245, 255},
    .text_secondary = {160, 160, 170, 255},
    .border_color = {70, 70, 80, 255},
    .shadow_color = {0, 0, 0, 100},

    // Window decorations
    .titlebar_active = {50, 50, 60, 255},
    .titlebar_inactive = {40, 40, 45, 255},
    .titlebar_text = {240, 240, 245, 255},
    .close_button_color = {180, 60, 60, 255},
    .close_button_hover = {220, 70, 70, 255},
    .minimize_button_color = {180, 160, 40, 255},
    .minimize_button_hover = {200, 180, 50, 255},
    .maximize_button_color = {60, 160, 60, 255},
    .maximize_button_hover = {70, 180, 70, 255},

    // Panel colors
    .panel_background = {26, 26, 31, 255},
    .panel_text = {220, 220, 225, 255},
    .panel_hover = {60, 60, 70, 255},
    .panel_active = {70, 130, 220, 255},
    .panel_border = {50, 50, 55, 255},

    // Menu colors
    .menu_background = {45, 45, 50, 255},
    .menu_hover = {70, 130, 220, 255},
    .menu_text = {240, 240, 245, 255},
    .menu_text_disabled = {100, 100, 110, 255},
    .menu_separator = {70, 70, 80, 255},
    .menu_border = {60, 60, 70, 255},

    // Button colors
    .button_background = {60, 60, 70, 255},
    .button_hover = {75, 75, 85, 255},
    .button_pressed = {50, 50, 60, 255},
    .button_disabled = {45, 45, 50, 255},
    .button_text = {240, 240, 245, 255},
    .button_border = {80, 80, 90, 255},

    // Input field colors
    .input_background = {35, 35, 40, 255},
    .input_border = {70, 70, 80, 255},
    .input_border_focus = {70, 130, 220, 255},
    .input_text = {240, 240, 245, 255},
    .input_placeholder = {120, 120, 130, 255},
    .input_selection = {70, 130, 220, 128},

    // Scrollbar colors
    .scrollbar_track = {40, 40, 45, 255},
    .scrollbar_thumb = {80, 80, 90, 255},
    .scrollbar_thumb_hover = {100, 100, 110, 255},

    // Desktop icon colors
    .icon_text = {255, 255, 255, 255},
    .icon_text_shadow = {0, 0, 0, 180},
    .icon_selection = {70, 130, 220, 128},

    // Geometry
    .corner_radius = 6,
    .shadow_blur = 8,
    .shadow_offset_x = 2,
    .shadow_offset_y = 4,
    .border_width = 1,
    .titlebar_height = 28,
    .panel_height = 36,
    .menu_item_height = 28,
    .button_padding = 8,
    .widget_spacing = 4,

    // Font settings
    .font_name = "default",
    .font_size = 8,
    .title_font_size = 8,
    .small_font_size = 8,

    // Features
    .enable_shadows = true,
    .enable_gradients = true,
    .enable_transparency = true,
    .enable_animations = false
};

// Predefined light theme
static const dks_theme_t light_theme = {
    // Main colors
    .primary_color = {0, 120, 212, 255},
    .secondary_color = {0, 150, 255, 255},
    .background_color = {240, 240, 245, 255},
    .surface_color = {255, 255, 255, 255},
    .text_color = {26, 26, 26, 255},
    .text_secondary = {102, 102, 102, 255},
    .border_color = {200, 200, 205, 255},
    .shadow_color = {0, 0, 0, 40},

    // Window decorations
    .titlebar_active = {255, 255, 255, 255},
    .titlebar_inactive = {240, 240, 245, 255},
    .titlebar_text = {26, 26, 26, 255},
    .close_button_color = {200, 60, 60, 255},
    .close_button_hover = {232, 17, 35, 255},
    .minimize_button_color = {200, 200, 200, 255},
    .minimize_button_hover = {180, 180, 180, 255},
    .maximize_button_color = {200, 200, 200, 255},
    .maximize_button_hover = {180, 180, 180, 255},

    // Panel colors
    .panel_background = {245, 245, 250, 255},
    .panel_text = {26, 26, 26, 255},
    .panel_hover = {220, 220, 230, 255},
    .panel_active = {0, 120, 212, 255},
    .panel_border = {200, 200, 205, 255},

    // Menu colors
    .menu_background = {255, 255, 255, 255},
    .menu_hover = {0, 120, 212, 255},
    .menu_text = {26, 26, 26, 255},
    .menu_text_disabled = {160, 160, 160, 255},
    .menu_separator = {220, 220, 225, 255},
    .menu_border = {200, 200, 205, 255},

    // Button colors
    .button_background = {240, 240, 245, 255},
    .button_hover = {225, 225, 230, 255},
    .button_pressed = {200, 200, 210, 255},
    .button_disabled = {245, 245, 250, 255},
    .button_text = {26, 26, 26, 255},
    .button_border = {200, 200, 205, 255},

    // Input field colors
    .input_background = {255, 255, 255, 255},
    .input_border = {200, 200, 205, 255},
    .input_border_focus = {0, 120, 212, 255},
    .input_text = {26, 26, 26, 255},
    .input_placeholder = {160, 160, 160, 255},
    .input_selection = {0, 120, 212, 80},

    // Scrollbar colors
    .scrollbar_track = {240, 240, 245, 255},
    .scrollbar_thumb = {200, 200, 205, 255},
    .scrollbar_thumb_hover = {160, 160, 170, 255},

    // Desktop icon colors
    .icon_text = {26, 26, 26, 255},
    .icon_text_shadow = {255, 255, 255, 180},
    .icon_selection = {0, 120, 212, 80},

    // Geometry (same as dark)
    .corner_radius = 6,
    .shadow_blur = 8,
    .shadow_offset_x = 2,
    .shadow_offset_y = 4,
    .border_width = 1,
    .titlebar_height = 28,
    .panel_height = 36,
    .menu_item_height = 28,
    .button_padding = 8,
    .widget_spacing = 4,

    // Font settings
    .font_name = "default",
    .font_size = 8,
    .title_font_size = 8,
    .small_font_size = 8,

    // Features
    .enable_shadows = true,
    .enable_gradients = true,
    .enable_transparency = true,
    .enable_animations = false
};

// High contrast theme
static const dks_theme_t high_contrast_theme = {
    // Main colors - pure black/white
    .primary_color = {255, 255, 0, 255},
    .secondary_color = {0, 255, 255, 255},
    .background_color = {0, 0, 0, 255},
    .surface_color = {0, 0, 0, 255},
    .text_color = {255, 255, 255, 255},
    .text_secondary = {200, 200, 200, 255},
    .border_color = {255, 255, 255, 255},
    .shadow_color = {0, 0, 0, 0},

    // Window decorations
    .titlebar_active = {0, 0, 128, 255},
    .titlebar_inactive = {0, 0, 0, 255},
    .titlebar_text = {255, 255, 255, 255},
    .close_button_color = {255, 0, 0, 255},
    .close_button_hover = {255, 100, 100, 255},
    .minimize_button_color = {128, 128, 128, 255},
    .minimize_button_hover = {200, 200, 200, 255},
    .maximize_button_color = {0, 128, 0, 255},
    .maximize_button_hover = {0, 200, 0, 255},

    // Panel colors
    .panel_background = {0, 0, 0, 255},
    .panel_text = {255, 255, 255, 255},
    .panel_hover = {0, 0, 128, 255},
    .panel_active = {255, 255, 0, 255},
    .panel_border = {255, 255, 255, 255},

    // Menu colors
    .menu_background = {0, 0, 0, 255},
    .menu_hover = {0, 0, 128, 255},
    .menu_text = {255, 255, 255, 255},
    .menu_text_disabled = {128, 128, 128, 255},
    .menu_separator = {255, 255, 255, 255},
    .menu_border = {255, 255, 255, 255},

    // Button colors
    .button_background = {0, 0, 0, 255},
    .button_hover = {0, 0, 128, 255},
    .button_pressed = {128, 128, 128, 255},
    .button_disabled = {64, 64, 64, 255},
    .button_text = {255, 255, 255, 255},
    .button_border = {255, 255, 255, 255},

    // Input field colors
    .input_background = {0, 0, 0, 255},
    .input_border = {255, 255, 255, 255},
    .input_border_focus = {255, 255, 0, 255},
    .input_text = {255, 255, 255, 255},
    .input_placeholder = {128, 128, 128, 255},
    .input_selection = {0, 0, 128, 255},

    // Scrollbar colors
    .scrollbar_track = {64, 64, 64, 255},
    .scrollbar_thumb = {255, 255, 255, 255},
    .scrollbar_thumb_hover = {255, 255, 0, 255},

    // Desktop icon colors
    .icon_text = {255, 255, 255, 255},
    .icon_text_shadow = {0, 0, 0, 255},
    .icon_selection = {0, 0, 128, 255},

    // Geometry - larger for accessibility
    .corner_radius = 0,
    .shadow_blur = 0,
    .shadow_offset_x = 0,
    .shadow_offset_y = 0,
    .border_width = 2,
    .titlebar_height = 32,
    .panel_height = 40,
    .menu_item_height = 32,
    .button_padding = 10,
    .widget_spacing = 6,

    // Font settings
    .font_name = "default",
    .font_size = 8,
    .title_font_size = 8,
    .small_font_size = 8,

    // Features - disable fancy effects
    .enable_shadows = false,
    .enable_gradients = false,
    .enable_transparency = false,
    .enable_animations = false
};

void dks_theme_init(void) {
    if (theme_initialized) return;

    // Default to dark theme
    memcpy(&current_theme, &dark_theme, sizeof(dks_theme_t));
    theme_initialized = true;
}

dks_theme_t* dks_theme_get_current(void) {
    if (!theme_initialized) {
        dks_theme_init();
    }
    return &current_theme;
}

void dks_theme_set(const dks_theme_t* theme) {
    if (theme) {
        memcpy(&current_theme, theme, sizeof(dks_theme_t));
    }
}

void dks_theme_load_preset(dks_theme_preset_t preset) {
    if (!theme_initialized) {
        dks_theme_init();
    }

    switch (preset) {
        case DKS_THEME_DARK:
            memcpy(&current_theme, &dark_theme, sizeof(dks_theme_t));
            break;
        case DKS_THEME_LIGHT:
            memcpy(&current_theme, &light_theme, sizeof(dks_theme_t));
            break;
        case DKS_THEME_HIGH_CONTRAST:
            memcpy(&current_theme, &high_contrast_theme, sizeof(dks_theme_t));
            break;
        case DKS_THEME_CUSTOM:
            // Keep current theme
            break;
    }
}

void dks_theme_set_accent_color(graphics_color_t color) {
    current_theme.primary_color = color;

    // Adjust related colors
    current_theme.panel_active = color;
    current_theme.menu_hover = color;
    current_theme.input_border_focus = color;

    // Create lighter/darker variants
    current_theme.secondary_color = dks_color_lighten(color, 30);
    current_theme.icon_selection = color;
    current_theme.icon_selection.a = 128;
    current_theme.input_selection = color;
    current_theme.input_selection.a = 80;
}

const dks_theme_t* dks_theme_get_preset(dks_theme_preset_t preset) {
    switch (preset) {
        case DKS_THEME_DARK:
            return &dark_theme;
        case DKS_THEME_LIGHT:
            return &light_theme;
        case DKS_THEME_HIGH_CONTRAST:
            return &high_contrast_theme;
        default:
            return &current_theme;
    }
}

// Color utility functions

graphics_color_t dks_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    graphics_color_t c = {r, g, b, 255};
    return c;
}

graphics_color_t dks_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    graphics_color_t c = {r, g, b, a};
    return c;
}

graphics_color_t dks_color_blend(graphics_color_t fg, graphics_color_t bg, uint8_t alpha) {
    graphics_color_t result;
    uint32_t a = alpha;
    uint32_t inv_a = 255 - alpha;

    result.r = (uint8_t)((fg.r * a + bg.r * inv_a) / 255);
    result.g = (uint8_t)((fg.g * a + bg.g * inv_a) / 255);
    result.b = (uint8_t)((fg.b * a + bg.b * inv_a) / 255);
    result.a = 255;

    return result;
}

graphics_color_t dks_color_lighten(graphics_color_t color, uint8_t amount) {
    graphics_color_t result;
    int r = color.r + amount;
    int g = color.g + amount;
    int b = color.b + amount;

    result.r = (r > 255) ? 255 : (uint8_t)r;
    result.g = (g > 255) ? 255 : (uint8_t)g;
    result.b = (b > 255) ? 255 : (uint8_t)b;
    result.a = color.a;

    return result;
}

graphics_color_t dks_color_darken(graphics_color_t color, uint8_t amount) {
    graphics_color_t result;
    int r = color.r - amount;
    int g = color.g - amount;
    int b = color.b - amount;

    result.r = (r < 0) ? 0 : (uint8_t)r;
    result.g = (g < 0) ? 0 : (uint8_t)g;
    result.b = (b < 0) ? 0 : (uint8_t)b;
    result.a = color.a;

    return result;
}

uint32_t dks_color_to_pixel(graphics_color_t color, pixel_format_t format) {
    switch (format) {
        case PIXEL_FORMAT_RGB_555:
            return ((color.r >> 3) << 10) | ((color.g >> 3) << 5) | (color.b >> 3);

        case PIXEL_FORMAT_RGB_565:
            return ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);

        case PIXEL_FORMAT_RGB_888:
            return (color.r << 16) | (color.g << 8) | color.b;

        case PIXEL_FORMAT_RGBA_8888:
            return (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;

        case PIXEL_FORMAT_BGR_888:
            return (color.b << 16) | (color.g << 8) | color.r;

        case PIXEL_FORMAT_BGRA_8888:
            return (color.a << 24) | (color.b << 16) | (color.g << 8) | color.r;

        default:
            return (color.r << 16) | (color.g << 8) | color.b;
    }
}
