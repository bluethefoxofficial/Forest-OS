/*
 * DKS Input Handling
 * Mouse and keyboard event routing for the desktop environment
 */

#include "../include/dks/dks_input.h"
#include "../include/dks/dks_core.h"
#include "../include/dks/dks_menu.h"
#include "../include/dks/dks_panel.h"
#include "../include/dks/dks_desktop.h"
#include "../include/widgets/widget_core.h"
#include <string.h>

// External timer function
extern uint32_t timer_get_ticks(void);

// Global input state
static dks_input_state_t input_state;

// Hotkey storage
#define MAX_HOTKEYS 32
static dks_hotkey_t hotkeys[MAX_HOTKEYS];
static uint32_t hotkey_count = 0;
static uint32_t next_hotkey_id = 1;

// Clipboard
#define CLIPBOARD_SIZE 4096
static char clipboard_buffer[CLIPBOARD_SIZE];
static bool clipboard_has_data = false;

// Middle click action
static middle_click_action_t middle_click_action = MIDDLE_CLICK_PASTE;
static void (*middle_click_callback)(int32_t, int32_t, void*) = NULL;
static void* middle_click_data = NULL;

// Double-click settings
static uint32_t double_click_time = 400;  // ms
static uint32_t double_click_distance = 4;  // pixels

void dks_input_init(void) {
    memset(&input_state, 0, sizeof(dks_input_state_t));
    memset(hotkeys, 0, sizeof(hotkeys));
    hotkey_count = 0;
    clipboard_buffer[0] = '\0';
    clipboard_has_data = false;
}

void dks_input_shutdown(void) {
    dks_hotkey_unregister_all();
}

dks_input_state_t* dks_input_get_state(void) {
    return &input_state;
}

// Mouse position

void dks_input_get_mouse_pos(int32_t* x, int32_t* y) {
    if (x) *x = input_state.mouse_x;
    if (y) *y = input_state.mouse_y;
}

void dks_input_set_mouse_pos(int32_t x, int32_t y) {
    input_state.mouse_x = x;
    input_state.mouse_y = y;
}

// Button state queries

bool dks_input_is_left_button_down(void) {
    return input_state.left_button;
}

bool dks_input_is_right_button_down(void) {
    return input_state.right_button;
}

bool dks_input_is_middle_button_down(void) {
    return input_state.middle_button;
}

bool dks_input_is_button_down(mouse_button_t button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return input_state.left_button;
        case MOUSE_BUTTON_RIGHT: return input_state.right_button;
        case MOUSE_BUTTON_MIDDLE: return input_state.middle_button;
        default: return false;
    }
}

bool dks_input_was_button_pressed(mouse_button_t button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return input_state.left_button && !input_state.left_button_prev;
        case MOUSE_BUTTON_RIGHT: return input_state.right_button && !input_state.right_button_prev;
        case MOUSE_BUTTON_MIDDLE: return input_state.middle_button && !input_state.middle_button_prev;
        default: return false;
    }
}

bool dks_input_was_button_released(mouse_button_t button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT: return !input_state.left_button && input_state.left_button_prev;
        case MOUSE_BUTTON_RIGHT: return !input_state.right_button && input_state.right_button_prev;
        case MOUSE_BUTTON_MIDDLE: return !input_state.middle_button && input_state.middle_button_prev;
        default: return false;
    }
}

// Modifier queries

bool dks_input_is_ctrl_down(void) {
    return input_state.ctrl_held;
}

bool dks_input_is_alt_down(void) {
    return input_state.alt_held;
}

bool dks_input_is_shift_down(void) {
    return input_state.shift_held;
}

bool dks_input_is_super_down(void) {
    return input_state.super_held;
}

uint32_t dks_input_get_modifiers(void) {
    uint32_t mods = KEY_MOD_NONE;
    if (input_state.ctrl_held) mods |= KEY_MOD_CTRL;
    if (input_state.alt_held) mods |= KEY_MOD_ALT;
    if (input_state.shift_held) mods |= KEY_MOD_SHIFT;
    if (input_state.super_held) mods |= KEY_MOD_SUPER;
    return mods;
}

// Event processing

void dks_input_process_mouse_event(const ps2_mouse_event_t* event) {
    if (!event) return;

    // Save previous state
    input_state.mouse_prev_x = input_state.mouse_x;
    input_state.mouse_prev_y = input_state.mouse_y;
    input_state.left_button_prev = input_state.left_button;
    input_state.right_button_prev = input_state.right_button;
    input_state.middle_button_prev = input_state.middle_button;

    // Update position
    input_state.mouse_x = event->x;
    input_state.mouse_y = event->y;

    // Clamp to screen bounds
    uint32_t screen_w = dks_get_screen_width();
    uint32_t screen_h = dks_get_screen_height();
    if (input_state.mouse_x < 0) input_state.mouse_x = 0;
    if (input_state.mouse_y < 0) input_state.mouse_y = 0;
    if ((uint32_t)input_state.mouse_x >= screen_w) input_state.mouse_x = screen_w - 1;
    if ((uint32_t)input_state.mouse_y >= screen_h) input_state.mouse_y = screen_h - 1;

    // Update button states
    input_state.left_button = event->left_button;
    input_state.right_button = event->right_button;
    input_state.middle_button = event->middle_button;

    int32_t mx = input_state.mouse_x;
    int32_t my = input_state.mouse_y;

    // Check for menu interactions first
    if (dks_menu_any_visible()) {
        if (dks_menu_handle_mouse_move(mx, my)) {
            // Menu handled mouse move
        }
        if (dks_input_was_button_pressed(MOUSE_BUTTON_LEFT)) {
            if (!dks_menu_handle_mouse_button(mx, my, MOUSE_BUTTON_LEFT, true)) {
                // Click outside menu - close it
                dks_menu_hide_all();
            }
        }
        return;
    }

    // Check panel interactions
    if (dks_panel_contains_point(mx, my)) {
        dks_panel_handle_mouse_move(mx, my);
        if (dks_input_was_button_pressed(MOUSE_BUTTON_LEFT)) {
            dks_panel_handle_mouse_button(mx, my, MOUSE_BUTTON_LEFT, true);
        }
        return;
    }

    // Handle dragging
    if (input_state.drag_type != DRAG_NONE) {
        if (input_state.left_button) {
            // Continue drag
            if (input_state.drag_type == DRAG_WINDOW_MOVE && input_state.drag_window) {
                int32_t new_x = mx - input_state.drag_offset_x;
                int32_t new_y = my - input_state.drag_offset_y;
                dks_window_set_position(input_state.drag_window, new_x, new_y);
            } else if (input_state.drag_type == DRAG_WINDOW_RESIZE && input_state.drag_window) {
                // Handle resize based on edge
                dks_window_t* win = input_state.drag_window;
                int32_t dx = mx - input_state.drag_start_x;
                int32_t dy = my - input_state.drag_start_y;

                uint32_t new_w = win->width;
                uint32_t new_h = win->height;
                int32_t new_x = win->x;
                int32_t new_y = win->y;

                switch (input_state.resize_edge) {
                    case DKS_HIT_RESIZE_E:
                        new_w = input_state.drag_offset_x + dx;
                        break;
                    case DKS_HIT_RESIZE_W:
                        new_x = win->x + dx;
                        new_w = input_state.drag_offset_x - dx;
                        break;
                    case DKS_HIT_RESIZE_S:
                        new_h = input_state.drag_offset_y + dy;
                        break;
                    case DKS_HIT_RESIZE_N:
                        new_y = win->y + dy;
                        new_h = input_state.drag_offset_y - dy;
                        break;
                    case DKS_HIT_RESIZE_SE:
                        new_w = input_state.drag_offset_x + dx;
                        new_h = input_state.drag_offset_y + dy;
                        break;
                    case DKS_HIT_RESIZE_SW:
                        new_x = win->x + dx;
                        new_w = input_state.drag_offset_x - dx;
                        new_h = input_state.drag_offset_y + dy;
                        break;
                    case DKS_HIT_RESIZE_NE:
                        new_w = input_state.drag_offset_x + dx;
                        new_y = win->y + dy;
                        new_h = input_state.drag_offset_y - dy;
                        break;
                    case DKS_HIT_RESIZE_NW:
                        new_x = win->x + dx;
                        new_w = input_state.drag_offset_x - dx;
                        new_y = win->y + dy;
                        new_h = input_state.drag_offset_y - dy;
                        break;
                    default:
                        break;
                }

                // Apply min/max constraints
                if (new_w < win->min_width) new_w = win->min_width;
                if (new_h < win->min_height) new_h = win->min_height;
                if (win->max_width > 0 && new_w > win->max_width) new_w = win->max_width;
                if (win->max_height > 0 && new_h > win->max_height) new_h = win->max_height;

                dks_window_set_position(win, new_x, new_y);
                dks_window_set_size(win, new_w, new_h);
            }
        } else {
            // End drag
            dks_input_end_drag();
        }
        return;
    }

    // Find window under cursor
    dks_window_t* window = dks_window_at_point(mx, my);

    // Update cursor based on hit test
    dks_hit_result_t hit = DKS_HIT_NONE;
    if (window) {
        hit = dks_window_hit_test(window, mx, my);
        dks_input_update_cursor(hit);
    } else {
        dks_set_cursor(DKS_CURSOR_ARROW);
    }

    // Handle button presses
    if (dks_input_was_button_pressed(MOUSE_BUTTON_LEFT)) {
        uint32_t current_time = timer_get_ticks();
        int32_t click_dist = (mx - input_state.last_click_x) * (mx - input_state.last_click_x) +
                             (my - input_state.last_click_y) * (my - input_state.last_click_y);

        // Check for double-click
        bool is_double_click = (current_time - input_state.last_click_time < double_click_time) &&
                               (click_dist < (int32_t)(double_click_distance * double_click_distance)) &&
                               (input_state.last_click_button == MOUSE_BUTTON_LEFT);

        if (window) {
            dks_window_focus(window);
            dks_window_bring_to_front(window);

            switch (hit) {
                case DKS_HIT_TITLEBAR:
                    // Start window drag
                    input_state.drag_type = DRAG_WINDOW_MOVE;
                    input_state.drag_window = window;
                    input_state.drag_offset_x = mx - window->x;
                    input_state.drag_offset_y = my - window->y;
                    break;

                case DKS_HIT_CLOSE:
                    dks_window_close(window);
                    break;

                case DKS_HIT_MINIMIZE:
                    dks_window_minimize(window);
                    break;

                case DKS_HIT_MAXIMIZE:
                    if (window->state == DKS_WINDOW_STATE_MAXIMIZED) {
                        dks_window_restore(window);
                    } else {
                        dks_window_maximize(window);
                    }
                    break;

                case DKS_HIT_RESIZE_N:
                case DKS_HIT_RESIZE_S:
                case DKS_HIT_RESIZE_E:
                case DKS_HIT_RESIZE_W:
                case DKS_HIT_RESIZE_NW:
                case DKS_HIT_RESIZE_NE:
                case DKS_HIT_RESIZE_SW:
                case DKS_HIT_RESIZE_SE:
                    if (window->flags & DKS_WINDOW_FLAG_RESIZABLE) {
                        input_state.drag_type = DRAG_WINDOW_RESIZE;
                        input_state.drag_window = window;
                        input_state.drag_start_x = mx;
                        input_state.drag_start_y = my;
                        input_state.drag_offset_x = window->width;
                        input_state.drag_offset_y = window->height;
                        input_state.resize_edge = hit;
                    }
                    break;

                case DKS_HIT_CLIENT:
                    // Send click event to window's widget tree
                    if (window->root_widget) {
                        widget_event_t evt = {0};
                        evt.type = is_double_click ? WIDGET_EVENT_DOUBLE_CLICK : WIDGET_EVENT_CLICK;
                        evt.mouse_x = mx;
                        evt.mouse_y = my;
                        evt.mouse_button = MOUSE_BUTTON_LEFT;
                        evt.timestamp = current_time;

                        widget_t* hit_widget = widget_hit_test(window->root_widget, mx, my);
                        if (hit_widget) {
                            widget_focus(hit_widget);
                            widget_dispatch_event(hit_widget, &evt);
                        }
                    }
                    break;

                default:
                    break;
            }
        } else {
            // Click on desktop
            if (is_double_click) {
                dks_desktop_handle_double_click(mx, my);
            } else {
                dks_desktop_handle_mouse_button(mx, my, MOUSE_BUTTON_LEFT, true);
            }
        }

        input_state.last_click_time = current_time;
        input_state.last_click_x = mx;
        input_state.last_click_y = my;
        input_state.last_click_button = MOUSE_BUTTON_LEFT;

    } else if (dks_input_was_button_pressed(MOUSE_BUTTON_RIGHT)) {
        // Right click - show context menu
        if (window) {
            dks_window_focus(window);
            // Show window context menu
            dks_menu_t* menu = dks_get_window_context_menu(window);
            if (menu) {
                dks_menu_show(menu, mx, my);
            }
        } else {
            // Desktop right-click
            dks_desktop_show_context_menu(mx, my);
        }

    } else if (dks_input_was_button_pressed(MOUSE_BUTTON_MIDDLE)) {
        // Middle click action
        switch (middle_click_action) {
            case MIDDLE_CLICK_CLOSE_WINDOW:
                if (window) {
                    dks_window_close(window);
                }
                break;
            case MIDDLE_CLICK_PASTE:
                if (input_state.focused_widget &&
                    input_state.focused_widget->type == WIDGET_TYPE_TEXT_INPUT) {
                    const char* text = dks_clipboard_paste();
                    if (text) {
                        while (*text) {
                            textinput_insert_char(input_state.focused_widget, *text++);
                        }
                    }
                }
                break;
            case MIDDLE_CLICK_CUSTOM:
                if (middle_click_callback) {
                    middle_click_callback(mx, my, middle_click_data);
                }
                break;
            default:
                break;
        }
    }

    // Handle hover updates
    if (window && window->root_widget) {
        widget_t* hovered = widget_hit_test(window->root_widget, mx, my);
        if (hovered != input_state.hovered_widget) {
            // Send leave event to old widget
            if (input_state.hovered_widget) {
                widget_event_t leave_evt = {0};
                leave_evt.type = WIDGET_EVENT_MOUSE_LEAVE;
                widget_dispatch_event(input_state.hovered_widget, &leave_evt);
            }
            // Send enter event to new widget
            if (hovered) {
                widget_event_t enter_evt = {0};
                enter_evt.type = WIDGET_EVENT_MOUSE_ENTER;
                widget_dispatch_event(hovered, &enter_evt);
            }
            input_state.hovered_widget = hovered;
        }
    }
}

void dks_input_process_key_event(uint32_t keycode, bool pressed) {
    // Update modifier states
    switch (keycode) {
        case 0x1D: // Left Ctrl
        case 0x9D: // Right Ctrl
            input_state.ctrl_held = pressed;
            break;
        case 0x38: // Left Alt
        case 0xB8: // Right Alt
            input_state.alt_held = pressed;
            break;
        case 0x2A: // Left Shift
        case 0x36: // Right Shift
            input_state.shift_held = pressed;
            break;
        case 0x5B: // Left Super/Windows
        case 0x5C: // Right Super/Windows
            input_state.super_held = pressed;
            break;
    }

    if (!pressed) return;  // Only process key presses, not releases

    // Check hotkeys
    uint32_t mods = dks_input_get_modifiers();
    for (uint32_t i = 0; i < hotkey_count; i++) {
        if (hotkeys[i].enabled &&
            hotkeys[i].keycode == keycode &&
            hotkeys[i].modifiers == mods) {
            if (hotkeys[i].callback) {
                hotkeys[i].callback(hotkeys[i].callback_data);
            }
            return;
        }
    }

    // Handle window switcher
    if (dks_window_switcher_is_visible()) {
        if (dks_window_switcher_handle_key(keycode, mods)) {
            return;
        }
    }

    // Handle global shortcuts
    if (mods & KEY_MOD_ALT) {
        switch (keycode) {
            case 0x0F: // Tab - window switcher
                if (!dks_window_switcher_is_visible()) {
                    dks_window_switcher_show();
                } else {
                    if (mods & KEY_MOD_SHIFT) {
                        dks_window_switcher_prev();
                    } else {
                        dks_window_switcher_next();
                    }
                }
                return;
            case 0x3E: // F4 - close window
                if (input_state.focused_window) {
                    dks_window_close(input_state.focused_window);
                }
                return;
        }
    }

    // Super key - start menu
    if (keycode == 0x5B && !mods) {
        dks_start_menu_toggle();
        return;
    }

    // Send to focused widget
    if (input_state.focused_widget) {
        widget_event_t evt = {0};
        evt.type = WIDGET_EVENT_KEY_DOWN;
        evt.key_code = keycode;
        evt.modifiers = mods;
        widget_dispatch_event(input_state.focused_widget, &evt);
    }
}

void dks_input_process_char(char c) {
    if (input_state.focused_widget) {
        widget_event_t evt = {0};
        evt.type = WIDGET_EVENT_KEY_DOWN;
        evt.key_char = c;
        evt.modifiers = dks_input_get_modifiers();
        widget_dispatch_event(input_state.focused_widget, &evt);
    }
}

// Focus management

void dks_input_set_focus(widget_t* widget) {
    if (input_state.focused_widget != widget) {
        if (input_state.focused_widget) {
            widget_blur(input_state.focused_widget);
        }
        input_state.focused_widget = widget;
        if (widget) {
            widget_focus(widget);
        }
    }
}

void dks_input_clear_focus(void) {
    dks_input_set_focus(NULL);
}

widget_t* dks_input_get_focused_widget(void) {
    return input_state.focused_widget;
}

void dks_input_focus_next(void) {
    widget_t* next = widget_get_next_focusable(input_state.focused_widget);
    if (next) {
        dks_input_set_focus(next);
    }
}

void dks_input_focus_prev(void) {
    widget_t* prev = widget_get_prev_focusable(input_state.focused_widget);
    if (prev) {
        dks_input_set_focus(prev);
    }
}

// Mouse capture

void dks_input_capture_mouse(widget_t* widget) {
    input_state.captured_widget = widget;
}

void dks_input_release_mouse(void) {
    input_state.captured_widget = NULL;
}

widget_t* dks_input_get_mouse_capture(void) {
    return input_state.captured_widget;
}

// Drag operations

void dks_input_begin_drag(drag_type_t type, void* data) {
    input_state.drag_type = type;
    input_state.drag_start_x = input_state.mouse_x;
    input_state.drag_start_y = input_state.mouse_y;
    (void)data;  // Could store drag data if needed
}

void dks_input_end_drag(void) {
    input_state.drag_type = DRAG_NONE;
    input_state.drag_window = NULL;
}

bool dks_input_is_dragging(void) {
    return input_state.drag_type != DRAG_NONE;
}

drag_type_t dks_input_get_drag_type(void) {
    return input_state.drag_type;
}

// Double-click timing

void dks_input_set_double_click_time(uint32_t ms) {
    double_click_time = ms;
}

uint32_t dks_input_get_double_click_time(void) {
    return double_click_time;
}

// Middle click configuration

void dks_input_set_middle_click_action(middle_click_action_t action) {
    middle_click_action = action;
}

middle_click_action_t dks_input_get_middle_click_action(void) {
    return middle_click_action;
}

void dks_input_set_middle_click_callback(void (*callback)(int32_t x, int32_t y, void* data), void* data) {
    middle_click_callback = callback;
    middle_click_data = data;
}

// Hotkey management

uint32_t dks_hotkey_register(uint32_t modifiers, uint32_t keycode, void (*callback)(void* data), void* data) {
    if (hotkey_count >= MAX_HOTKEYS) return 0;

    uint32_t id = next_hotkey_id++;
    hotkeys[hotkey_count].id = id;
    hotkeys[hotkey_count].modifiers = modifiers;
    hotkeys[hotkey_count].keycode = keycode;
    hotkeys[hotkey_count].callback = callback;
    hotkeys[hotkey_count].callback_data = data;
    hotkeys[hotkey_count].enabled = true;
    hotkey_count++;

    return id;
}

void dks_hotkey_unregister(uint32_t id) {
    for (uint32_t i = 0; i < hotkey_count; i++) {
        if (hotkeys[i].id == id) {
            // Shift remaining hotkeys
            for (uint32_t j = i; j < hotkey_count - 1; j++) {
                hotkeys[j] = hotkeys[j + 1];
            }
            hotkey_count--;
            return;
        }
    }
}

void dks_hotkey_set_enabled(uint32_t id, bool enabled) {
    for (uint32_t i = 0; i < hotkey_count; i++) {
        if (hotkeys[i].id == id) {
            hotkeys[i].enabled = enabled;
            return;
        }
    }
}

void dks_hotkey_unregister_all(void) {
    hotkey_count = 0;
}

void dks_input_register_default_hotkeys(void) {
    // Register default system hotkeys
    // These are handled directly in dks_input_process_key_event
    // but could be registered here for customization
}

// Clipboard operations

void dks_clipboard_copy(const char* text) {
    if (!text) {
        clipboard_buffer[0] = '\0';
        clipboard_has_data = false;
        return;
    }
    strncpy(clipboard_buffer, text, CLIPBOARD_SIZE - 1);
    clipboard_buffer[CLIPBOARD_SIZE - 1] = '\0';
    clipboard_has_data = true;
}

void dks_clipboard_cut(const char* text) {
    dks_clipboard_copy(text);
}

const char* dks_clipboard_paste(void) {
    return clipboard_has_data ? clipboard_buffer : NULL;
}

bool dks_clipboard_has_text(void) {
    return clipboard_has_data;
}

void dks_clipboard_clear(void) {
    clipboard_buffer[0] = '\0';
    clipboard_has_data = false;
}

// Cursor updates

void dks_input_update_cursor(dks_hit_result_t hit) {
    switch (hit) {
        case DKS_HIT_RESIZE_N:
        case DKS_HIT_RESIZE_S:
            dks_set_cursor(DKS_CURSOR_RESIZE_V);
            break;
        case DKS_HIT_RESIZE_E:
        case DKS_HIT_RESIZE_W:
            dks_set_cursor(DKS_CURSOR_RESIZE_H);
            break;
        case DKS_HIT_RESIZE_NW:
        case DKS_HIT_RESIZE_SE:
            dks_set_cursor(DKS_CURSOR_RESIZE_DIAG1);
            break;
        case DKS_HIT_RESIZE_NE:
        case DKS_HIT_RESIZE_SW:
            dks_set_cursor(DKS_CURSOR_RESIZE_DIAG2);
            break;
        case DKS_HIT_TITLEBAR:
            dks_set_cursor(DKS_CURSOR_MOVE);
            break;
        case DKS_HIT_CLIENT:
            dks_set_cursor(DKS_CURSOR_ARROW);
            break;
        default:
            dks_set_cursor(DKS_CURSOR_ARROW);
            break;
    }
}

// Event creation helpers

void dks_input_create_mouse_event(widget_event_t* event, widget_event_type_t type) {
    if (!event) return;
    memset(event, 0, sizeof(widget_event_t));
    event->type = type;
    event->mouse_x = input_state.mouse_x;
    event->mouse_y = input_state.mouse_y;
    event->mouse_dx = input_state.mouse_x - input_state.mouse_prev_x;
    event->mouse_dy = input_state.mouse_y - input_state.mouse_prev_y;
    event->modifiers = dks_input_get_modifiers();
    event->timestamp = timer_get_ticks();
}

void dks_input_create_key_event(widget_event_t* event, widget_event_type_t type, uint32_t keycode, char c) {
    if (!event) return;
    memset(event, 0, sizeof(widget_event_t));
    event->type = type;
    event->key_code = keycode;
    event->key_char = c;
    event->modifiers = dks_input_get_modifiers();
    event->timestamp = timer_get_ticks();
}
