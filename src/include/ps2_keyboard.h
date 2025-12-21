#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include "types.h"
#include "interrupt.h"

// Keyboard commands
#define KB_CMD_SET_LEDS              0xED
#define KB_CMD_ECHO                  0xEE
#define KB_CMD_GET_SET_SCANCODE_SET  0xF0
#define KB_CMD_IDENTIFY              0xF2
#define KB_CMD_SET_TYPEMATIC         0xF3
#define KB_CMD_ENABLE_SCANNING       0xF4
#define KB_CMD_DISABLE_SCANNING      0xF5
#define KB_CMD_SET_DEFAULT_PARAMS    0xF6
#define KB_CMD_RESEND                0xFE
#define KB_CMD_RESET                 0xFF

// Scan code sets
#define KB_SCANCODE_SET_1            0x01
#define KB_SCANCODE_SET_2            0x02
#define KB_SCANCODE_SET_3            0x03

// LED states
#define KB_LED_SCROLL_LOCK           0x01
#define KB_LED_NUM_LOCK              0x02
#define KB_LED_CAPS_LOCK             0x04

// Keyboard layout identifiers
typedef enum {
    KEYBOARD_LAYOUT_US = 0,
    KEYBOARD_LAYOUT_GB,
    KEYBOARD_LAYOUT_MAX
} keyboard_layout_id_t;

// Key states
typedef enum {
    KEY_STATE_PRESSED,
    KEY_STATE_RELEASED,
    KEY_STATE_REPEAT
} key_state_t;

// Key codes - standardized across all keyboard drivers
typedef enum {
    KEY_UNKNOWN = 0,
    
    // Function keys
    KEY_F1 = 0x10, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    
    // Number row
    KEY_ESC = 0x20, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUALS,
    KEY_BACKSPACE, KEY_TAB,
    
    // Letters
    KEY_Q = 0x30, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y,
    KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET,
    KEY_ENTER, KEY_LEFT_CTRL, KEY_A, KEY_S,
    KEY_D = 0x40, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K,
    KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE,
    KEY_LEFT_SHIFT, KEY_BACKSLASH, KEY_OEM_102, KEY_Z, KEY_X, KEY_C, KEY_V,
    KEY_B = 0x50, KEY_N, KEY_M, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
    KEY_RIGHT_SHIFT, KEY_KEYPAD_MULTIPLY, KEY_LEFT_ALT, KEY_SPACE,
    KEY_CAPS_LOCK,
    
    // Keypad
    KEY_KEYPAD_7 = 0x60, KEY_KEYPAD_8, KEY_KEYPAD_9, KEY_KEYPAD_MINUS,
    KEY_KEYPAD_4, KEY_KEYPAD_5, KEY_KEYPAD_6, KEY_KEYPAD_PLUS,
    KEY_KEYPAD_1, KEY_KEYPAD_2, KEY_KEYPAD_3, KEY_KEYPAD_0,
    KEY_KEYPAD_PERIOD, KEY_KEYPAD_ENTER, KEY_KEYPAD_DIVIDE,
    
    // Arrow keys and navigation
    KEY_HOME = 0x70, KEY_UP, KEY_PAGE_UP, KEY_LEFT, KEY_RIGHT,
    KEY_END, KEY_DOWN, KEY_PAGE_DOWN, KEY_INSERT, KEY_DELETE,
    
    // Modifier keys
    KEY_RIGHT_CTRL = 0x80, KEY_RIGHT_ALT, KEY_LEFT_GUI, KEY_RIGHT_GUI,
    KEY_MENU, KEY_NUM_LOCK, KEY_SCROLL_LOCK,
    
    // Multimedia keys
    KEY_VOLUME_DOWN = 0x90, KEY_VOLUME_UP, KEY_MUTE, KEY_POWER,
    KEY_SLEEP, KEY_WAKE, KEY_WWW_SEARCH, KEY_WWW_FAVORITES,
    KEY_WWW_REFRESH, KEY_WWW_STOP, KEY_WWW_FORWARD, KEY_WWW_BACK,
    KEY_MY_COMPUTER, KEY_EMAIL, KEY_MEDIA_SELECT, KEY_CALCULATOR,
    
    KEY_PRINT_SCREEN = 0xA0, KEY_PAUSE,
    
    KEY_MAX = 0xFF
} key_code_t;

// Keyboard event structure
typedef struct {
    key_code_t key_code;
    key_state_t state;
    uint8 scancode_raw[8];  // Raw scancode sequence
    uint8 scancode_length;
    bool shift;
    bool ctrl;
    bool alt;
    bool gui;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
    uint32 timestamp;
    char ascii;  // ASCII representation (if available)
} keyboard_event_t;

// Keyboard driver state
typedef struct {
    uint8 current_scancode_set;
    bool leds_caps_lock;
    bool leds_num_lock;
    bool leds_scroll_lock;
    bool modifiers_left_shift;
    bool modifiers_right_shift;
    bool modifiers_left_ctrl;
    bool modifiers_right_ctrl;
    bool modifiers_left_alt;
    bool modifiers_right_alt;
    bool modifiers_left_gui;
    bool modifiers_right_gui;
    bool scanning_enabled;
    uint8 state_machine_state;
    uint8 scancode_buffer[8];
    uint8 scancode_buffer_pos;
    bool extended_scancode;
    bool break_code;
    bool pause_sequence;
    uint8 pause_sequence_length;
    uint8 pause_expected_length;
    keyboard_layout_id_t active_layout;
    bool key_down[KEY_MAX];
} keyboard_driver_state_t;

// Callback function type for keyboard events
typedef void (*keyboard_event_callback_t)(const keyboard_event_t* event);

// Function prototypes
int ps2_keyboard_init(void);
void ps2_keyboard_irq_handler(struct interrupt_frame* frame, uint32 error_code);
bool ps2_keyboard_set_leds(uint8 led_state);
bool ps2_keyboard_set_scancode_set(uint8 scancode_set);
bool ps2_keyboard_set_typematic(uint8 rate, uint8 delay);
bool ps2_keyboard_enable_scanning(void);
bool ps2_keyboard_disable_scanning(void);
bool ps2_keyboard_reset(void);
key_code_t ps2_keyboard_scancode_to_keycode(uint8* scancode, uint8 length, uint8 scancode_set);
void ps2_keyboard_register_event_callback(keyboard_event_callback_t callback);
keyboard_driver_state_t* ps2_keyboard_get_state(void);
bool ps2_keyboard_poll_ascii(char* out_char);
void ps2_keyboard_clear_ascii_buffer(void);
void ps2_keyboard_select_layout(keyboard_layout_id_t layout);

#endif
