#ifndef PANICUI_H
#define PANICUI_H

#include "types.h"
#include "graphics/graphics_manager.h"
#include "graphics/graphics_types.h"
#include "ps2_mouse.h"

// =============================================================================
// PANICUI CORE DEFINITIONS
// =============================================================================

#define PANICUI_VERSION "1.0"
#define PANICUI_TITLE "Forest OS Kernel Panic - Graphics Mode"

// Window dimensions and layout
#define PANICUI_MIN_WIDTH       1024
#define PANICUI_MIN_HEIGHT      768
#define PANICUI_WINDOW_BORDER   2
#define PANICUI_TITLEBAR_HEIGHT 32
#define PANICUI_STATUSBAR_HEIGHT 24
#define PANICUI_TAB_HEIGHT      28
#define PANICUI_SIDEBAR_WIDTH   200
#define PANICUI_PADDING         8
#define PANICUI_SCROLLBAR_WIDTH 16

// Color scheme - Modern dark theme
#define PANICUI_COLOR_BG_PRIMARY    ((graphics_color_t){20,  20,  30,  255})  // Very dark blue
#define PANICUI_COLOR_BG_SECONDARY  ((graphics_color_t){30,  30,  45,  255})  // Dark blue
#define PANICUI_COLOR_BG_ACCENT     ((graphics_color_t){40,  40,  60,  255})  // Medium dark blue
#define PANICUI_COLOR_BORDER        ((graphics_color_t){60,  60,  80,  255})  // Border blue
#define PANICUI_COLOR_TITLEBAR      ((graphics_color_t){25,  25,  40,  255})  // Title bar
#define PANICUI_COLOR_TEXT_PRIMARY  ((graphics_color_t){255, 255, 255, 255})  // White
#define PANICUI_COLOR_TEXT_SECONDARY ((graphics_color_t){200, 200, 220, 255}) // Light gray
#define PANICUI_COLOR_TEXT_MUTED    ((graphics_color_t){150, 150, 170, 255})  // Muted gray
#define PANICUI_COLOR_ERROR         ((graphics_color_t){255, 80,  80,  255})  // Red
#define PANICUI_COLOR_WARNING       ((graphics_color_t){255, 200, 80,  255})  // Orange
#define PANICUI_COLOR_SUCCESS       ((graphics_color_t){80,  255, 80,  255})  // Green
#define PANICUI_COLOR_INFO          ((graphics_color_t){80,  160, 255, 255})  // Blue
#define PANICUI_COLOR_HIGHLIGHT     ((graphics_color_t){100, 150, 255, 255})  // Light blue
#define PANICUI_COLOR_SELECTION     ((graphics_color_t){60,  120, 200, 128})  // Semi-transparent blue

// Font settings
#define PANICUI_FONT_SIZE_LARGE  16
#define PANICUI_FONT_SIZE_NORMAL 14
#define PANICUI_FONT_SIZE_SMALL  12

// =============================================================================
// UI COMPONENT TYPES
// =============================================================================

typedef enum {
    PANICUI_PANEL_OVERVIEW = 0,
    PANICUI_PANEL_REGISTERS,
    PANICUI_PANEL_MEMORY,
    PANICUI_PANEL_STACK,
    PANICUI_PANEL_SYSTEM,
    PANICUI_PANEL_RECOVERY,
    PANICUI_PANEL_COUNT
} panicui_panel_type_t;

typedef enum {
    PANICUI_CURSOR_DEFAULT = 0,
    PANICUI_CURSOR_POINTER,
    PANICUI_CURSOR_TEXT,
    PANICUI_CURSOR_RESIZE_H,
    PANICUI_CURSOR_RESIZE_V,
    PANICUI_CURSOR_RESIZE_DIAG,
    PANICUI_CURSOR_MOVE,
    PANICUI_CURSOR_COUNT
} panicui_cursor_type_t;

typedef enum {
    PANICUI_EVENT_NONE = 0,
    PANICUI_EVENT_MOUSE_MOVE,
    PANICUI_EVENT_MOUSE_CLICK,
    PANICUI_EVENT_MOUSE_RELEASE,
    PANICUI_EVENT_KEY_PRESS,
    PANICUI_EVENT_TAB_CLICK,
    PANICUI_EVENT_SCROLL,
    PANICUI_EVENT_WINDOW_RESIZE
} panicui_event_type_t;

// =============================================================================
// UI COMPONENT STRUCTURES
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

typedef struct {
    panicui_widget_t base;
    char title[128];
    bool active;
    bool closable;
    panicui_panel_type_t type;
    
    // Scrolling support
    int32_t scroll_x;
    int32_t scroll_y;
    int32_t content_width;
    int32_t content_height;
    bool show_scrollbar_h;
    bool show_scrollbar_v;
    
    // Content-specific data
    union {
        struct {
            char error_message[512];
            char error_type[128];
            char file_location[256];
            uint32_t line_number;
            uint32_t error_code;
        } overview;
        
        struct {
            uint32_t eax, ebx, ecx, edx;
            uint32_t esp, ebp, esi, edi;
            uint32_t eip, eflags;
            uint16_t cs, ds, es, fs, gs, ss;
            uint32_t cr0, cr2, cr3, cr4;
        } registers;
        
        struct {
            uint32_t base_address;
            uint32_t view_size;
            uint8_t* memory_data;
            bool hex_mode;
            uint32_t bytes_per_line;
            uint32_t highlighted_offset;
        } memory;
        
        struct {
            uint32_t* stack_trace;
            uint32_t frame_count;
            uint32_t selected_frame;
            char function_names[16][64];
        } stack;
        
        struct {
            char cpu_info[256];
            char memory_info[256];
            char hardware_info[512];
            uint32_t uptime;
            uint32_t total_memory;
            uint32_t free_memory;
        } system;
        
        struct {
            char suggestions[10][128];
            uint32_t suggestion_count;
            bool can_continue;
            bool can_reboot;
            bool can_debug;
        } recovery;
    } content;
} panicui_panel_t;

typedef struct {
    graphics_rect_t bounds;
    char title[128];
    bool dragging;
    int32_t drag_start_x;
    int32_t drag_start_y;
} panicui_titlebar_t;

typedef struct {
    graphics_rect_t bounds;
    char status_text[256];
    uint32_t timestamp;
} panicui_statusbar_t;

typedef struct {
    int32_t x, y;
    panicui_cursor_type_t type;
    bool visible;
    graphics_surface_t* cursor_surface;
} panicui_cursor_t;

// =============================================================================
// MAIN PANICUI STATE STRUCTURE
// =============================================================================

typedef struct {
    bool initialized;
    bool graphics_mode_available;
    
    // Display properties
    uint32_t screen_width;
    uint32_t screen_height;
    graphics_surface_t* main_surface;
    graphics_surface_t* back_buffer;
    font_t* font_large;
    font_t* font_normal;
    font_t* font_small;
    
    // Window components
    graphics_rect_t window_bounds;
    panicui_titlebar_t titlebar;
    panicui_statusbar_t statusbar;
    panicui_tab_t tabs[PANICUI_PANEL_COUNT];
    panicui_panel_t panels[PANICUI_PANEL_COUNT];
    panicui_panel_type_t active_panel;
    
    // Input state
    panicui_cursor_t cursor;
    ps2_mouse_state_t mouse_state;
    bool mouse_captured;
    uint32_t last_click_time;
    panicui_event_type_t last_event;
    
    // Animation and rendering
    bool need_redraw;
    bool enable_animations;
    uint32_t animation_time;
    
    // Error context
    char panic_message[512];
    char panic_file[256];
    uint32_t panic_line;
    uint32_t fault_address;
    uint32_t error_code;
    
    // Performance metrics
    uint32_t frame_count;
    uint32_t last_fps_time;
    uint32_t fps;
} panicui_context_t;

// =============================================================================
// PANICUI FUNCTION DECLARATIONS
// =============================================================================

// Core initialization and management
graphics_result_t panicui_init(void);
void panicui_shutdown(void);
bool panicui_is_graphics_available(void);

// Main interface functions
void panicui_show_panic(const char* message, const char* file, uint32_t line, 
                       uint32_t fault_addr, uint32_t error_code);
void panicui_main_loop(void);
void panicui_handle_input(void);
void panicui_render_frame(void);

// Panel management
void panicui_switch_to_panel(panicui_panel_type_t panel);
void panicui_update_panel_content(panicui_panel_type_t panel);
void panicui_scroll_panel(panicui_panel_type_t panel, int32_t delta_x, int32_t delta_y);

// Drawing functions
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

// Input handling
void panicui_handle_mouse_event(const ps2_mouse_event_t* event);
void panicui_handle_key_event(uint32_t keycode);
bool panicui_point_in_rect(int32_t x, int32_t y, graphics_rect_t rect);
panicui_widget_t* panicui_get_widget_at_point(int32_t x, int32_t y);

// Memory and system info collection
void panicui_collect_register_info(void);
void panicui_collect_memory_info(uint32_t fault_address);
void panicui_collect_stack_trace(void);
void panicui_collect_system_info(void);
void panicui_generate_recovery_suggestions(void);

// Color and theme utilities
graphics_color_t panicui_blend_colors(graphics_color_t a, graphics_color_t b, uint8_t alpha);
graphics_color_t panicui_darken_color(graphics_color_t color, uint8_t amount);
graphics_color_t panicui_lighten_color(graphics_color_t color, uint8_t amount);

// Panel-specific drawing functions (internal)
void panicui_draw_overview_panel(void* content, graphics_rect_t area);
void panicui_draw_registers_panel(void* content, graphics_rect_t area);
void panicui_draw_memory_panel(void* content, graphics_rect_t area);
void panicui_draw_stack_panel(void* content, graphics_rect_t area);
void panicui_draw_system_panel(void* content, graphics_rect_t area);
void panicui_draw_recovery_panel(void* content, graphics_rect_t area);

// Global context access
panicui_context_t* panicui_get_context(void);

#endif // PANICUI_H
