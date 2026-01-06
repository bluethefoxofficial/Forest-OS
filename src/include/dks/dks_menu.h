#ifndef DKS_MENU_H
#define DKS_MENU_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "dks_theme.h"

// Maximum menu items
#define DKS_MENU_MAX_ITEMS 32
#define DKS_MENU_MAX_LABEL 64
#define DKS_MENU_MAX_SHORTCUT 16

// Forward declaration
typedef struct dks_menu dks_menu_t;
typedef struct dks_menu_item dks_menu_item_t;

// Menu item types
typedef enum {
    MENU_ITEM_NORMAL,
    MENU_ITEM_CHECKBOX,
    MENU_ITEM_RADIO,
    MENU_ITEM_SEPARATOR,
    MENU_ITEM_SUBMENU
} menu_item_type_t;

// Menu item callback
typedef void (*menu_item_callback_t)(dks_menu_item_t* item, void* data);

// Menu item structure
struct dks_menu_item {
    menu_item_type_t type;
    uint32_t id;

    // Content
    char label[DKS_MENU_MAX_LABEL];
    char shortcut[DKS_MENU_MAX_SHORTCUT];
    bmp_image_t* icon;

    // State
    bool enabled;
    bool checked;           // For checkbox/radio items
    bool hovered;

    // Submenu
    dks_menu_t* submenu;

    // Callback
    menu_item_callback_t on_click;
    void* callback_data;

    // Radio group (items with same group are mutually exclusive)
    uint32_t radio_group;

    // Computed bounds (set during render)
    graphics_rect_t bounds;

    // Linked list
    dks_menu_item_t* next;
    dks_menu_item_t* prev;
};

// Menu structure
struct dks_menu {
    // Items
    dks_menu_item_t* items;
    uint32_t item_count;

    // Position and size
    int32_t x, y;
    uint32_t width, height;

    // State
    bool visible;
    int32_t hover_index;
    dks_menu_item_t* hovered_item;

    // Parent/child relationship
    dks_menu_t* parent;
    dks_menu_t* open_submenu;
    dks_menu_item_t* parent_item;   // Item in parent that opened this submenu

    // Type (for context)
    enum {
        MENU_TYPE_CONTEXT,
        MENU_TYPE_START,
        MENU_TYPE_MENUBAR,
        MENU_TYPE_DROPDOWN
    } type;

    // Animation
    float opacity;
    bool animating;
};

// Menu creation and destruction
dks_menu_t* dks_menu_create(void);
void dks_menu_destroy(dks_menu_t* menu);

// Add items
dks_menu_item_t* dks_menu_add_item(dks_menu_t* menu, const char* label, menu_item_callback_t callback, void* data);
dks_menu_item_t* dks_menu_add_item_with_icon(dks_menu_t* menu, const char* label, bmp_image_t* icon, menu_item_callback_t callback, void* data);
dks_menu_item_t* dks_menu_add_item_with_shortcut(dks_menu_t* menu, const char* label, const char* shortcut, menu_item_callback_t callback, void* data);
dks_menu_item_t* dks_menu_add_checkbox(dks_menu_t* menu, const char* label, bool checked, menu_item_callback_t callback, void* data);
dks_menu_item_t* dks_menu_add_radio(dks_menu_t* menu, const char* label, uint32_t group, bool selected, menu_item_callback_t callback, void* data);
dks_menu_item_t* dks_menu_add_separator(dks_menu_t* menu);
dks_menu_item_t* dks_menu_add_submenu(dks_menu_t* menu, const char* label, dks_menu_t* submenu);

// Remove items
void dks_menu_remove_item(dks_menu_t* menu, dks_menu_item_t* item);
void dks_menu_clear(dks_menu_t* menu);

// Item operations
void dks_menu_item_set_enabled(dks_menu_item_t* item, bool enabled);
void dks_menu_item_set_checked(dks_menu_item_t* item, bool checked);
void dks_menu_item_set_label(dks_menu_item_t* item, const char* label);
void dks_menu_item_set_icon(dks_menu_item_t* item, bmp_image_t* icon);

// Show/hide menu
void dks_menu_show(dks_menu_t* menu, int32_t x, int32_t y);
void dks_menu_show_aligned(dks_menu_t* menu, const graphics_rect_t* anchor, alignment_t h_align, alignment_t v_align);
void dks_menu_hide(dks_menu_t* menu);
void dks_menu_hide_all(void);  // Hide all open menus

// Menu state
bool dks_menu_is_visible(dks_menu_t* menu);
bool dks_menu_any_visible(void);
dks_menu_t* dks_menu_get_active(void);

// Input handling
bool dks_menu_handle_mouse_move(int32_t x, int32_t y);
bool dks_menu_handle_mouse_button(int32_t x, int32_t y, uint8_t button, bool pressed);
bool dks_menu_handle_key(uint32_t keycode, uint32_t modifiers);
bool dks_menu_contains_point(dks_menu_t* menu, int32_t x, int32_t y);
dks_menu_item_t* dks_menu_hit_test(dks_menu_t* menu, int32_t x, int32_t y);

// Rendering
void dks_menu_render(dks_menu_t* menu, graphics_surface_t* surface, const dks_theme_t* theme);
void dks_menu_render_all(graphics_surface_t* surface, const dks_theme_t* theme);
void dks_menu_invalidate(dks_menu_t* menu);

// Calculate menu size
void dks_menu_calc_size(dks_menu_t* menu, const dks_theme_t* theme);

// Built-in context menus
dks_menu_t* dks_get_desktop_context_menu(void);
dks_menu_t* dks_get_window_context_menu(void* window);
dks_menu_t* dks_get_file_context_menu(const char* filepath, bool is_directory);
dks_menu_t* dks_get_text_context_menu(bool has_selection, bool can_paste);

// Start menu
dks_menu_t* dks_get_start_menu(void);
void dks_start_menu_add_app(const char* name, const char* app_id, bmp_image_t* icon);
void dks_start_menu_add_category(const char* name);
void dks_start_menu_rebuild(void);

// Menu bar (horizontal menu for windows)
typedef struct {
    dks_menu_t** menus;
    char** labels;
    uint32_t count;
    int32_t active_index;
    graphics_rect_t bounds;
    bool visible;
} dks_menubar_t;

dks_menubar_t* dks_menubar_create(void);
void dks_menubar_destroy(dks_menubar_t* menubar);
void dks_menubar_add_menu(dks_menubar_t* menubar, const char* label, dks_menu_t* menu);
void dks_menubar_render(dks_menubar_t* menubar, graphics_surface_t* surface, int32_t x, int32_t y, uint32_t width, const dks_theme_t* theme);
bool dks_menubar_handle_mouse(dks_menubar_t* menubar, int32_t x, int32_t y, uint8_t button, bool pressed);

#endif // DKS_MENU_H
