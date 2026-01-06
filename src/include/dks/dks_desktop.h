#ifndef DKS_DESKTOP_H
#define DKS_DESKTOP_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "../bmp.h"
#include "dks_theme.h"

// Maximum desktop icons
#define DKS_MAX_DESKTOP_ICONS 64
#define DKS_ICON_LABEL_MAX 64
#define DKS_ICON_PATH_MAX 256

// Wallpaper display mode
typedef enum {
    WALLPAPER_STRETCH,
    WALLPAPER_CENTER,
    WALLPAPER_TILE,
    WALLPAPER_FIT,
    WALLPAPER_FILL,
    WALLPAPER_SPAN
} wallpaper_mode_t;

// Desktop icon structure
typedef struct desktop_icon {
    uint32_t id;
    bool in_use;

    // Display
    char label[DKS_ICON_LABEL_MAX];
    bmp_image_t* icon;

    // Position (grid-based or free)
    int32_t grid_x, grid_y;     // Grid position
    int32_t x, y;               // Actual position

    // Target
    char target_path[DKS_ICON_PATH_MAX];
    char app_id[32];

    // Type
    enum {
        ICON_TYPE_APP,
        ICON_TYPE_FILE,
        ICON_TYPE_FOLDER,
        ICON_TYPE_LINK
    } type;

    // State
    bool selected;
    bool hovered;
    bool editing;               // Renaming

    // Computed bounds
    graphics_rect_t icon_bounds;
    graphics_rect_t label_bounds;
    graphics_rect_t total_bounds;

} desktop_icon_t;

// Desktop state
typedef struct {
    // Wallpaper
    bmp_image_t* wallpaper;
    char wallpaper_path[DKS_ICON_PATH_MAX];
    wallpaper_mode_t wallpaper_mode;
    graphics_color_t background_color;

    // Icons
    desktop_icon_t icons[DKS_MAX_DESKTOP_ICONS];
    uint32_t icon_count;

    // Icon layout
    uint32_t icon_size;         // Icon image size
    uint32_t icon_spacing;      // Space between icons
    uint32_t grid_cols;
    uint32_t grid_rows;
    bool auto_arrange;
    bool align_to_grid;
    bool sort_by_name;

    // Selection
    desktop_icon_t* selected_icon;
    bool multi_select;
    int32_t selection_count;

    // Selection rectangle (rubber band)
    bool selecting;
    int32_t select_start_x, select_start_y;
    int32_t select_end_x, select_end_y;

    // Editing
    desktop_icon_t* editing_icon;
    char edit_buffer[DKS_ICON_LABEL_MAX];
    uint32_t edit_cursor;

    // Dirty flag
    bool dirty;

} dks_desktop_t;

// Desktop initialization
void dks_desktop_init(void);
void dks_desktop_shutdown(void);

// Wallpaper
void dks_desktop_set_wallpaper(const char* path);
void dks_desktop_set_wallpaper_image(bmp_image_t* image);
void dks_desktop_set_wallpaper_mode(wallpaper_mode_t mode);
void dks_desktop_set_background_color(graphics_color_t color);
void dks_desktop_clear_wallpaper(void);
const char* dks_desktop_get_wallpaper_path(void);

// Desktop icons
desktop_icon_t* dks_desktop_add_icon(const char* label, const char* target, bmp_image_t* icon);
desktop_icon_t* dks_desktop_add_app_icon(const char* app_id, const char* label, bmp_image_t* icon);
desktop_icon_t* dks_desktop_add_file_icon(const char* path);
desktop_icon_t* dks_desktop_add_folder_icon(const char* path, const char* label);
void dks_desktop_remove_icon(desktop_icon_t* icon);
void dks_desktop_remove_icon_by_id(uint32_t id);
void dks_desktop_clear_icons(void);

// Icon operations
void dks_desktop_select_icon(desktop_icon_t* icon);
void dks_desktop_deselect_icon(desktop_icon_t* icon);
void dks_desktop_deselect_all(void);
void dks_desktop_select_all(void);
void dks_desktop_toggle_icon_selection(desktop_icon_t* icon);
desktop_icon_t* dks_desktop_get_selected_icon(void);
uint32_t dks_desktop_get_selected_count(void);

// Icon layout
void dks_desktop_set_icon_size(uint32_t size);
void dks_desktop_set_icon_spacing(uint32_t spacing);
void dks_desktop_set_auto_arrange(bool auto_arrange);
void dks_desktop_arrange_icons(void);
void dks_desktop_sort_icons(bool by_name);

// Icon editing (rename)
void dks_desktop_begin_rename(desktop_icon_t* icon);
void dks_desktop_end_rename(bool apply);
void dks_desktop_cancel_rename(void);
bool dks_desktop_is_renaming(void);

// Hit testing
desktop_icon_t* dks_desktop_hit_test(int32_t x, int32_t y);
bool dks_desktop_point_on_icon(desktop_icon_t* icon, int32_t x, int32_t y);

// Input handling
bool dks_desktop_handle_mouse_move(int32_t x, int32_t y);
bool dks_desktop_handle_mouse_button(int32_t x, int32_t y, uint8_t button, bool pressed);
bool dks_desktop_handle_double_click(int32_t x, int32_t y);
bool dks_desktop_handle_key(uint32_t keycode, uint32_t modifiers);
bool dks_desktop_handle_char(char c);

// Actions
void dks_desktop_open_icon(desktop_icon_t* icon);
void dks_desktop_delete_selected(void);
void dks_desktop_copy_selected(void);
void dks_desktop_cut_selected(void);
void dks_desktop_paste(void);

// Context menu
void dks_desktop_show_context_menu(int32_t x, int32_t y);
void dks_desktop_show_icon_context_menu(desktop_icon_t* icon, int32_t x, int32_t y);

// Rendering
void dks_desktop_render(graphics_surface_t* surface, const dks_theme_t* theme);
void dks_desktop_render_wallpaper(graphics_surface_t* surface);
void dks_desktop_render_icons(graphics_surface_t* surface, const dks_theme_t* theme);
void dks_desktop_render_selection_rect(graphics_surface_t* surface, const dks_theme_t* theme);
void dks_desktop_invalidate(void);

// Get available area (excluding panel)
void dks_desktop_get_work_area(graphics_rect_t* rect);

// Window switcher (Alt+Tab)
typedef struct {
    bool visible;
    dks_window_t** windows;
    uint32_t window_count;
    int32_t selected_index;
    graphics_rect_t bounds;
    float opacity;
} dks_window_switcher_t;

void dks_window_switcher_show(void);
void dks_window_switcher_hide(void);
void dks_window_switcher_next(void);
void dks_window_switcher_prev(void);
void dks_window_switcher_select(void);
void dks_window_switcher_cancel(void);
bool dks_window_switcher_is_visible(void);
void dks_window_switcher_render(graphics_surface_t* surface, const dks_theme_t* theme);
bool dks_window_switcher_handle_key(uint32_t keycode, uint32_t modifiers);

#endif // DKS_DESKTOP_H
