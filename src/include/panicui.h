/*
 * Forest OS PanicUI - TTY-Based Kernel Panic Screen
 *
 * A clean, framebuffer-based panic display using TTY for text output.
 * Provides a BSOD-style panic screen with multiple pages of information.
 */

#ifndef PANICUI_H
#define PANICUI_H

#include "types.h"
#include "graphics/graphics_manager.h"
#include "graphics/graphics_types.h"
#include "ps2_mouse.h"

// =============================================================================
// VERSION
// =============================================================================

#define PANICUI_VERSION "2.0"
#define PANICUI_TITLE "Forest OS Kernel Panic"

// =============================================================================
// PANEL/PAGE TYPES (kept for compatibility)
// =============================================================================

typedef enum {
    PANICUI_PANEL_OVERVIEW = 0,
    PANICUI_PANEL_REGISTERS,
    PANICUI_PANEL_MEMORY,
    PANICUI_PANEL_STACK,
    PANICUI_PANEL_SYSTEM,
    PANICUI_PANEL_COLORS,     // Not used in TTY version, kept for compatibility
    PANICUI_PANEL_RECOVERY,   // Not used in TTY version, kept for compatibility
    PANICUI_PANEL_COUNT
} panicui_panel_type_t;

// =============================================================================
// BASIC WIDGET STRUCTURES (kept for compatibility)
// =============================================================================

typedef struct {
    graphics_rect_t bounds;
    bool visible;
    bool enabled;
    bool hovered;
    bool pressed;
    graphics_color_t bg_color;
    graphics_color_t text_color;
    graphics_color_t border_color;
} panicui_widget_t;

typedef struct {
    panicui_widget_t base;
    char text[256];
    font_t* font;
    bool centered;
    uint32_t text_width;
    uint32_t text_height;
} panicui_label_t;

typedef struct {
    panicui_widget_t base;
    char text[64];
    bool active;
    panicui_panel_type_t panel_type;
} panicui_tab_t;

// Simplified context for compatibility
typedef struct {
    bool initialized;
    bool graphics_mode_available;
    uint32_t screen_width;
    uint32_t screen_height;
    graphics_surface_t* main_surface;
    graphics_surface_t* back_buffer;
    font_t* font_large;
    font_t* font_normal;
    font_t* font_small;
} panicui_context_t;

// =============================================================================
// CORE API
// =============================================================================

// Initialize the panic UI system
graphics_result_t panicui_init(void);

// Shutdown and cleanup
void panicui_shutdown(void);

// Check if graphics mode is available for panic display
bool panicui_is_graphics_available(void);

// Display a kernel panic with the given information
// This function does not return - it enters an infinite loop
void panicui_show_panic(const char* message, const char* file, uint32_t line,
                       uint32_t fault_addr, uint32_t error_code);

// Main event loop (called by panicui_show_panic)
void panicui_main_loop(void);

// Render a single frame
void panicui_render_frame(void);

// Handle input events
void panicui_handle_input(void);

// Switch to a specific panel/page
void panicui_switch_to_panel(panicui_panel_type_t panel);

// =============================================================================
// COMPATIBILITY STUBS - These are no-ops in the TTY version
// =============================================================================

// Input handling
void panicui_handle_mouse_event(const ps2_mouse_event_t* event);
void panicui_handle_key_event(uint32_t keycode);
bool panicui_point_in_rect(int32_t x, int32_t y, graphics_rect_t rect);
panicui_widget_t* panicui_get_widget_at_point(int32_t x, int32_t y);

// Panel management
void panicui_update_panel_content(panicui_panel_type_t panel);
void panicui_scroll_panel(panicui_panel_type_t panel, int32_t delta_x, int32_t delta_y);

// Drawing functions (stubs)
void panicui_draw_window_frame(void);
void panicui_draw_titlebar(void);
void panicui_draw_tabs(void);
void panicui_draw_panel(panicui_panel_type_t panel);
void panicui_draw_statusbar(void);
void panicui_draw_cursor(void);

// Utility functions
void panicui_draw_rect_with_border(graphics_rect_t rect, graphics_color_t bg,
                                  graphics_color_t border, uint32_t border_width);
void panicui_draw_text_with_shadow(int32_t x, int32_t y, const char* text,
                                  font_t* font, graphics_color_t color);
void panicui_draw_button(graphics_rect_t bounds, const char* text, bool pressed, bool hovered);
graphics_rect_t panicui_get_text_bounds(const char* text, font_t* font);

// Information collection
void panicui_collect_register_info(void);
void panicui_collect_memory_info(uint32_t fault_address);
void panicui_collect_stack_trace(void);
void panicui_collect_system_info(void);
void panicui_generate_recovery_suggestions(void);

// Color utilities
graphics_color_t panicui_blend_colors(graphics_color_t a, graphics_color_t b, uint8_t alpha);
graphics_color_t panicui_darken_color(graphics_color_t color, uint8_t amount);
graphics_color_t panicui_lighten_color(graphics_color_t color, uint8_t amount);

// Panel drawing (stubs for compatibility)
void panicui_draw_overview_panel(void* content, graphics_rect_t area);
void panicui_draw_registers_panel(void* content, graphics_rect_t area);
void panicui_draw_memory_panel(void* content, graphics_rect_t area);
void panicui_draw_stack_panel(void* content, graphics_rect_t area);
void panicui_draw_system_panel(void* content, graphics_rect_t area);
void panicui_draw_colors_panel(void* content, graphics_rect_t area);
void panicui_draw_recovery_panel(void* content, graphics_rect_t area);

// Color visualization (stubs)
void panicui_draw_hsv_square(graphics_rect_t bounds, float hue, float* selected_s, float* selected_v);
void panicui_draw_hue_bar(graphics_rect_t bounds, float* selected_hue);
void panicui_draw_ansi_color_grid(graphics_rect_t bounds);
void panicui_draw_color_preview(graphics_rect_t bounds, graphics_color_t color);
graphics_color_t panicui_hsv_to_rgb(float h, float s, float v);
void panicui_rgb_to_hsv(graphics_color_t rgb, float* h, float* s, float* v);
void panicui_generate_ansi_palette(graphics_color_t* palette);

// Visual effects (stubs)
void panicui_draw_glow_effect(graphics_rect_t bounds, graphics_color_t color, uint32_t radius);
void panicui_draw_gradient_rect(graphics_rect_t bounds, graphics_color_t start, graphics_color_t end, bool vertical);
void panicui_draw_animated_background(void);
void panicui_draw_particle_system(void);
void panicui_init_effects(void);
void panicui_add_sparkle_effect(int32_t x, int32_t y);
void panicui_draw_sparkles(void);
void panicui_draw_scanlines(void);
void panicui_draw_vignette(void);
void panicui_render_enhanced_frame(void);

// Color panel (stubs)
void panicui_handle_color_panel_click(int32_t x, int32_t y);
void panicui_init_colors_panel(void);

// Help overlay (stubs)
void panicui_show_help_overlay(void);
void panicui_draw_help_overlay(void);

// Context access
panicui_context_t* panicui_get_context(void);

#endif // PANICUI_H
