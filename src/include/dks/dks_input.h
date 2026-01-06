#ifndef DKS_INPUT_H
#define DKS_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"
#include "../ps2_mouse.h"
#include "dks_core.h"

// Middle click actions
typedef enum {
    MIDDLE_CLICK_NONE,
    MIDDLE_CLICK_CLOSE_WINDOW,
    MIDDLE_CLICK_PASTE,
    MIDDLE_CLICK_AUTO_SCROLL,
    MIDDLE_CLICK_OPEN_LINK,
    MIDDLE_CLICK_CUSTOM
} middle_click_action_t;

// Drag operation types
typedef enum {
    DRAG_NONE = 0,
    DRAG_WINDOW_MOVE,
    DRAG_WINDOW_RESIZE,
    DRAG_SELECTION,
    DRAG_FILE,
    DRAG_WIDGET
} drag_type_t;

// Input state structure
typedef struct {
    // Mouse position
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t mouse_prev_x;
    int32_t mouse_prev_y;

    // Button states
    bool left_button;
    bool right_button;
    bool middle_button;
    bool left_button_prev;
    bool right_button_prev;
    bool middle_button_prev;

    // Click tracking for double-click detection
    uint32_t last_click_time;
    int32_t last_click_x;
    int32_t last_click_y;
    uint8_t last_click_button;
    uint32_t click_count;

    // Drag state
    drag_type_t drag_type;
    dks_window_t* drag_window;
    int32_t drag_start_x;
    int32_t drag_start_y;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
    dks_hit_result_t resize_edge;

    // Keyboard modifiers
    bool ctrl_held;
    bool alt_held;
    bool shift_held;
    bool super_held;

    // Focus
    dks_window_t* focused_window;
    widget_t* focused_widget;
    widget_t* hovered_widget;

    // Captured widget (receives all mouse events)
    widget_t* captured_widget;

} dks_input_state_t;

// Hotkey registration
typedef struct {
    uint32_t id;
    uint32_t modifiers;     // KEY_MOD_* flags
    uint32_t keycode;
    void (*callback)(void* data);
    void* callback_data;
    bool enabled;
} dks_hotkey_t;

// Initialization
void dks_input_init(void);
void dks_input_shutdown(void);

// Get input state
dks_input_state_t* dks_input_get_state(void);

// Mouse position
void dks_input_get_mouse_pos(int32_t* x, int32_t* y);
void dks_input_set_mouse_pos(int32_t x, int32_t y);

// Button state queries
bool dks_input_is_left_button_down(void);
bool dks_input_is_right_button_down(void);
bool dks_input_is_middle_button_down(void);
bool dks_input_is_button_down(mouse_button_t button);
bool dks_input_was_button_pressed(mouse_button_t button);
bool dks_input_was_button_released(mouse_button_t button);

// Modifier state queries
bool dks_input_is_ctrl_down(void);
bool dks_input_is_alt_down(void);
bool dks_input_is_shift_down(void);
bool dks_input_is_super_down(void);
uint32_t dks_input_get_modifiers(void);

// Event processing (called from main loop)
void dks_input_process_mouse_event(const ps2_mouse_event_t* event);
void dks_input_process_key_event(uint32_t keycode, bool pressed);
void dks_input_process_char(char c);

// Focus management
void dks_input_set_focus(widget_t* widget);
void dks_input_clear_focus(void);
widget_t* dks_input_get_focused_widget(void);
void dks_input_focus_next(void);
void dks_input_focus_prev(void);

// Mouse capture
void dks_input_capture_mouse(widget_t* widget);
void dks_input_release_mouse(void);
widget_t* dks_input_get_mouse_capture(void);

// Drag operations
void dks_input_begin_drag(drag_type_t type, void* data);
void dks_input_end_drag(void);
bool dks_input_is_dragging(void);
drag_type_t dks_input_get_drag_type(void);

// Double-click timing
void dks_input_set_double_click_time(uint32_t ms);
uint32_t dks_input_get_double_click_time(void);

// Middle click configuration
void dks_input_set_middle_click_action(middle_click_action_t action);
middle_click_action_t dks_input_get_middle_click_action(void);
void dks_input_set_middle_click_callback(void (*callback)(int32_t x, int32_t y, void* data), void* data);

// Hotkey management
uint32_t dks_hotkey_register(uint32_t modifiers, uint32_t keycode, void (*callback)(void* data), void* data);
void dks_hotkey_unregister(uint32_t id);
void dks_hotkey_set_enabled(uint32_t id, bool enabled);
void dks_hotkey_unregister_all(void);

// Built-in hotkeys (registered automatically)
void dks_input_register_default_hotkeys(void);

// Clipboard operations
void dks_clipboard_copy(const char* text);
void dks_clipboard_cut(const char* text);
const char* dks_clipboard_paste(void);
bool dks_clipboard_has_text(void);
void dks_clipboard_clear(void);

// Cursor updates based on hit test
void dks_input_update_cursor(dks_hit_result_t hit);

// Event creation helpers
void dks_input_create_mouse_event(widget_event_t* event, widget_event_type_t type);
void dks_input_create_key_event(widget_event_t* event, widget_event_type_t type, uint32_t keycode, char c);

#endif // DKS_INPUT_H
