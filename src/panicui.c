/*
 * Forest OS PanicUI - Graphical Kernel Panic Interface
 * 
 * A modern, interactive graphical interface for kernel panic handling.
 * Features mouse navigation, tabbed interface, memory visualization,
 * stack trace inspection, and comprehensive debugging information.
 * 
 * This module provides a professional X11-style windowed interface
 * for handling critical system errors in a user-friendly manner.
 */

#include "panicui.h"
#include "system.h"
#include "memory.h"
#include "string.h"
#include "debuglog.h"
#include "kb.h"
#include "util.h"
#include "hardware.h"
#include <stdio.h>
#include "panicui_gfx.h"
#include "panicui_input.h"
#include "panicui_wm.h"

// =============================================================================
// GLOBAL STATE
// =============================================================================

static panicui_context_t g_panicui;
static bool g_panicui_active = false;

// Tab names for the interface
static const char* g_tab_names[PANICUI_PANEL_COUNT] = {
    "Overview",
    "Registers", 
    "Memory",
    "Stack Trace",
    "System Info",
    "Recovery"
};

// =============================================================================
// GRAPHICS PRIMITIVES AND UTILITIES
// =============================================================================

// Draw a rounded rectangle with gradient effect
void panicui_draw_rounded_rect(graphics_rect_t rect, graphics_color_t color, 
                               uint32_t radius, bool gradient) {
    // For simplicity, draw regular rectangles for now
    // TODO: Implement proper rounded corners and gradients
    if (gradient) {
        // Simple vertical gradient effect
        for (uint32_t y = 0; y < rect.height; y++) {
            uint8_t alpha = (uint8_t)(255 * y / rect.height);
            graphics_color_t grad_color = panicui_darken_color(color, alpha / 8);
            graphics_rect_t line_rect = {rect.x, rect.y + y, rect.width, 1};
            graphics_draw_rect(&line_rect, grad_color, true);
        }
    } else {
        graphics_draw_rect(&rect, color, true);
    }
}

// Draw rectangle with border and optional shadow
void panicui_draw_rect_with_border(graphics_rect_t rect, graphics_color_t bg, 
                                  graphics_color_t border, uint32_t border_width) {
    // Draw shadow first (offset by 1 pixel)
    graphics_rect_t shadow_rect = {rect.x + 1, rect.y + 1, rect.width, rect.height};
    graphics_color_t shadow_color = {0, 0, 0, 32};
    graphics_draw_rect(&shadow_rect, shadow_color, true);
    
    // Draw main background
    graphics_draw_rect(&rect, bg, true);
    
    // Draw border
    if (border_width > 0) {
        // Top border
        graphics_rect_t top = {rect.x, rect.y, rect.width, border_width};
        graphics_draw_rect(&top, border, true);
        
        // Bottom border  
        graphics_rect_t bottom = {rect.x, rect.y + rect.height - border_width, 
                                 rect.width, border_width};
        graphics_draw_rect(&bottom, border, true);
        
        // Left border
        graphics_rect_t left = {rect.x, rect.y, border_width, rect.height};
        graphics_draw_rect(&left, border, true);
        
        // Right border
        graphics_rect_t right = {rect.x + rect.width - border_width, rect.y, 
                                border_width, rect.height};
        graphics_draw_rect(&right, border, true);
    }
}

// Draw text with drop shadow for better readability
void panicui_draw_text_with_shadow(int32_t x, int32_t y, const char* text, 
                                  font_t* font, graphics_color_t color) {
    if (!text || !font) return;
    
    // Draw shadow (offset by 1 pixel)
    graphics_color_t shadow_color = {0, 0, 0, 128};
    graphics_draw_text(x + 1, y + 1, text, font, shadow_color);
    
    // Draw main text
    graphics_draw_text(x, y, text, font, color);
}

// Draw a modern-style button with hover and pressed states
void panicui_draw_button(graphics_rect_t bounds, const char* text, bool pressed, bool hovered) {
    graphics_color_t bg_color = PANICUI_COLOR_BG_ACCENT;
    graphics_color_t border_color = PANICUI_COLOR_BORDER;
    
    if (pressed) {
        bg_color = panicui_darken_color(bg_color, 30);
        border_color = PANICUI_COLOR_HIGHLIGHT;
        bounds.x += 1;
        bounds.y += 1;
    } else if (hovered) {
        bg_color = panicui_lighten_color(bg_color, 20);
        border_color = PANICUI_COLOR_HIGHLIGHT;
    }
    
    panicui_draw_rect_with_border(bounds, bg_color, border_color, 1);
    
    // Center text in button
    if (text && g_panicui.font_normal) {
        uint32_t text_width, text_height;
        if (graphics_get_text_bounds(text, g_panicui.font_normal, &text_width, &text_height) == GRAPHICS_SUCCESS) {
            int32_t text_x = bounds.x + (bounds.width - text_width) / 2;
            int32_t text_y = bounds.y + (bounds.height - text_height) / 2;
            panicui_draw_text_with_shadow(text_x, text_y, text, g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        }
    }
}

// Get text bounds for layout calculations
graphics_rect_t panicui_get_text_bounds(const char* text, font_t* font) {
    graphics_rect_t bounds = {0, 0, 0, 0};
    if (text && font) {
        graphics_get_text_bounds(text, font, &bounds.width, &bounds.height);
    }
    return bounds;
}

// Color manipulation utilities
graphics_color_t panicui_blend_colors(graphics_color_t a, graphics_color_t b, uint8_t alpha) {
    graphics_color_t result;
    result.r = (a.r * (255 - alpha) + b.r * alpha) / 255;
    result.g = (a.g * (255 - alpha) + b.g * alpha) / 255;
    result.b = (a.b * (255 - alpha) + b.b * alpha) / 255;
    result.a = (a.a * (255 - alpha) + b.a * alpha) / 255;
    return result;
}

graphics_color_t panicui_darken_color(graphics_color_t color, uint8_t amount) {
    graphics_color_t result = color;
    result.r = (result.r > amount) ? result.r - amount : 0;
    result.g = (result.g > amount) ? result.g - amount : 0;
    result.b = (result.b > amount) ? result.b - amount : 0;
    return result;
}

graphics_color_t panicui_lighten_color(graphics_color_t color, uint8_t amount) {
    graphics_color_t result = color;
    result.r = (result.r + amount < 255) ? result.r + amount : 255;
    result.g = (result.g + amount < 255) ? result.g + amount : 255;
    result.b = (result.b + amount < 255) ? result.b + amount : 255;
    return result;
}

// =============================================================================
// CORE INITIALIZATION AND MANAGEMENT
// =============================================================================

graphics_result_t panicui_init(void) {
    debuglog(DEBUG_INFO, "[PanicUI] Initializing graphics panic interface...\n");
    
    // Clear context
    memset(&g_panicui, 0, sizeof(panicui_context_t));
    
    // Check if graphics subsystem is available
    if (!graphics_is_initialized()) {
        debuglog(DEBUG_WARN, "[PanicUI] Graphics subsystem not initialized\n");
        g_panicui.graphics_mode_available = false;
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Get current video mode
    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "[PanicUI] Failed to get current video mode\n");
        return GRAPHICS_ERROR_GENERIC;
    }
    
    g_panicui.screen_width = mode.width;
    g_panicui.screen_height = mode.height;
    
    // Ensure minimum resolution for GUI
    if (g_panicui.screen_width < PANICUI_MIN_WIDTH || g_panicui.screen_height < PANICUI_MIN_HEIGHT) {
        // Try to set a suitable graphics mode
        graphics_result_t result = graphics_set_mode(PANICUI_MIN_WIDTH, PANICUI_MIN_HEIGHT, 32, 60);
        if (result != GRAPHICS_SUCCESS) {
            debuglog(DEBUG_WARN, "[PanicUI] Failed to set graphics mode, using current resolution\n");
        } else {
            g_panicui.screen_width = PANICUI_MIN_WIDTH;
            g_panicui.screen_height = PANICUI_MIN_HEIGHT;
        }
    }
    
    // Load fonts
    graphics_load_font(NULL, PANICUI_FONT_SIZE_LARGE, &g_panicui.font_large);
    graphics_load_font(NULL, PANICUI_FONT_SIZE_NORMAL, &g_panicui.font_normal);
    graphics_load_font(NULL, PANICUI_FONT_SIZE_SMALL, &g_panicui.font_small);
    
    // Calculate window bounds (centered on screen)
    uint32_t window_width = (g_panicui.screen_width * 90) / 100;  // 90% of screen width
    uint32_t window_height = (g_panicui.screen_height * 85) / 100; // 85% of screen height
    
    g_panicui.window_bounds.x = (g_panicui.screen_width - window_width) / 2;
    g_panicui.window_bounds.y = (g_panicui.screen_height - window_height) / 2;
    g_panicui.window_bounds.width = window_width;
    g_panicui.window_bounds.height = window_height;
    
    // Initialize titlebar
    g_panicui.titlebar.bounds.x = g_panicui.window_bounds.x;
    g_panicui.titlebar.bounds.y = g_panicui.window_bounds.y;
    g_panicui.titlebar.bounds.width = g_panicui.window_bounds.width;
    g_panicui.titlebar.bounds.height = PANICUI_TITLEBAR_HEIGHT;
    strcpy(g_panicui.titlebar.title, PANICUI_TITLE);
    
    // Initialize status bar
    g_panicui.statusbar.bounds.x = g_panicui.window_bounds.x;
    g_panicui.statusbar.bounds.y = g_panicui.window_bounds.y + g_panicui.window_bounds.height - PANICUI_STATUSBAR_HEIGHT;
    g_panicui.statusbar.bounds.width = g_panicui.window_bounds.width;
    g_panicui.statusbar.bounds.height = PANICUI_STATUSBAR_HEIGHT;
    strcpy(g_panicui.statusbar.status_text, "Kernel Panic - System Halted");
    
    // Initialize tabs
    uint32_t tab_width = (g_panicui.window_bounds.width - PANICUI_SIDEBAR_WIDTH) / PANICUI_PANEL_COUNT;
    for (uint32_t i = 0; i < PANICUI_PANEL_COUNT; i++) {
        panicui_tab_t* tab = &g_panicui.tabs[i];
        tab->base.bounds.x = g_panicui.window_bounds.x + PANICUI_SIDEBAR_WIDTH + i * tab_width;
        tab->base.bounds.y = g_panicui.window_bounds.y + PANICUI_TITLEBAR_HEIGHT;
        tab->base.bounds.width = tab_width;
        tab->base.bounds.height = PANICUI_TAB_HEIGHT;
        tab->base.visible = true;
        tab->base.enabled = true;
        tab->panel_type = (panicui_panel_type_t)i;
        strcpy(tab->text, g_tab_names[i]);
        tab->active = (i == 0);
    }
    
    // Initialize panels
    for (uint32_t i = 0; i < PANICUI_PANEL_COUNT; i++) {
        panicui_panel_t* panel = &g_panicui.panels[i];
        panel->base.bounds.x = g_panicui.window_bounds.x + PANICUI_SIDEBAR_WIDTH;
        panel->base.bounds.y = g_panicui.window_bounds.y + PANICUI_TITLEBAR_HEIGHT + PANICUI_TAB_HEIGHT;
        panel->base.bounds.width = g_panicui.window_bounds.width - PANICUI_SIDEBAR_WIDTH;
        panel->base.bounds.height = g_panicui.window_bounds.height - PANICUI_TITLEBAR_HEIGHT - PANICUI_TAB_HEIGHT - PANICUI_STATUSBAR_HEIGHT;
        panel->base.visible = (i == 0);
        panel->type = (panicui_panel_type_t)i;
        panel->active = (i == 0);
        sprintf(panel->title, "%s", g_tab_names[i]);
    }
    
    g_panicui.active_panel = PANICUI_PANEL_OVERVIEW;
    
    // Enable double buffering for smooth rendering
    graphics_enable_double_buffering(true);
    
    // Initialize the window manager and input handlers
    panicui_wm_init();
    panicui_init_input();
    
    g_panicui.graphics_mode_available = true;
    g_panicui.initialized = true;
    g_panicui.need_redraw = true;
    g_panicui.enable_animations = true;
    
    debuglog(DEBUG_INFO, "[PanicUI] Graphics panic interface initialized (%ux%u)\n", 
             g_panicui.screen_width, g_panicui.screen_height);
    
    return GRAPHICS_SUCCESS;
}

void panicui_shutdown(void) {
    if (!g_panicui.initialized) return;
    
    debuglog(DEBUG_INFO, "[PanicUI] Shutting down graphics panic interface\n");
    
    // Unload fonts
    if (g_panicui.font_large) graphics_unload_font(g_panicui.font_large);
    if (g_panicui.font_normal) graphics_unload_font(g_panicui.font_normal);
    if (g_panicui.font_small) graphics_unload_font(g_panicui.font_small);
    
    // Clean up surfaces
    if (g_panicui.main_surface) graphics_destroy_surface(g_panicui.main_surface);
    if (g_panicui.back_buffer) graphics_destroy_surface(g_panicui.back_buffer);
    
    // Disable double buffering
    graphics_enable_double_buffering(false);
    
    memset(&g_panicui, 0, sizeof(panicui_context_t));
    g_panicui_active = false;
}

bool panicui_is_graphics_available(void) {
    return g_panicui.graphics_mode_available && graphics_is_initialized();
}

panicui_context_t* panicui_get_context(void) {
    return &g_panicui;
}

// =============================================================================
// MAIN INTERFACE FUNCTIONS  
// =============================================================================

void panicui_show_panic(const char* message, const char* file, uint32_t line, 
                       uint32_t fault_addr, uint32_t error_code) {
    if (!panicui_is_graphics_available()) {
        debuglog(DEBUG_ERROR, "[PanicUI] Graphics mode not available for panic display\n");
        return;
    }
    
    // Store panic information
    if (message) {
        strncpy(g_panicui.panic_message, message, sizeof(g_panicui.panic_message) - 1);
        g_panicui.panic_message[sizeof(g_panicui.panic_message) - 1] = '\0';
    }
    if (file) {
        strncpy(g_panicui.panic_file, file, sizeof(g_panicui.panic_file) - 1);
        g_panicui.panic_file[sizeof(g_panicui.panic_file) - 1] = '\0';
    }
    g_panicui.panic_line = line;
    g_panicui.fault_address = fault_addr;
    g_panicui.error_code = error_code;
    
    // Collect system information for all panels
    panicui_collect_register_info();
    panicui_collect_memory_info(fault_addr);
    panicui_collect_stack_trace();
    panicui_collect_system_info();
    panicui_generate_recovery_suggestions();
    
    // Mark for redraw
    g_panicui.need_redraw = true;
    g_panicui_active = true;
    
    debuglog(DEBUG_INFO, "[PanicUI] Displaying panic: %s at %s:%u\n", message, file, line);
    
    // Enter main event loop
    panicui_main_loop();
}

void panicui_main_loop(void) {
    g_panicui.frame_count = 0;
    g_panicui.last_fps_time = 0; // TODO: Get system timer
    
    while (g_panicui_active) {
        // Input is handled by the graphics manager via callbacks.
        // The main loop just needs to keep running.
        
        // Render frame if needed
        if (g_panicui.need_redraw) {
            panicui_render_frame();
            g_panicui.need_redraw = false;
            g_panicui.frame_count++;
        }
        
        // Small delay to prevent excessive CPU usage
        // TODO: Implement proper VSync waiting
        for (volatile int i = 0; i < 100000; i++);
    }
}

// =============================================================================
// INPUT HANDLING
// =============================================================================

void panicui_handle_input(void) {
    // The graphics manager will forward input events to the registered handler,
    // which is panicui_handle_mouse in panicui_input.c.
    // That handler will then call panicui_wm_handle_input.
    // So, we don't need to do anything here.
}

void panicui_handle_mouse_event(const ps2_mouse_event_t* event) {
    if (!event || !g_panicui_active) return;
    
    // Update cursor position
    g_panicui.cursor.x = event->x;
    g_panicui.cursor.y = event->y;
    
    // Clamp cursor to screen
    if (g_panicui.cursor.x < 0) g_panicui.cursor.x = 0;
    if (g_panicui.cursor.y < 0) g_panicui.cursor.y = 0;
    if (g_panicui.cursor.x >= (int32_t)g_panicui.screen_width) 
        g_panicui.cursor.x = g_panicui.screen_width - 1;
    if (g_panicui.cursor.y >= (int32_t)g_panicui.screen_height) 
        g_panicui.cursor.y = g_panicui.screen_height - 1;
    
    // Handle mouse clicks
    if (event->left_button && !g_panicui.mouse_state.left_button) {
        // Left click - check for tab clicks
        for (uint32_t i = 0; i < PANICUI_PANEL_COUNT; i++) {
            if (panicui_point_in_rect(g_panicui.cursor.x, g_panicui.cursor.y, g_panicui.tabs[i].base.bounds)) {
                panicui_switch_to_panel((panicui_panel_type_t)i);
                break;
            }
        }
    }
    
    // Update mouse state
    g_panicui.mouse_state = (ps2_mouse_state_t){
        .x = event->x,
        .y = event->y,
        .left_button = event->left_button,
        .right_button = event->right_button,
        .middle_button = event->middle_button,
        .x_overflow = event->x_overflow,
        .y_overflow = event->y_overflow
    };
    
    g_panicui.need_redraw = true;
}

void panicui_handle_key_event(uint32_t keycode) {
    switch (keycode) {
        case '1': case '2': case '3': case '4': case '5': case '6':
            // Switch to panel by number
            panicui_switch_to_panel((panicui_panel_type_t)(keycode - '1'));
            break;
            
        case 'q': case 'Q':
        case 27: // ESC key
            // Exit panic interface (system remains halted)
            g_panicui_active = false;
            break;
            
        case 'r': case 'R':
            // Attempt system reboot if recovery panel suggests it
            if (g_panicui.panels[PANICUI_PANEL_RECOVERY].content.recovery.can_reboot) {
                debuglog(DEBUG_INFO, "[PanicUI] User requested system reboot\n");
                // TODO: Implement safe reboot
            }
            break;
            
        default:
            break;
    }
    
    g_panicui.need_redraw = true;
}

bool panicui_point_in_rect(int32_t x, int32_t y, graphics_rect_t rect) {
    return (x >= rect.x && x < rect.x + (int32_t)rect.width &&
            y >= rect.y && y < rect.y + (int32_t)rect.height);
}

// =============================================================================
// PANEL MANAGEMENT
// =============================================================================

void panicui_switch_to_panel(panicui_panel_type_t panel) {
    if (panel >= PANICUI_PANEL_COUNT || panel == g_panicui.active_panel) return;
    
    // Deactivate current panel and tab
    g_panicui.panels[g_panicui.active_panel].active = false;
    g_panicui.panels[g_panicui.active_panel].base.visible = false;
    g_panicui.tabs[g_panicui.active_panel].active = false;
    
    // Activate new panel and tab
    g_panicui.active_panel = panel;
    g_panicui.panels[panel].active = true;
    g_panicui.panels[panel].base.visible = true;
    g_panicui.tabs[panel].active = true;
    
    // Update panel content
    panicui_update_panel_content(panel);
    
    g_panicui.need_redraw = true;
    
    debuglog(DEBUG_INFO, "[PanicUI] Switched to panel: %s\n", g_tab_names[panel]);
}

void panicui_update_panel_content(panicui_panel_type_t panel) {
    if (panel >= PANICUI_PANEL_COUNT) return;
    
    panicui_panel_t* p = &g_panicui.panels[panel];
    
    switch (panel) {
        case PANICUI_PANEL_OVERVIEW:
            // Overview content is updated in panicui_show_panic
            strcpy(p->content.overview.error_message, g_panicui.panic_message);
            strcpy(p->content.overview.file_location, g_panicui.panic_file);
            p->content.overview.line_number = g_panicui.panic_line;
            p->content.overview.error_code = g_panicui.error_code;
            strcpy(p->content.overview.error_type, "Page Fault"); // TODO: Classify error type
            break;
            
        case PANICUI_PANEL_REGISTERS:
        case PANICUI_PANEL_MEMORY:
        case PANICUI_PANEL_STACK:
        case PANICUI_PANEL_SYSTEM:
        case PANICUI_PANEL_RECOVERY:
            // Content updated by respective collection functions
            break;
    }
}

// These functions will be implemented in the next part...
void panicui_collect_register_info(void) {
    // TODO: Implement register collection
    panicui_panel_t* panel = &g_panicui.panels[PANICUI_PANEL_REGISTERS];
    
    // For now, set dummy values
    panel->content.registers.eax = 0xDEADBEEF;
    panel->content.registers.ebx = 0xCAFEBABE;
    panel->content.registers.ecx = 0x12345678;
    panel->content.registers.edx = 0x87654321;
    // TODO: Get actual register values from interrupt frame
}

void panicui_collect_memory_info(uint32_t fault_address) {
    panicui_panel_t* panel = &g_panicui.panels[PANICUI_PANEL_MEMORY];
    
    panel->content.memory.base_address = fault_address & ~0xFFF; // Page align
    panel->content.memory.view_size = 4096; // Show one page
    panel->content.memory.hex_mode = true;
    panel->content.memory.bytes_per_line = 16;
    panel->content.memory.highlighted_offset = fault_address & 0xFFF;
    
    // TODO: Safely read memory around fault address
}

void panicui_collect_stack_trace(void) {
    // TODO: Implement stack unwinding
    panicui_panel_t* panel = &g_panicui.panels[PANICUI_PANEL_STACK];
    
    panel->content.stack.frame_count = 5;
    panel->content.stack.selected_frame = 0;
    
    static uint32_t dummy_stack[5] = {0xC0100123, 0xC0100456, 0xC0100789, 0xC0100ABC, 0xC0100DEF};
    panel->content.stack.stack_trace = dummy_stack;

    strcpy(panel->content.stack.function_names[0], "kernel_panic");
    strcpy(panel->content.stack.function_names[1], "divide_by_zero_handler");
    strcpy(panel->content.stack.function_names[2], "some_other_function");
    strcpy(panel->content.stack.function_names[3], "another_function");
    strcpy(panel->content.stack.function_names[4], "start_kernel");
}

void panicui_collect_system_info(void) {
    panicui_panel_t* panel = &g_panicui.panels[PANICUI_PANEL_SYSTEM];
    
    strcpy(panel->content.system.cpu_info, "Intel x86 Compatible CPU");
    strcpy(panel->content.system.memory_info, "Physical Memory: 128MB");
    strcpy(panel->content.system.hardware_info, "Graphics: BGA/VESA Compatible");
    
    // TODO: Get actual system information
}

void panicui_generate_recovery_suggestions(void) {
    panicui_panel_t* panel = &g_panicui.panels[PANICUI_PANEL_RECOVERY];
    
    panel->content.recovery.suggestion_count = 3;
    strcpy(panel->content.recovery.suggestions[0], "Check for hardware issues");
    strcpy(panel->content.recovery.suggestions[1], "Review recent kernel changes");
    strcpy(panel->content.recovery.suggestions[2], "Run memory diagnostics");
    
    panel->content.recovery.can_continue = false;
    panel->content.recovery.can_reboot = true;
    panel->content.recovery.can_debug = false;
}

// =============================================================================
// RENDERING FUNCTIONS
// =============================================================================

void panicui_render_frame(void) {
    if (!g_panicui.initialized || !panicui_is_graphics_available()) return;
    
    // Clear screen with background color
    graphics_clear_screen(PANICUI_COLOR_BG_PRIMARY);
    
    // Draw all windows
    panicui_wm_draw(g_panicui.main_surface);
    
    // Swap buffers for smooth display
    graphics_swap_buffers();
}

void panicui_draw_window_frame(void) {
    // Draw main window with border
    panicui_draw_rect_with_border(g_panicui.window_bounds, 
                                 PANICUI_COLOR_BG_SECONDARY, 
                                 PANICUI_COLOR_BORDER, 
                                 PANICUI_WINDOW_BORDER);
    
    // Draw sidebar area
    graphics_rect_t sidebar_rect = {
        g_panicui.window_bounds.x + PANICUI_WINDOW_BORDER,
        g_panicui.window_bounds.y + PANICUI_TITLEBAR_HEIGHT + PANICUI_WINDOW_BORDER,
        PANICUI_SIDEBAR_WIDTH - PANICUI_WINDOW_BORDER,
        g_panicui.window_bounds.height - PANICUI_TITLEBAR_HEIGHT - PANICUI_STATUSBAR_HEIGHT - 2 * PANICUI_WINDOW_BORDER
    };
    
    graphics_draw_rect(&sidebar_rect, PANICUI_COLOR_BG_ACCENT, true);
    
    // Draw Forest OS logo/title in sidebar
    if (g_panicui.font_large) {
        panicui_draw_text_with_shadow(sidebar_rect.x + PANICUI_PADDING, 
                                     sidebar_rect.y + PANICUI_PADDING,
                                     "Forest OS", 
                                     g_panicui.font_large, 
                                     PANICUI_COLOR_TEXT_PRIMARY);
    }
    
    if (g_panicui.font_normal) {
        panicui_draw_text_with_shadow(sidebar_rect.x + PANICUI_PADDING, 
                                     sidebar_rect.y + PANICUI_PADDING + 30,
                                     "Kernel Panic", 
                                     g_panicui.font_normal, 
                                     PANICUI_COLOR_ERROR);
    }
    
    // Draw version info
    if (g_panicui.font_small) {
        panicui_draw_text_with_shadow(sidebar_rect.x + PANICUI_PADDING, 
                                     sidebar_rect.y + sidebar_rect.height - 40,
                                     "PanicUI " PANICUI_VERSION, 
                                     g_panicui.font_small, 
                                     PANICUI_COLOR_TEXT_MUTED);
    }
}

void panicui_draw_titlebar(void) {
    // Draw titlebar background
    graphics_draw_rect(&g_panicui.titlebar.bounds, PANICUI_COLOR_TITLEBAR, true);
    
    // Draw titlebar border
    graphics_rect_t border_rect = {
        g_panicui.titlebar.bounds.x,
        g_panicui.titlebar.bounds.y + g_panicui.titlebar.bounds.height - 1,
        g_panicui.titlebar.bounds.width,
        1
    };
    graphics_draw_rect(&border_rect, PANICUI_COLOR_BORDER, true);
    
    // Draw title text
    if (g_panicui.font_normal) {
        panicui_draw_text_with_shadow(g_panicui.titlebar.bounds.x + PANICUI_PADDING,
                                     g_panicui.titlebar.bounds.y + (PANICUI_TITLEBAR_HEIGHT - 16) / 2,
                                     g_panicui.titlebar.title,
                                     g_panicui.font_normal,
                                     PANICUI_COLOR_TEXT_PRIMARY);
    }
    
    // Draw close button (X) in top-right corner
    graphics_rect_t close_btn = {
        g_panicui.titlebar.bounds.x + g_panicui.titlebar.bounds.width - 30,
        g_panicui.titlebar.bounds.y + 5,
        20, 20
    };
    
    bool close_hovered = panicui_point_in_rect(g_panicui.cursor.x, g_panicui.cursor.y, close_btn);
    panicui_draw_button(close_btn, "X", false, close_hovered);
}

void panicui_draw_tabs(void) {
    for (uint32_t i = 0; i < PANICUI_PANEL_COUNT; i++) {
        panicui_tab_t* tab = &g_panicui.tabs[i];
        
        // Choose colors based on tab state
        graphics_color_t bg_color = tab->active ? PANICUI_COLOR_BG_SECONDARY : PANICUI_COLOR_BG_ACCENT;
        graphics_color_t text_color = tab->active ? PANICUI_COLOR_TEXT_PRIMARY : PANICUI_COLOR_TEXT_SECONDARY;
        graphics_color_t border_color = tab->active ? PANICUI_COLOR_HIGHLIGHT : PANICUI_COLOR_BORDER;
        
        // Check if tab is hovered
        bool hovered = panicui_point_in_rect(g_panicui.cursor.x, g_panicui.cursor.y, tab->base.bounds);
        if (hovered && !tab->active) {
            bg_color = panicui_lighten_color(bg_color, 15);
        }
        
        // Draw tab background
        graphics_draw_rect(&tab->base.bounds, bg_color, true);
        
        // Draw tab border (bottom line for active tab, full border for inactive)
        if (tab->active) {
            graphics_rect_t bottom_line = {
                tab->base.bounds.x,
                tab->base.bounds.y + tab->base.bounds.height - 2,
                tab->base.bounds.width,
                2
            };
            graphics_draw_rect(&bottom_line, border_color, true);
        } else {
            graphics_rect_t border = {
                tab->base.bounds.x,
                tab->base.bounds.y + tab->base.bounds.height - 1,
                tab->base.bounds.width,
                1
            };
            graphics_draw_rect(&border, border_color, true);
        }
        
        // Draw tab text
        if (g_panicui.font_normal) {
            uint32_t text_width, text_height;
            if (graphics_get_text_bounds(tab->text, g_panicui.font_normal, &text_width, &text_height) == GRAPHICS_SUCCESS) {
                int32_t text_x = tab->base.bounds.x + (tab->base.bounds.width - text_width) / 2;
                int32_t text_y = tab->base.bounds.y + (tab->base.bounds.height - text_height) / 2;
                panicui_draw_text_with_shadow(text_x, text_y, tab->text, g_panicui.font_normal, text_color);
            }
        }
    }
}

void panicui_draw_panel(panicui_panel_type_t panel) {
    if (panel >= PANICUI_PANEL_COUNT) return;
    
    panicui_panel_t* p = &g_panicui.panels[panel];
    
    // Draw panel background
    graphics_draw_rect(&p->base.bounds, PANICUI_COLOR_BG_SECONDARY, true);
    
    // Draw panel border
    graphics_rect_t border_rect = p->base.bounds;
    border_rect.x -= 1;
    border_rect.y -= 1;
    border_rect.width += 2;
    border_rect.height += 2;
    panicui_draw_rect_with_border(border_rect, COLOR_TRANSPARENT, PANICUI_COLOR_BORDER, 1);
    
    // Content area (with padding)
    graphics_rect_t content_area = {
        p->base.bounds.x + PANICUI_PADDING,
        p->base.bounds.y + PANICUI_PADDING,
        p->base.bounds.width - 2 * PANICUI_PADDING,
        p->base.bounds.height - 2 * PANICUI_PADDING
    };
    
    switch (panel) {
        case PANICUI_PANEL_OVERVIEW:
            panicui_draw_overview_panel(&p->content.overview, content_area);
            break;
        case PANICUI_PANEL_REGISTERS:
            panicui_draw_registers_panel(&p->content.registers, content_area);
            break;
        case PANICUI_PANEL_MEMORY:
            panicui_draw_memory_panel(&p->content.memory, content_area);
            break;
        case PANICUI_PANEL_STACK:
            panicui_draw_stack_panel(&p->content.stack, content_area);
            break;
        case PANICUI_PANEL_SYSTEM:
            panicui_draw_system_panel(&p->content.system, content_area);
            break;
        case PANICUI_PANEL_RECOVERY:
            panicui_draw_recovery_panel(&p->content.recovery, content_area);
            break;
    }
}

// Panel-specific drawing functions
void panicui_draw_overview_panel(void* content, graphics_rect_t area) {
    struct { char error_message[512]; char error_type[128]; char file_location[256]; 
             uint32_t line_number; uint32_t error_code; } *overview = content;
    
    int32_t y = area.y;
    int32_t line_height = 20;
    
    if (g_panicui.font_large) {
        // Error type header
        panicui_draw_text_with_shadow(area.x, y, "KERNEL PANIC", g_panicui.font_large, PANICUI_COLOR_ERROR);
        y += 35;
        
        // Error message
        panicui_draw_text_with_shadow(area.x, y, overview->error_message, g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += 30;
    }
    
    if (g_panicui.font_normal) {
        // Error details
        char error_details[256];
        sprintf(error_details, "Type: %s", overview->error_type);
        panicui_draw_text_with_shadow(area.x, y, error_details, g_panicui.font_normal, PANICUI_COLOR_TEXT_SECONDARY);
        y += line_height;
        
        sprintf(error_details, "Location: %s:%u", overview->file_location, overview->line_number);
        panicui_draw_text_with_shadow(area.x, y, error_details, g_panicui.font_normal, PANICUI_COLOR_TEXT_SECONDARY);
        y += line_height;
        
        sprintf(error_details, "Error Code: 0x%08X", overview->error_code);
        panicui_draw_text_with_shadow(area.x, y, error_details, g_panicui.font_normal, PANICUI_COLOR_TEXT_SECONDARY);
        y += line_height * 2;
        
        // Instructions
        panicui_draw_text_with_shadow(area.x, y, "Use the tabs above to view detailed information:", 
                                     g_panicui.font_normal, PANICUI_COLOR_TEXT_SECONDARY);
        y += line_height * 2;
        
        panicui_draw_text_with_shadow(area.x, y, "• Registers - CPU register state at time of panic", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, "• Memory - Memory view around fault address", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, "• Stack Trace - Function call stack", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, "• System Info - Hardware and system state", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, "• Recovery - Suggested recovery actions", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
    }
}

void panicui_draw_registers_panel(void* content, graphics_rect_t area) {
    struct { uint32_t eax, ebx, ecx, edx, esp, ebp, esi, edi, eip, eflags;
             uint16_t cs, ds, es, fs, gs, ss; uint32_t cr0, cr2, cr3, cr4; } *regs = content;
    
    int32_t y = area.y;
    int32_t line_height = 18;
    int32_t col_width = area.width / 3;
    
    if (g_panicui.font_normal) {
        // General purpose registers
        panicui_draw_text_with_shadow(area.x, y, "General Purpose Registers:", 
                                     g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height + 5;
        
        char reg_text[64];
        sprintf(reg_text, "EAX: 0x%08X", regs->eax);
        panicui_draw_text_with_shadow(area.x, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        
        sprintf(reg_text, "EBX: 0x%08X", regs->ebx);
        panicui_draw_text_with_shadow(area.x + col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        
        sprintf(reg_text, "ECX: 0x%08X", regs->ecx);
        panicui_draw_text_with_shadow(area.x + 2 * col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height;
        
        sprintf(reg_text, "EDX: 0x%08X", regs->edx);
        panicui_draw_text_with_shadow(area.x, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        
        sprintf(reg_text, "ESI: 0x%08X", regs->esi);
        panicui_draw_text_with_shadow(area.x + col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        
        sprintf(reg_text, "EDI: 0x%08X", regs->edi);
        panicui_draw_text_with_shadow(area.x + 2 * col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height * 2;
        
        // Stack and instruction pointers
        panicui_draw_text_with_shadow(area.x, y, "Stack & Instruction Pointers:", 
                                     g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height + 5;
        
        sprintf(reg_text, "EIP: 0x%08X", regs->eip);
        panicui_draw_text_with_shadow(area.x, y, reg_text, g_panicui.font_small, PANICUI_COLOR_ERROR);
        
        sprintf(reg_text, "ESP: 0x%08X", regs->esp);
        panicui_draw_text_with_shadow(area.x + col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        
        sprintf(reg_text, "EBP: 0x%08X", regs->ebp);
        panicui_draw_text_with_shadow(area.x + 2 * col_width, y, reg_text, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
    }
}

void panicui_draw_memory_panel(void* content, graphics_rect_t area) {
    struct { uint32_t base_address; uint32_t view_size; uint8_t* memory_data; 
             bool hex_mode; uint32_t bytes_per_line; uint32_t highlighted_offset; } *mem = content;
    
    int32_t y = area.y;
    int32_t line_height = 16;
    
    if (g_panicui.font_normal) {
        char header[128];
        sprintf(header, "Memory View - Base Address: 0x%08X", mem->base_address);
        panicui_draw_text_with_shadow(area.x, y, header, g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height * 2;
        
        // TODO: Draw hex dump with highlighted fault address
        panicui_draw_text_with_shadow(area.x, y, "Memory visualization not yet implemented", 
                                     g_panicui.font_small, PANICUI_COLOR_TEXT_MUTED);
    }
}

void panicui_draw_stack_panel(void* content, graphics_rect_t area) {
    struct { uint32_t* stack_trace; uint32_t frame_count; uint32_t selected_frame; 
             char function_names[16][64]; } *stack = content;
    
    int32_t y = area.y;
    int32_t line_height = 18;
    
    if (g_panicui.font_normal) {
        panicui_draw_text_with_shadow(area.x, y, "Call Stack:", g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height + 5;
        
        for (uint32_t i = 0; i < stack->frame_count && i < 16; i++) {
            char frame_text[128];
            sprintf(frame_text, "#%u  0x%08X  %s", i, stack->stack_trace[i], stack->function_names[i]);
            
            graphics_color_t text_color = (i == stack->selected_frame) ? 
                PANICUI_COLOR_HIGHLIGHT : PANICUI_COLOR_TEXT_PRIMARY;
                
            panicui_draw_text_with_shadow(area.x + 10, y, frame_text, g_panicui.font_small, text_color);
            y += line_height;
        }
    }
}

void panicui_draw_system_panel(void* content, graphics_rect_t area) {
    struct { char cpu_info[256]; char memory_info[256]; char hardware_info[512]; 
             uint32_t uptime; uint32_t total_memory; uint32_t free_memory; } *sys = content;
    
    int32_t y = area.y;
    int32_t line_height = 18;
    
    if (g_panicui.font_normal) {
        panicui_draw_text_with_shadow(area.x, y, "System Information:", 
                                     g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height + 5;
        
        panicui_draw_text_with_shadow(area.x, y, sys->cpu_info, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, sys->memory_info, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height;
        
        panicui_draw_text_with_shadow(area.x, y, sys->hardware_info, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
    }
}

void panicui_draw_recovery_panel(void* content, graphics_rect_t area) {
    struct { char suggestions[10][128]; uint32_t suggestion_count; 
             bool can_continue; bool can_reboot; bool can_debug; } *recovery = content;
    
    int32_t y = area.y;
    int32_t line_height = 18;
    
    if (g_panicui.font_normal) {
        panicui_draw_text_with_shadow(area.x, y, "Recovery Suggestions:", 
                                     g_panicui.font_normal, PANICUI_COLOR_TEXT_PRIMARY);
        y += line_height + 5;
        
        for (uint32_t i = 0; i < recovery->suggestion_count && i < 10; i++) {
            char suggestion[140];
            sprintf(suggestion, "• %s", recovery->suggestions[i]);
            panicui_draw_text_with_shadow(area.x, y, suggestion, g_panicui.font_small, PANICUI_COLOR_TEXT_PRIMARY);
            y += line_height;
        }
        
        y += line_height;
        
        // Action buttons
        if (recovery->can_reboot) {
            graphics_rect_t reboot_btn = {area.x, y, 100, 25};
            bool hovered = panicui_point_in_rect(g_panicui.cursor.x, g_panicui.cursor.y, reboot_btn);
            panicui_draw_button(reboot_btn, "Reboot", false, hovered);
        }
        
        graphics_rect_t halt_btn = {area.x + 110, y, 100, 25};
        bool halt_hovered = panicui_point_in_rect(g_panicui.cursor.x, g_panicui.cursor.y, halt_btn);
        panicui_draw_button(halt_btn, "Halt", false, halt_hovered);
    }
}

void panicui_draw_statusbar(void) {
    // Draw status bar background
    graphics_draw_rect(&g_panicui.statusbar.bounds, PANICUI_COLOR_BG_ACCENT, true);
    
    // Draw top border
    graphics_rect_t border_rect = {
        g_panicui.statusbar.bounds.x,
        g_panicui.statusbar.bounds.y,
        g_panicui.statusbar.bounds.width,
        1
    };
    graphics_draw_rect(&border_rect, PANICUI_COLOR_BORDER, true);
    
    // Status text
    if (g_panicui.font_small) {
        panicui_draw_text_with_shadow(g_panicui.statusbar.bounds.x + PANICUI_PADDING,
                                     g_panicui.statusbar.bounds.y + 5,
                                     g_panicui.statusbar.status_text,
                                     g_panicui.font_small,
                                     PANICUI_COLOR_TEXT_SECONDARY);
        
        // Mouse coordinates (debug info)
        char mouse_info[64];
        sprintf(mouse_info, "Mouse: %d,%d", g_panicui.cursor.x, g_panicui.cursor.y);
        panicui_draw_text_with_shadow(g_panicui.statusbar.bounds.x + g_panicui.statusbar.bounds.width - 150,
                                     g_panicui.statusbar.bounds.y + 5,
                                     mouse_info,
                                     g_panicui.font_small,
                                     PANICUI_COLOR_TEXT_MUTED);
    }
}

void panicui_draw_cursor(void) {
    if (!g_panicui.cursor.visible) return;
    
    // Draw simple cursor (crosshair or arrow)
    graphics_color_t cursor_color = PANICUI_COLOR_TEXT_PRIMARY;
    
    // Draw cursor as crosshair for now
    graphics_draw_line(g_panicui.cursor.x - 5, g_panicui.cursor.y, 
                      g_panicui.cursor.x + 5, g_panicui.cursor.y, cursor_color);
    graphics_draw_line(g_panicui.cursor.x, g_panicui.cursor.y - 5,
                      g_panicui.cursor.x, g_panicui.cursor.y + 5, cursor_color);
    
    // Draw cursor center dot
    graphics_draw_pixel(g_panicui.cursor.x, g_panicui.cursor.y, cursor_color);
}
