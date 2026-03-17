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

// PS/2 driver internal key codes
// Note: These are different from the Linux evdev key codes in input_event.h
// Use these for PS/2 driver internals; use input_event.h KEY_* for system-wide events
typedef enum {
    PS2_KEY_UNKNOWN = 0,

    // Function keys
    PS2_KEY_F1 = 0x10, PS2_KEY_F2, PS2_KEY_F3, PS2_KEY_F4, PS2_KEY_F5, PS2_KEY_F6,
    PS2_KEY_F7, PS2_KEY_F8, PS2_KEY_F9, PS2_KEY_F10, PS2_KEY_F11, PS2_KEY_F12,

    // Number row
    PS2_KEY_ESC = 0x20, PS2_KEY_1, PS2_KEY_2, PS2_KEY_3, PS2_KEY_4, PS2_KEY_5,
    PS2_KEY_6, PS2_KEY_7, PS2_KEY_8, PS2_KEY_9, PS2_KEY_0, PS2_KEY_MINUS, PS2_KEY_EQUALS,
    PS2_KEY_BACKSPACE, PS2_KEY_TAB,

    // Letters
    PS2_KEY_Q = 0x30, PS2_KEY_W, PS2_KEY_E, PS2_KEY_R, PS2_KEY_T, PS2_KEY_Y,
    PS2_KEY_U, PS2_KEY_I, PS2_KEY_O, PS2_KEY_P, PS2_KEY_LEFT_BRACKET, PS2_KEY_RIGHT_BRACKET,
    PS2_KEY_ENTER, PS2_KEY_LEFT_CTRL, PS2_KEY_A, PS2_KEY_S,
    PS2_KEY_D = 0x40, PS2_KEY_F, PS2_KEY_G, PS2_KEY_H, PS2_KEY_J, PS2_KEY_K,
    PS2_KEY_L, PS2_KEY_SEMICOLON, PS2_KEY_APOSTROPHE, PS2_KEY_GRAVE,
    PS2_KEY_LEFT_SHIFT, PS2_KEY_BACKSLASH, PS2_KEY_OEM_102, PS2_KEY_Z, PS2_KEY_X, PS2_KEY_C, PS2_KEY_V,
    PS2_KEY_B = 0x50, PS2_KEY_N, PS2_KEY_M, PS2_KEY_COMMA, PS2_KEY_PERIOD, PS2_KEY_SLASH,
    PS2_KEY_RIGHT_SHIFT, PS2_KEY_KEYPAD_MULTIPLY, PS2_KEY_LEFT_ALT, PS2_KEY_SPACE,
    PS2_KEY_CAPS_LOCK,

    // Keypad
    PS2_KEY_KEYPAD_7 = 0x60, PS2_KEY_KEYPAD_8, PS2_KEY_KEYPAD_9, PS2_KEY_KEYPAD_MINUS,
    PS2_KEY_KEYPAD_4, PS2_KEY_KEYPAD_5, PS2_KEY_KEYPAD_6, PS2_KEY_KEYPAD_PLUS,
    PS2_KEY_KEYPAD_1, PS2_KEY_KEYPAD_2, PS2_KEY_KEYPAD_3, PS2_KEY_KEYPAD_0,
    PS2_KEY_KEYPAD_PERIOD, PS2_KEY_KEYPAD_ENTER, PS2_KEY_KEYPAD_DIVIDE,

    // Arrow keys and navigation
    PS2_KEY_HOME = 0x70, PS2_KEY_UP, PS2_KEY_PAGE_UP, PS2_KEY_LEFT, PS2_KEY_RIGHT,
    PS2_KEY_END, PS2_KEY_DOWN, PS2_KEY_PAGE_DOWN, PS2_KEY_INSERT, PS2_KEY_DELETE,

    // Modifier keys
    PS2_KEY_RIGHT_CTRL = 0x80, PS2_KEY_RIGHT_ALT, PS2_KEY_LEFT_GUI, PS2_KEY_RIGHT_GUI,
    PS2_KEY_MENU, PS2_KEY_NUM_LOCK, PS2_KEY_SCROLL_LOCK,

    // Multimedia keys
    PS2_KEY_VOLUME_DOWN = 0x90, PS2_KEY_VOLUME_UP, PS2_KEY_MUTE, PS2_KEY_POWER,
    PS2_KEY_SLEEP, PS2_KEY_WAKE, PS2_KEY_WWW_SEARCH, PS2_KEY_WWW_FAVORITES,
    PS2_KEY_WWW_REFRESH, PS2_KEY_WWW_STOP, PS2_KEY_WWW_FORWARD, PS2_KEY_WWW_BACK,
    PS2_KEY_MY_COMPUTER, PS2_KEY_EMAIL, PS2_KEY_MEDIA_SELECT, PS2_KEY_CALCULATOR,

    PS2_KEY_PRINT_SCREEN = 0xA0, PS2_KEY_PAUSE,

    PS2_KEY_MAX = 0xFF
} ps2_key_code_t;

// Alias for backwards compatibility within PS/2 driver code
typedef ps2_key_code_t key_code_t;

// Backward-compatibility macros for KEY_* names (only if input_event.h not included)
// These allow existing PS/2 driver code to continue using KEY_* names
#ifndef KEY_ESC  // input_event.h defines KEY_ESC, so check for it
#define KEY_UNKNOWN         PS2_KEY_UNKNOWN
#define KEY_F1              PS2_KEY_F1
#define KEY_F2              PS2_KEY_F2
#define KEY_F3              PS2_KEY_F3
#define KEY_F4              PS2_KEY_F4
#define KEY_F5              PS2_KEY_F5
#define KEY_F6              PS2_KEY_F6
#define KEY_F7              PS2_KEY_F7
#define KEY_F8              PS2_KEY_F8
#define KEY_F9              PS2_KEY_F9
#define KEY_F10             PS2_KEY_F10
#define KEY_F11             PS2_KEY_F11
#define KEY_F12             PS2_KEY_F12
#define KEY_ESC             PS2_KEY_ESC
#define KEY_1               PS2_KEY_1
#define KEY_2               PS2_KEY_2
#define KEY_3               PS2_KEY_3
#define KEY_4               PS2_KEY_4
#define KEY_5               PS2_KEY_5
#define KEY_6               PS2_KEY_6
#define KEY_7               PS2_KEY_7
#define KEY_8               PS2_KEY_8
#define KEY_9               PS2_KEY_9
#define KEY_0               PS2_KEY_0
#define KEY_MINUS           PS2_KEY_MINUS
#define KEY_EQUALS          PS2_KEY_EQUALS
#define KEY_BACKSPACE       PS2_KEY_BACKSPACE
#define KEY_TAB             PS2_KEY_TAB
#define KEY_Q               PS2_KEY_Q
#define KEY_W               PS2_KEY_W
#define KEY_E               PS2_KEY_E
#define KEY_R               PS2_KEY_R
#define KEY_T               PS2_KEY_T
#define KEY_Y               PS2_KEY_Y
#define KEY_U               PS2_KEY_U
#define KEY_I               PS2_KEY_I
#define KEY_O               PS2_KEY_O
#define KEY_P               PS2_KEY_P
#define KEY_LEFT_BRACKET    PS2_KEY_LEFT_BRACKET
#define KEY_RIGHT_BRACKET   PS2_KEY_RIGHT_BRACKET
#define KEY_ENTER           PS2_KEY_ENTER
#define KEY_LEFT_CTRL       PS2_KEY_LEFT_CTRL
#define KEY_A               PS2_KEY_A
#define KEY_S               PS2_KEY_S
#define KEY_D               PS2_KEY_D
#define KEY_F               PS2_KEY_F
#define KEY_G               PS2_KEY_G
#define KEY_H               PS2_KEY_H
#define KEY_J               PS2_KEY_J
#define KEY_K               PS2_KEY_K
#define KEY_L               PS2_KEY_L
#define KEY_SEMICOLON       PS2_KEY_SEMICOLON
#define KEY_APOSTROPHE      PS2_KEY_APOSTROPHE
#define KEY_GRAVE           PS2_KEY_GRAVE
#define KEY_LEFT_SHIFT      PS2_KEY_LEFT_SHIFT
#define KEY_BACKSLASH       PS2_KEY_BACKSLASH
#define KEY_OEM_102         PS2_KEY_OEM_102
#define KEY_Z               PS2_KEY_Z
#define KEY_X               PS2_KEY_X
#define KEY_C               PS2_KEY_C
#define KEY_V               PS2_KEY_V
#define KEY_B               PS2_KEY_B
#define KEY_N               PS2_KEY_N
#define KEY_M               PS2_KEY_M
#define KEY_COMMA           PS2_KEY_COMMA
#define KEY_PERIOD          PS2_KEY_PERIOD
#define KEY_SLASH           PS2_KEY_SLASH
#define KEY_RIGHT_SHIFT     PS2_KEY_RIGHT_SHIFT
#define KEY_KEYPAD_MULTIPLY PS2_KEY_KEYPAD_MULTIPLY
#define KEY_LEFT_ALT        PS2_KEY_LEFT_ALT
#define KEY_SPACE           PS2_KEY_SPACE
#define KEY_CAPS_LOCK       PS2_KEY_CAPS_LOCK
#define KEY_KEYPAD_7        PS2_KEY_KEYPAD_7
#define KEY_KEYPAD_8        PS2_KEY_KEYPAD_8
#define KEY_KEYPAD_9        PS2_KEY_KEYPAD_9
#define KEY_KEYPAD_MINUS    PS2_KEY_KEYPAD_MINUS
#define KEY_KEYPAD_4        PS2_KEY_KEYPAD_4
#define KEY_KEYPAD_5        PS2_KEY_KEYPAD_5
#define KEY_KEYPAD_6        PS2_KEY_KEYPAD_6
#define KEY_KEYPAD_PLUS     PS2_KEY_KEYPAD_PLUS
#define KEY_KEYPAD_1        PS2_KEY_KEYPAD_1
#define KEY_KEYPAD_2        PS2_KEY_KEYPAD_2
#define KEY_KEYPAD_3        PS2_KEY_KEYPAD_3
#define KEY_KEYPAD_0        PS2_KEY_KEYPAD_0
#define KEY_KEYPAD_PERIOD   PS2_KEY_KEYPAD_PERIOD
#define KEY_KEYPAD_ENTER    PS2_KEY_KEYPAD_ENTER
#define KEY_KEYPAD_DIVIDE   PS2_KEY_KEYPAD_DIVIDE
#define KEY_HOME            PS2_KEY_HOME
#define KEY_UP              PS2_KEY_UP
#define KEY_PAGE_UP         PS2_KEY_PAGE_UP
#define KEY_LEFT            PS2_KEY_LEFT
#define KEY_RIGHT           PS2_KEY_RIGHT
#define KEY_END             PS2_KEY_END
#define KEY_DOWN            PS2_KEY_DOWN
#define KEY_PAGE_DOWN       PS2_KEY_PAGE_DOWN
#define KEY_INSERT          PS2_KEY_INSERT
#define KEY_DELETE          PS2_KEY_DELETE
#define KEY_RIGHT_CTRL      PS2_KEY_RIGHT_CTRL
#define KEY_RIGHT_ALT       PS2_KEY_RIGHT_ALT
#define KEY_LEFT_GUI        PS2_KEY_LEFT_GUI
#define KEY_RIGHT_GUI       PS2_KEY_RIGHT_GUI
#define KEY_MENU            PS2_KEY_MENU
#define KEY_NUM_LOCK        PS2_KEY_NUM_LOCK
#define KEY_SCROLL_LOCK     PS2_KEY_SCROLL_LOCK
#define KEY_VOLUME_DOWN     PS2_KEY_VOLUME_DOWN
#define KEY_VOLUME_UP       PS2_KEY_VOLUME_UP
#define KEY_MUTE            PS2_KEY_MUTE
#define KEY_POWER           PS2_KEY_POWER
#define KEY_SLEEP           PS2_KEY_SLEEP
#define KEY_WAKE            PS2_KEY_WAKE
#define KEY_WWW_SEARCH      PS2_KEY_WWW_SEARCH
#define KEY_WWW_FAVORITES   PS2_KEY_WWW_FAVORITES
#define KEY_WWW_REFRESH     PS2_KEY_WWW_REFRESH
#define KEY_WWW_STOP        PS2_KEY_WWW_STOP
#define KEY_WWW_FORWARD     PS2_KEY_WWW_FORWARD
#define KEY_WWW_BACK        PS2_KEY_WWW_BACK
#define KEY_MY_COMPUTER     PS2_KEY_MY_COMPUTER
#define KEY_EMAIL           PS2_KEY_EMAIL
#define KEY_MEDIA_SELECT    PS2_KEY_MEDIA_SELECT
#define KEY_CALCULATOR      PS2_KEY_CALCULATOR
#define KEY_PRINT_SCREEN    PS2_KEY_PRINT_SCREEN
#define KEY_PAUSE           PS2_KEY_PAUSE
#define KEY_MAX             PS2_KEY_MAX
#endif /* KEY_ESC */

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
uint8 ps2_keyboard_get_scancode_set(void);
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
void ps2_keyboard_debug_status(void);
void ps2_keyboard_process_scancode(uint8 scancode);
void ps2_keyboard_poll(void);

// Device presence checking and hot reload
bool ps2_keyboard_is_present(void);
int ps2_keyboard_reinit(void);

#endif
