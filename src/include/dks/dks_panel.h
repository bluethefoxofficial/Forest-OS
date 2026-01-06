#ifndef DKS_PANEL_H
#define DKS_PANEL_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "dks_theme.h"
#include "dks_core.h"

// Panel position
typedef enum {
    PANEL_POSITION_BOTTOM,
    PANEL_POSITION_TOP,
    PANEL_POSITION_LEFT,
    PANEL_POSITION_RIGHT
} panel_position_t;

// Panel mode
typedef enum {
    PANEL_MODE_TASKBAR,    // Traditional taskbar with start, window list, tray
    PANEL_MODE_DOCK        // macOS-style dock with centered icons
} panel_mode_t;

// Panel item types
typedef enum {
    PANEL_ITEM_START_BUTTON,
    PANEL_ITEM_APP_LAUNCHER,
    PANEL_ITEM_WINDOW_BUTTON,
    PANEL_ITEM_SEPARATOR,
    PANEL_ITEM_SYSTRAY_ICON,
    PANEL_ITEM_CLOCK,
    PANEL_ITEM_SPACER
} panel_item_type_t;

// Panel item structure
typedef struct panel_item {
    panel_item_type_t type;
    uint32_t id;

    // Geometry (computed)
    graphics_rect_t bounds;

    // State
    bool visible;
    bool enabled;
    bool hovered;
    bool pressed;
    bool active;        // For window buttons - window is focused

    // Content
    char label[64];
    char tooltip[128];
    bmp_image_t* icon;

    // For window buttons
    dks_window_t* window;

    // Callback
    void (*on_click)(struct panel_item* item, void* data);
    void* callback_data;

    // Linked list
    struct panel_item* next;
    struct panel_item* prev;
} panel_item_t;

// Panel configuration
typedef struct {
    panel_position_t position;
    panel_mode_t mode;
    uint32_t size;              // Height for top/bottom, width for left/right
    bool auto_hide;
    uint32_t auto_hide_delay;   // ms before hiding

    // Taskbar mode options
    bool show_start_button;
    bool show_window_list;
    bool show_systray;
    bool show_clock;
    bool show_seconds;
    bool group_windows;         // Group windows by app

    // Dock mode options
    uint32_t icon_size;
    uint32_t icon_spacing;
    bool magnify_on_hover;
    uint32_t magnify_size;

    // Appearance
    bool transparent;
    uint8_t opacity;           // 0-255
} dks_panel_config_t;

// Panel state
typedef struct {
    dks_panel_config_t config;
    graphics_rect_t bounds;
    panel_item_t* items;
    uint32_t item_count;

    // Auto-hide state
    bool hidden;
    uint32_t hide_timer;
    bool mouse_in_panel;

    // Hover state
    panel_item_t* hovered_item;

    // Animations
    float current_size;         // For auto-hide animation
    float target_size;

    // Dirty flag
    bool dirty;
} dks_panel_t;

// Panel initialization
void dks_panel_init(void);
void dks_panel_shutdown(void);

// Configuration
void dks_panel_set_config(const dks_panel_config_t* config);
dks_panel_config_t* dks_panel_get_config(void);
void dks_panel_set_position(panel_position_t position);
void dks_panel_set_mode(panel_mode_t mode);
void dks_panel_set_size(uint32_t size);
void dks_panel_set_auto_hide(bool auto_hide);

// Panel items
panel_item_t* dks_panel_add_launcher(const char* app_id, const char* label, bmp_image_t* icon, void (*callback)(panel_item_t*, void*), void* data);
panel_item_t* dks_panel_add_separator(void);
void dks_panel_remove_item(panel_item_t* item);
void dks_panel_clear_items(void);

// Window list management (for taskbar mode)
void dks_panel_update_window_list(void);
void dks_panel_add_window(dks_window_t* window);
void dks_panel_remove_window(dks_window_t* window);
void dks_panel_update_window(dks_window_t* window);

// System tray
typedef struct {
    uint32_t id;
    bmp_image_t* icon;
    char tooltip[128];
    void (*on_click)(void* data);
    void (*on_right_click)(void* data);
    void* callback_data;
} systray_icon_t;

void dks_systray_add_icon(uint32_t id, bmp_image_t* icon, const char* tooltip, void (*on_click)(void*), void* data);
void dks_systray_remove_icon(uint32_t id);
void dks_systray_update_icon(uint32_t id, bmp_image_t* new_icon);
void dks_systray_set_tooltip(uint32_t id, const char* tooltip);

// Clock
void dks_panel_update_clock(void);
void dks_panel_get_time_string(char* buffer, uint32_t size, bool show_seconds);

// Input handling
bool dks_panel_handle_mouse_move(int32_t x, int32_t y);
bool dks_panel_handle_mouse_button(int32_t x, int32_t y, uint8_t button, bool pressed);
bool dks_panel_contains_point(int32_t x, int32_t y);
panel_item_t* dks_panel_hit_test(int32_t x, int32_t y);

// Rendering
void dks_panel_render(graphics_surface_t* surface, const dks_theme_t* theme);
void dks_panel_invalidate(void);

// Get panel geometry
void dks_panel_get_bounds(graphics_rect_t* bounds);
uint32_t dks_panel_get_reserved_space(void);  // Space reserved for panel on screen edge

// Start menu (separate but related)
typedef struct {
    bool visible;
    graphics_rect_t bounds;
    int32_t hover_index;
    // Menu items handled by dks_menu.h
} dks_start_menu_t;

void dks_start_menu_show(void);
void dks_start_menu_hide(void);
void dks_start_menu_toggle(void);
bool dks_start_menu_is_visible(void);

#endif // DKS_PANEL_H
