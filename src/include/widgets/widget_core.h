#ifndef WIDGET_CORE_H
#define WIDGET_CORE_H

#include "widget_types.h"
#include "../dks/dks_theme.h"
#include "../bmp.h"

// Maximum widget text length
#define WIDGET_MAX_TEXT 256
#define WIDGET_MAX_NAME 32
#define WIDGET_MAX_CHILDREN 64

// Base widget structure
struct widget {
    // Identity
    widget_type_t type;
    uint32_t id;
    char name[WIDGET_MAX_NAME];

    // Geometry (relative to parent)
    int32_t x, y;
    uint32_t width, height;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;
    widget_spacing_t padding;
    widget_spacing_t margin;

    // State
    uint32_t state;  // Combination of widget_state_t flags
    bool visible;
    bool enabled;
    bool focusable;
    bool dirty;      // Needs repaint

    // Content
    char text[WIDGET_MAX_TEXT];
    bmp_image_t* icon;
    graphics_color_t fg_color;
    graphics_color_t bg_color;
    alignment_t text_align;

    // Layout
    layout_type_t layout;
    alignment_t h_align;
    alignment_t v_align;
    int32_t spacing;

    // Event callbacks
    widget_callback_t on_click;
    widget_callback_t on_double_click;
    widget_callback_t on_right_click;
    widget_callback_t on_middle_click;
    widget_callback_t on_mouse_enter;
    widget_callback_t on_mouse_leave;
    widget_callback_t on_mouse_move;
    widget_callback_t on_focus;
    widget_callback_t on_blur;
    widget_callback_t on_key_down;
    widget_callback_t on_key_up;
    widget_callback_t on_change;
    widget_callback_t on_scroll;
    widget_paint_callback_t on_paint;

    // User data for callbacks
    void* user_data;
    void* callback_data;

    // Hierarchy
    widget_t* parent;
    widget_t* first_child;
    widget_t* last_child;
    widget_t* prev_sibling;
    widget_t* next_sibling;
    uint32_t child_count;

    // Type-specific data (cast based on type)
    void* type_data;

    // Computed absolute position (set during layout)
    int32_t abs_x, abs_y;
};

// Button-specific data
typedef struct {
    bool toggle_mode;
    bool toggled;
    graphics_color_t hover_color;
    graphics_color_t press_color;
} widget_button_data_t;

// Text input specific data
typedef struct {
    uint32_t cursor_pos;
    uint32_t selection_start;
    uint32_t selection_end;
    uint32_t scroll_offset;
    bool password_mode;
    char password_char;
    char placeholder[WIDGET_MAX_TEXT];
    uint32_t max_length;
    bool multiline;
    bool readonly;
} widget_textinput_data_t;

// Checkbox specific data
typedef struct {
    bool checked;
    char label[WIDGET_MAX_TEXT];
} widget_checkbox_data_t;

// Dropdown specific data
typedef struct {
    char** options;
    uint32_t option_count;
    int32_t selected_index;
    bool expanded;
    uint32_t visible_items;
} widget_dropdown_data_t;

// Slider specific data
typedef struct {
    int32_t min_value;
    int32_t max_value;
    int32_t current_value;
    int32_t step;
    bool vertical;
    bool show_value;
} widget_slider_data_t;

// Scroll container specific data
typedef struct {
    int32_t scroll_x;
    int32_t scroll_y;
    int32_t content_width;
    int32_t content_height;
    bool show_h_scrollbar;
    bool show_v_scrollbar;
    int32_t scrollbar_size;
} widget_scroll_data_t;

// List view item
typedef struct {
    char text[WIDGET_MAX_TEXT];
    bmp_image_t* icon;
    void* data;
    bool selected;
} list_view_item_t;

// List view specific data
typedef struct {
    list_view_item_t* items;
    uint32_t item_count;
    uint32_t item_capacity;
    int32_t selected_index;
    uint32_t item_height;
    int32_t scroll_offset;
    bool multi_select;
} widget_listview_data_t;

// Widget creation and destruction
widget_t* widget_create(widget_type_t type);
widget_t* widget_create_with_bounds(widget_type_t type, int32_t x, int32_t y, uint32_t w, uint32_t h);
void widget_destroy(widget_t* widget);
void widget_destroy_recursive(widget_t* widget);

// Property setters
void widget_set_position(widget_t* widget, int32_t x, int32_t y);
void widget_set_size(widget_t* widget, uint32_t width, uint32_t height);
void widget_set_bounds(widget_t* widget, int32_t x, int32_t y, uint32_t w, uint32_t h);
void widget_set_text(widget_t* widget, const char* text);
void widget_set_icon(widget_t* widget, bmp_image_t* icon);
void widget_set_enabled(widget_t* widget, bool enabled);
void widget_set_visible(widget_t* widget, bool visible);
void widget_set_focusable(widget_t* widget, bool focusable);
void widget_set_colors(widget_t* widget, graphics_color_t fg, graphics_color_t bg);
void widget_set_user_data(widget_t* widget, void* data);

// Property getters
const char* widget_get_text(widget_t* widget);
bool widget_is_enabled(widget_t* widget);
bool widget_is_visible(widget_t* widget);
bool widget_has_state(widget_t* widget, widget_state_t state);
void* widget_get_user_data(widget_t* widget);
void widget_get_absolute_position(widget_t* widget, int32_t* x, int32_t* y);

// State management
void widget_set_state(widget_t* widget, widget_state_t state, bool set);
void widget_add_state(widget_t* widget, widget_state_t state);
void widget_remove_state(widget_t* widget, widget_state_t state);
void widget_toggle_state(widget_t* widget, widget_state_t state);

// Hierarchy management
void widget_add_child(widget_t* parent, widget_t* child);
void widget_remove_child(widget_t* parent, widget_t* child);
void widget_insert_child(widget_t* parent, widget_t* child, uint32_t index);
void widget_remove_from_parent(widget_t* widget);
widget_t* widget_get_child(widget_t* parent, uint32_t index);
widget_t* widget_find_by_id(widget_t* root, uint32_t id);
widget_t* widget_find_by_name(widget_t* root, const char* name);

// Event handling
bool widget_dispatch_event(widget_t* widget, widget_event_t* event);
void widget_set_callback(widget_t* widget, widget_event_type_t event_type, widget_callback_t callback, void* data);

// Hit testing
widget_t* widget_hit_test(widget_t* root, int32_t x, int32_t y);
bool widget_contains_point(widget_t* widget, int32_t x, int32_t y);

// Layout and rendering
void widget_layout(widget_t* widget);
void widget_invalidate(widget_t* widget);
void widget_invalidate_recursive(widget_t* widget);
void widget_paint(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
void widget_paint_recursive(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);

// Focus management
void widget_focus(widget_t* widget);
void widget_blur(widget_t* widget);
widget_t* widget_get_focused(widget_t* root);
widget_t* widget_get_next_focusable(widget_t* current);
widget_t* widget_get_prev_focusable(widget_t* current);

// Specific widget creation helpers
widget_t* widget_create_button(const char* text, widget_callback_t on_click, void* data);
widget_t* widget_create_label(const char* text);
widget_t* widget_create_textinput(const char* placeholder);
widget_t* widget_create_checkbox(const char* label, bool checked);
widget_t* widget_create_dropdown(const char** options, uint32_t count);
widget_t* widget_create_slider(int32_t min, int32_t max, int32_t value);
widget_t* widget_create_icon(bmp_image_t* icon, const char* tooltip);

// Text input helpers
void textinput_set_text(widget_t* widget, const char* text);
void textinput_insert_char(widget_t* widget, char c);
void textinput_delete_char(widget_t* widget, bool forward);
void textinput_move_cursor(widget_t* widget, int32_t delta);
void textinput_select_all(widget_t* widget);
void textinput_clear_selection(widget_t* widget);

// Checkbox helpers
bool checkbox_is_checked(widget_t* widget);
void checkbox_set_checked(widget_t* widget, bool checked);
void checkbox_toggle(widget_t* widget);

// Dropdown helpers
int32_t dropdown_get_selected(widget_t* widget);
void dropdown_set_selected(widget_t* widget, int32_t index);
void dropdown_add_option(widget_t* widget, const char* option);
void dropdown_clear_options(widget_t* widget);

// Slider helpers
int32_t slider_get_value(widget_t* widget);
void slider_set_value(widget_t* widget, int32_t value);
void slider_set_range(widget_t* widget, int32_t min, int32_t max);

// List view helpers
void listview_add_item(widget_t* widget, const char* text, bmp_image_t* icon, void* data);
void listview_remove_item(widget_t* widget, uint32_t index);
void listview_clear(widget_t* widget);
int32_t listview_get_selected(widget_t* widget);
void listview_set_selected(widget_t* widget, int32_t index);

#endif // WIDGET_CORE_H
