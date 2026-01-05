#ifndef KEYBOARD_INTERRUPT_HANDLER_H
#define KEYBOARD_INTERRUPT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    KEYBOARD_SUCCESS = 0,
    KEYBOARD_ERROR_INVALID_PARAMS,
    KEYBOARD_ERROR_NOT_INITIALIZED,
    KEYBOARD_ERROR_NO_DATA,
    KEYBOARD_ERROR_NO_SPACE,
    KEYBOARD_ERROR_DEVICE_REGISTRATION_FAILED,
    KEYBOARD_ERROR_HARDWARE_ERROR,
    KEYBOARD_ERROR_TIMEOUT
} keyboard_error_t;

typedef enum {
    KEYBOARD_LAYOUT_US = 0,
    KEYBOARD_LAYOUT_UK,
    KEYBOARD_LAYOUT_DE,
    KEYBOARD_LAYOUT_FR,
    KEYBOARD_LAYOUT_CUSTOM
} keyboard_layout_t;

typedef enum {
    KEY_UNKNOWN = 0,
    KEY_ESCAPE = 1,
    KEY_1 = 2, KEY_2 = 3, KEY_3 = 4, KEY_4 = 5, KEY_5 = 6,
    KEY_6 = 7, KEY_7 = 8, KEY_8 = 9, KEY_9 = 10, KEY_0 = 11,
    KEY_MINUS = 12, KEY_EQUALS = 13, KEY_BACKSPACE = 14, KEY_TAB = 15,
    KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19, KEY_T = 20,
    KEY_Y = 21, KEY_U = 22, KEY_I = 23, KEY_O = 24, KEY_P = 25,
    KEY_LEFT_BRACKET = 26, KEY_RIGHT_BRACKET = 27, KEY_ENTER = 28,
    KEY_LEFT_CTRL = 29, KEY_A = 30, KEY_S = 31, KEY_D = 32,
    KEY_F = 33, KEY_G = 34, KEY_H = 35, KEY_J = 36, KEY_K = 37,
    KEY_L = 38, KEY_SEMICOLON = 39, KEY_QUOTE = 40, KEY_GRAVE = 41,
    KEY_LEFT_SHIFT = 42, KEY_BACKSLASH = 43, KEY_Z = 44, KEY_X = 45,
    KEY_C = 46, KEY_V = 47, KEY_B = 48, KEY_N = 49, KEY_M = 50,
    KEY_COMMA = 51, KEY_PERIOD = 52, KEY_SLASH = 53, KEY_RIGHT_SHIFT = 54,
    KEY_KP_MULTIPLY = 55, KEY_LEFT_ALT = 56, KEY_SPACE = 57,
    KEY_CAPS_LOCK = 58, KEY_F1 = 59, KEY_F2 = 60, KEY_F3 = 61,
    KEY_F4 = 62, KEY_F5 = 63, KEY_F6 = 64, KEY_F7 = 65,
    KEY_F8 = 66, KEY_F9 = 67, KEY_F10 = 68, KEY_NUM_LOCK = 69,
    KEY_SCROLL_LOCK = 70, KEY_KP_7 = 71, KEY_KP_8 = 72, KEY_KP_9 = 73,
    KEY_KP_MINUS = 74, KEY_KP_4 = 75, KEY_KP_5 = 76, KEY_KP_6 = 77,
    KEY_KP_PLUS = 78, KEY_KP_1 = 79, KEY_KP_2 = 80, KEY_KP_3 = 81,
    KEY_KP_0 = 82, KEY_KP_PERIOD = 83, KEY_F11 = 87, KEY_F12 = 88,
    KEY_RIGHT_CTRL = 157, KEY_RIGHT_ALT = 184,
    KEY_UP = 200, KEY_PAGE_UP = 201, KEY_LEFT = 203, KEY_RIGHT = 205,
    KEY_END = 207, KEY_DOWN = 208, KEY_PAGE_DOWN = 209,
    KEY_INSERT = 210, KEY_DELETE = 211, KEY_HOME = 199
} keyboard_key_code_t;

typedef struct {
    keyboard_key_code_t keycode;
    uint8_t scancode;
    char ascii;
    bool pressed;
    bool extended;
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
    uint64_t timestamp;
} keyboard_key_event_t;

typedef struct {
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
    bool extended_scancode;
    uint8_t last_scancode;
    uint64_t last_keypress_time;
    uint64_t repeat_start_time;
    bool repeat_active;
} keyboard_modifier_state_t;

typedef struct {
    bool enable_key_repeat;
    bool enable_extended_scancodes;
    bool enable_modifier_tracking;
    bool enable_statistics;
    uint32_t repeat_delay_ms;
    uint32_t repeat_rate_ms;
    uint32_t buffer_size;
} keyboard_config_t;

typedef struct {
    bool filter_enabled;
    bool key_pressed_only;
    bool key_released_only;
    bool modifier_keys_only;
    bool printable_keys_only;
    bool function_keys_only;
    keyboard_key_code_t specific_key;
} keyboard_event_filter_t;

typedef struct {
    uint64_t total_interrupts;
    uint64_t total_scancodes;
    uint64_t total_key_events;
    uint64_t dropped_scancodes;
    uint64_t repeat_events;
    uint32_t buffer_scancodes_pending;
    uint32_t buffer_events_pending;
    uint32_t callbacks_registered;
} keyboard_statistics_t;

typedef void (*keyboard_event_callback_t)(const keyboard_key_event_t *event, void *user_data);

keyboard_error_t keyboard_interrupt_init(const keyboard_config_t *config);

keyboard_error_t keyboard_register_callback(keyboard_event_callback_t callback, 
                                          void *user_data,
                                          const keyboard_event_filter_t *filter);

keyboard_error_t keyboard_get_key_event(keyboard_key_event_t *event);

keyboard_error_t keyboard_get_scancode(uint8_t *scancode);

keyboard_error_t keyboard_get_modifier_state(keyboard_modifier_state_t *state);

keyboard_error_t keyboard_set_leds(bool caps_lock, bool num_lock, bool scroll_lock);

keyboard_error_t keyboard_get_statistics(keyboard_statistics_t *stats);

void keyboard_process_events(void);

bool keyboard_is_key_pressed(keyboard_key_code_t keycode);

bool keyboard_is_initialized(void);

size_t keyboard_get_pending_events(void);

static inline const char* keyboard_error_to_string(keyboard_error_t error) {
    switch (error) {
        case KEYBOARD_SUCCESS:
            return "Success";
        case KEYBOARD_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case KEYBOARD_ERROR_NOT_INITIALIZED:
            return "Keyboard not initialized";
        case KEYBOARD_ERROR_NO_DATA:
            return "No keyboard data available";
        case KEYBOARD_ERROR_NO_SPACE:
            return "No space for additional callbacks";
        case KEYBOARD_ERROR_DEVICE_REGISTRATION_FAILED:
            return "Failed to register keyboard device";
        case KEYBOARD_ERROR_HARDWARE_ERROR:
            return "Keyboard hardware error";
        case KEYBOARD_ERROR_TIMEOUT:
            return "Keyboard operation timeout";
        default:
            return "Unknown keyboard error";
    }
}

static inline const char* keyboard_key_to_string(keyboard_key_code_t key) {
    switch (key) {
        case KEY_ESCAPE: return "Escape";
        case KEY_1: return "1";
        case KEY_2: return "2";
        case KEY_3: return "3";
        case KEY_4: return "4";
        case KEY_5: return "5";
        case KEY_6: return "6";
        case KEY_7: return "7";
        case KEY_8: return "8";
        case KEY_9: return "9";
        case KEY_0: return "0";
        case KEY_MINUS: return "Minus";
        case KEY_EQUALS: return "Equals";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_TAB: return "Tab";
        case KEY_Q: return "Q";
        case KEY_W: return "W";
        case KEY_E: return "E";
        case KEY_R: return "R";
        case KEY_T: return "T";
        case KEY_Y: return "Y";
        case KEY_U: return "U";
        case KEY_I: return "I";
        case KEY_O: return "O";
        case KEY_P: return "P";
        case KEY_ENTER: return "Enter";
        case KEY_LEFT_CTRL: return "Left Ctrl";
        case KEY_A: return "A";
        case KEY_S: return "S";
        case KEY_D: return "D";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";
        case KEY_LEFT_SHIFT: return "Left Shift";
        case KEY_Z: return "Z";
        case KEY_X: return "X";
        case KEY_C: return "C";
        case KEY_V: return "V";
        case KEY_B: return "B";
        case KEY_N: return "N";
        case KEY_M: return "M";
        case KEY_RIGHT_SHIFT: return "Right Shift";
        case KEY_LEFT_ALT: return "Left Alt";
        case KEY_SPACE: return "Space";
        case KEY_CAPS_LOCK: return "Caps Lock";
        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";
        case KEY_NUM_LOCK: return "Num Lock";
        case KEY_SCROLL_LOCK: return "Scroll Lock";
        case KEY_UP: return "Up Arrow";
        case KEY_DOWN: return "Down Arrow";
        case KEY_LEFT: return "Left Arrow";
        case KEY_RIGHT: return "Right Arrow";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PAGE_UP: return "Page Up";
        case KEY_PAGE_DOWN: return "Page Down";
        case KEY_INSERT: return "Insert";
        case KEY_DELETE: return "Delete";
        default: return "Unknown";
    }
}

static inline keyboard_config_t keyboard_default_config(void) {
    return (keyboard_config_t){
        .enable_key_repeat = true,
        .enable_extended_scancodes = true,
        .enable_modifier_tracking = true,
        .enable_statistics = true,
        .repeat_delay_ms = 500,
        .repeat_rate_ms = 50,
        .buffer_size = 256
    };
}

static inline keyboard_config_t keyboard_realtime_config(void) {
    return (keyboard_config_t){
        .enable_key_repeat = false,
        .enable_extended_scancodes = true,
        .enable_modifier_tracking = true,
        .enable_statistics = false,
        .repeat_delay_ms = 0,
        .repeat_rate_ms = 0,
        .buffer_size = 64
    };
}

static inline keyboard_event_filter_t create_printable_filter(void) {
    return (keyboard_event_filter_t){
        .filter_enabled = true,
        .key_pressed_only = true,
        .key_released_only = false,
        .modifier_keys_only = false,
        .printable_keys_only = true,
        .function_keys_only = false,
        .specific_key = KEY_UNKNOWN
    };
}

static inline keyboard_event_filter_t create_modifier_filter(void) {
    return (keyboard_event_filter_t){
        .filter_enabled = true,
        .key_pressed_only = false,
        .key_released_only = false,
        .modifier_keys_only = true,
        .printable_keys_only = false,
        .function_keys_only = false,
        .specific_key = KEY_UNKNOWN
    };
}

static inline keyboard_event_filter_t create_key_filter(keyboard_key_code_t key) {
    return (keyboard_event_filter_t){
        .filter_enabled = true,
        .key_pressed_only = false,
        .key_released_only = false,
        .modifier_keys_only = false,
        .printable_keys_only = false,
        .function_keys_only = false,
        .specific_key = key
    };
}

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t value);
extern uint64_t rdtsc(void);
extern uint64_t tsc_frequency_hz;

#endif // KEYBOARD_INTERRUPT_HANDLER_H