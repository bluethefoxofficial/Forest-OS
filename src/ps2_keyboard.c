#include "include/interrupt.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_controller.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/keyboard_layout.h"

#ifndef PS2_KEYBOARD_DEFAULT_LAYOUT
#define PS2_KEYBOARD_DEFAULT_LAYOUT KEYBOARD_LAYOUT_US
#endif

// Helper functions
static void kbd_memset(void *ptr, int value, size_t num) {
    memory_set((uint8*)ptr, (uint8)value, (uint32)num);
}

static void kbd_memcpy(void *dest, const void *src, size_t num) {
    memory_copy((char*)src, (char*)dest, (int)num);
}

static keyboard_driver_state_t kbd_state;
static keyboard_event_callback_t event_callback = NULL;

#define PS2_ASCII_BUFFER_SIZE 128
static char ascii_buffer[PS2_ASCII_BUFFER_SIZE];
static volatile uint32 ascii_buffer_head = 0;
static volatile uint32 ascii_buffer_tail = 0;

static void ps2_keyboard_enqueue_ascii(char ch) {
    uint32 next_head = (ascii_buffer_head + 1) % PS2_ASCII_BUFFER_SIZE;
    if (next_head == ascii_buffer_tail) {
        ascii_buffer_tail = (ascii_buffer_tail + 1) % PS2_ASCII_BUFFER_SIZE;
    }
    ascii_buffer[ascii_buffer_head] = ch;
    ascii_buffer_head = next_head;
}

bool ps2_keyboard_poll_ascii(char* out_char) {
    if (!out_char) {
        return false;
    }
    if (ascii_buffer_head == ascii_buffer_tail) {
        return false;
    }
    *out_char = ascii_buffer[ascii_buffer_tail];
    ascii_buffer_tail = (ascii_buffer_tail + 1) % PS2_ASCII_BUFFER_SIZE;
    return true;
}

void ps2_keyboard_clear_ascii_buffer(void) {
    ascii_buffer_head = ascii_buffer_tail = 0;
}

// Scan code set 1 to key code mapping
static const key_code_t scancode_set1_to_keycode[256] = {
    [0x01] = KEY_ESC,
    [0x02] = KEY_1, [0x03] = KEY_2, [0x04] = KEY_3, [0x05] = KEY_4,
    [0x06] = KEY_5, [0x07] = KEY_6, [0x08] = KEY_7, [0x09] = KEY_8,
    [0x0A] = KEY_9, [0x0B] = KEY_0, [0x0C] = KEY_MINUS, [0x0D] = KEY_EQUALS,
    [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    
    [0x10] = KEY_Q, [0x11] = KEY_W, [0x12] = KEY_E, [0x13] = KEY_R,
    [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
    [0x18] = KEY_O, [0x19] = KEY_P, [0x1A] = KEY_LEFT_BRACKET, [0x1B] = KEY_RIGHT_BRACKET,
    [0x1C] = KEY_ENTER, [0x1D] = KEY_LEFT_CTRL,
    
    [0x1E] = KEY_A, [0x1F] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F,
    [0x22] = KEY_G, [0x23] = KEY_H, [0x24] = KEY_J, [0x25] = KEY_K,
    [0x26] = KEY_L, [0x27] = KEY_SEMICOLON, [0x28] = KEY_APOSTROPHE,
    [0x29] = KEY_GRAVE, [0x2A] = KEY_LEFT_SHIFT, [0x2B] = KEY_BACKSLASH,
    
    [0x2C] = KEY_Z, [0x2D] = KEY_X, [0x2E] = KEY_C, [0x2F] = KEY_V,
    [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M, [0x33] = KEY_COMMA,
    [0x34] = KEY_PERIOD, [0x35] = KEY_SLASH, [0x36] = KEY_RIGHT_SHIFT,
    [0x37] = KEY_KEYPAD_MULTIPLY, [0x38] = KEY_LEFT_ALT, [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPS_LOCK,
    
    [0x3B] = KEY_F1, [0x3C] = KEY_F2, [0x3D] = KEY_F3, [0x3E] = KEY_F4,
    [0x3F] = KEY_F5, [0x40] = KEY_F6, [0x41] = KEY_F7, [0x42] = KEY_F8,
    [0x43] = KEY_F9, [0x44] = KEY_F10, [0x45] = KEY_NUM_LOCK,
    [0x46] = KEY_SCROLL_LOCK,
    
    [0x47] = KEY_KEYPAD_7, [0x48] = KEY_KEYPAD_8, [0x49] = KEY_KEYPAD_9,
    [0x4A] = KEY_KEYPAD_MINUS, [0x4B] = KEY_KEYPAD_4, [0x4C] = KEY_KEYPAD_5,
    [0x4D] = KEY_KEYPAD_6, [0x4E] = KEY_KEYPAD_PLUS, [0x4F] = KEY_KEYPAD_1,
    [0x50] = KEY_KEYPAD_2, [0x51] = KEY_KEYPAD_3, [0x52] = KEY_KEYPAD_0,
    [0x53] = KEY_KEYPAD_PERIOD,
    
    [0x56] = KEY_OEM_102,
    [0x57] = KEY_F11, [0x58] = KEY_F12,
};

// Extended scan codes (0xE0 prefix)
static const key_code_t extended_scancode_set1_to_keycode[256] = {
    [0x1C] = KEY_KEYPAD_ENTER, [0x1D] = KEY_RIGHT_CTRL,
    [0x35] = KEY_KEYPAD_DIVIDE, [0x38] = KEY_RIGHT_ALT,
    [0x47] = KEY_HOME, [0x48] = KEY_UP, [0x49] = KEY_PAGE_UP,
    [0x4B] = KEY_LEFT, [0x4D] = KEY_RIGHT, [0x4F] = KEY_END,
    [0x50] = KEY_DOWN, [0x51] = KEY_PAGE_DOWN, [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE, [0x5B] = KEY_LEFT_GUI, [0x5C] = KEY_RIGHT_GUI,
    [0x5D] = KEY_MENU,
    
    // Multimedia keys
    [0x10] = KEY_WWW_SEARCH, [0x19] = KEY_MUTE, [0x20] = KEY_VOLUME_DOWN,
    [0x22] = KEY_CALCULATOR, [0x24] = KEY_WWW_STOP, [0x2E] = KEY_VOLUME_UP,
    [0x30] = KEY_WWW_FORWARD, [0x32] = KEY_WWW_BACK, [0x38] = KEY_WWW_BACK,
    [0x3A] = KEY_MY_COMPUTER, [0x48] = KEY_EMAIL, [0x50] = KEY_MEDIA_SELECT,
    [0x5E] = KEY_POWER, [0x5F] = KEY_SLEEP, [0x63] = KEY_WAKE,
    [0x65] = KEY_WWW_SEARCH, [0x66] = KEY_WWW_FAVORITES, [0x67] = KEY_WWW_REFRESH,
};

// Scan code set 2 to key code mapping (make codes only)
static const key_code_t scancode_set2_to_keycode[256] = {
    [0x01] = KEY_F9, [0x03] = KEY_F5, [0x04] = KEY_F3, [0x05] = KEY_F1,
    [0x06] = KEY_F2, [0x07] = KEY_F12, [0x09] = KEY_F10, [0x0A] = KEY_F8,
    [0x0B] = KEY_F6, [0x0C] = KEY_F4, [0x0D] = KEY_TAB, [0x0E] = KEY_GRAVE,
    [0x11] = KEY_LEFT_ALT, [0x12] = KEY_LEFT_SHIFT, [0x14] = KEY_LEFT_CTRL,
    [0x15] = KEY_Q, [0x16] = KEY_1, [0x1A] = KEY_Z, [0x1B] = KEY_S,
    [0x1C] = KEY_A, [0x1D] = KEY_W, [0x1E] = KEY_2, [0x21] = KEY_C,
    [0x22] = KEY_X, [0x23] = KEY_D, [0x24] = KEY_E, [0x25] = KEY_4,
    [0x26] = KEY_3, [0x29] = KEY_SPACE, [0x2A] = KEY_V, [0x2B] = KEY_F,
    [0x2C] = KEY_T, [0x2D] = KEY_R, [0x2E] = KEY_5, [0x31] = KEY_N,
    [0x32] = KEY_B, [0x33] = KEY_H, [0x34] = KEY_G, [0x35] = KEY_Y,
    [0x36] = KEY_6, [0x3A] = KEY_M, [0x3B] = KEY_J, [0x3C] = KEY_U,
    [0x3D] = KEY_7, [0x3E] = KEY_8, [0x41] = KEY_COMMA, [0x42] = KEY_K,
    [0x43] = KEY_I, [0x44] = KEY_O, [0x45] = KEY_0, [0x46] = KEY_9,
    [0x49] = KEY_PERIOD, [0x4A] = KEY_SLASH, [0x4B] = KEY_L, [0x4C] = KEY_SEMICOLON,
    [0x4D] = KEY_P, [0x4E] = KEY_MINUS, [0x52] = KEY_APOSTROPHE,
    [0x54] = KEY_LEFT_BRACKET, [0x55] = KEY_EQUALS, [0x58] = KEY_CAPS_LOCK,
    [0x59] = KEY_RIGHT_SHIFT, [0x5A] = KEY_ENTER, [0x5B] = KEY_RIGHT_BRACKET,
    [0x5D] = KEY_BACKSLASH, [0x61] = KEY_OEM_102, [0x66] = KEY_BACKSPACE,
    [0x69] = KEY_KEYPAD_1, [0x6B] = KEY_KEYPAD_4, [0x6C] = KEY_KEYPAD_7,
    [0x70] = KEY_KEYPAD_0, [0x71] = KEY_KEYPAD_PERIOD, [0x72] = KEY_KEYPAD_2,
    [0x73] = KEY_KEYPAD_5, [0x74] = KEY_KEYPAD_6, [0x75] = KEY_KEYPAD_8,
    [0x76] = KEY_ESC, [0x77] = KEY_NUM_LOCK, [0x78] = KEY_F11,
    [0x79] = KEY_KEYPAD_PLUS, [0x7A] = KEY_KEYPAD_3, [0x7B] = KEY_KEYPAD_MINUS,
    [0x7C] = KEY_KEYPAD_MULTIPLY, [0x7D] = KEY_KEYPAD_9, [0x7E] = KEY_SCROLL_LOCK,
    [0x7F] = KEY_UNKNOWN, [0x83] = KEY_F7,
};

// Extended scan codes (0xE0 prefix) for set 2
static const key_code_t extended_scancode_set2_to_keycode[256] = {
    [0x11] = KEY_RIGHT_ALT, [0x14] = KEY_RIGHT_CTRL,
    [0x1F] = KEY_LEFT_GUI, [0x27] = KEY_RIGHT_GUI, [0x2F] = KEY_MENU,
    [0x4A] = KEY_KEYPAD_DIVIDE, [0x5A] = KEY_KEYPAD_ENTER,
    [0x69] = KEY_END, [0x6B] = KEY_LEFT, [0x6C] = KEY_HOME,
    [0x70] = KEY_INSERT, [0x71] = KEY_DELETE, [0x72] = KEY_DOWN,
    [0x74] = KEY_RIGHT, [0x75] = KEY_UP, [0x7A] = KEY_PAGE_DOWN,
    [0x7C] = KEY_PRINT_SCREEN, [0x7D] = KEY_PAGE_UP,
};

static const keyboard_layout_t* active_layout = NULL;
static inline const keyboard_layout_t* ps2_get_active_layout(void) {
    if (!active_layout) {
        active_layout = keyboard_layout_get_default();
    }
    return active_layout;
}

static void ps2_keyboard_process_scancode(uint8 scancode);
static void ps2_keyboard_process_scancode_set1(uint8 scancode);
static void ps2_keyboard_process_scancode_set2(uint8 scancode);
static bool ps2_keyboard_normalize_key_state(key_code_t key_code, key_state_t* state);
static void ps2_keyboard_send_event(key_code_t key_code, key_state_t state, uint8* raw_scancode, uint8 length);
static void ps2_keyboard_update_modifier_state(key_code_t key_code, key_state_t state);
static const keyboard_layout_t* ps2_keyboard_layout_from_id(keyboard_layout_id_t layout);

int ps2_keyboard_init(void) {
    print("[KB] Initializing PS/2 keyboard driver...\n");
    
    // Initialize driver state
    kbd_memset(&kbd_state, 0, sizeof(kbd_state));
    kbd_state.scanning_enabled = false;
    
    // Reset keyboard
    if (!ps2_keyboard_reset()) {
        print("[KB] Failed to reset keyboard\n");
        return -1;
    }
    
    bool translation_enabled = ps2_controller_is_translation_enabled();
    if (translation_enabled) {
        kbd_state.current_scancode_set = KB_SCANCODE_SET_1;  // controller converts to set 1
    } else {
        // Use native scan code set 2 when no translation is applied
        if (!ps2_keyboard_set_scancode_set(KB_SCANCODE_SET_2)) {
            print("[KB] Failed to set scan code set 2\n");
            return -1;
        }
        kbd_state.current_scancode_set = KB_SCANCODE_SET_2;
    }
    
    ps2_keyboard_select_layout(PS2_KEYBOARD_DEFAULT_LAYOUT);
    
    // Set default typematic rate
    if (!ps2_keyboard_set_typematic(0x0B, 0x01)) {
        print("[KB] Failed to set typematic rate\n");
        return -1;
    }
    
    // Enable scanning
    if (!ps2_keyboard_enable_scanning()) {
        print("[KB] Failed to enable scanning\n");
        return -1;
    }
    
    print("[KB] PS/2 keyboard driver initialized successfully\n");
    return 0;
}

void ps2_keyboard_irq_handler(struct interrupt_frame* frame, uint32 error_code) {
    (void)frame;
    (void)error_code;
    
    if (ps2_keyboard_data_available()) {
        uint8 scancode = ps2_controller_read_data();
        ps2_keyboard_process_scancode(scancode);
    }
    
    pic_send_eoi(1);
}

static void ps2_keyboard_process_scancode(uint8 scancode) {
    if (kbd_state.scancode_buffer_pos < sizeof(kbd_state.scancode_buffer)) {
        kbd_state.scancode_buffer[kbd_state.scancode_buffer_pos++] = scancode;
    }
    
    if (!kbd_state.pause_sequence && scancode == 0xE1) {
        kbd_state.pause_sequence = true;
        kbd_state.pause_sequence_length = 1;
        kbd_state.pause_expected_length = (kbd_state.current_scancode_set == KB_SCANCODE_SET_2) ? 8 : 6;
        return;
    }
    
    if (kbd_state.pause_sequence) {
        kbd_state.pause_sequence_length++;
        if (kbd_state.pause_sequence_length >= kbd_state.pause_expected_length) {
            ps2_keyboard_send_event(KEY_PAUSE, KEY_STATE_PRESSED,
                                    kbd_state.scancode_buffer, kbd_state.scancode_buffer_pos);
            kbd_state.scancode_buffer_pos = 0;
            kbd_state.pause_sequence = false;
            kbd_state.pause_sequence_length = 0;
            kbd_state.pause_expected_length = 0;
            kbd_state.extended_scancode = false;
            kbd_state.break_code = false;
        }
        return;
    }
    
    if (kbd_state.current_scancode_set == KB_SCANCODE_SET_1) {
        ps2_keyboard_process_scancode_set1(scancode);
    } else {
        ps2_keyboard_process_scancode_set2(scancode);
    }
}

static void ps2_keyboard_process_scancode_set1(uint8 scancode) {
    if (scancode == 0xE0) {
        kbd_state.extended_scancode = true;
        return;
    }
    
    bool release = (scancode & 0x80U) != 0;
    scancode &= 0x7F;
    
    key_code_t key_code = kbd_state.extended_scancode ?
        extended_scancode_set1_to_keycode[scancode] :
        scancode_set1_to_keycode[scancode];
    
    if (key_code == KEY_UNKNOWN) {
        kbd_state.scancode_buffer_pos = 0;
        kbd_state.extended_scancode = false;
        kbd_state.break_code = false;
        return;
    }
    
    key_state_t state = release ? KEY_STATE_RELEASED : KEY_STATE_PRESSED;
    if (!ps2_keyboard_normalize_key_state(key_code, &state)) {
        kbd_state.scancode_buffer_pos = 0;
        kbd_state.extended_scancode = false;
        kbd_state.break_code = false;
        return;
    }
    
    if (state != KEY_STATE_REPEAT) {
        ps2_keyboard_update_modifier_state(key_code, state);
    }
    ps2_keyboard_send_event(key_code, state, kbd_state.scancode_buffer, kbd_state.scancode_buffer_pos);
    
    kbd_state.scancode_buffer_pos = 0;
    kbd_state.extended_scancode = false;
    kbd_state.break_code = false;
}

static void ps2_keyboard_process_scancode_set2(uint8 scancode) {
    if (scancode == 0xE0) {
        kbd_state.extended_scancode = true;
        return;
    }
    
    if (scancode == 0xF0) {
        kbd_state.break_code = true;
        return;
    }
    
    key_code_t key_code = kbd_state.extended_scancode ?
        extended_scancode_set2_to_keycode[scancode] :
        scancode_set2_to_keycode[scancode];
    
    if (key_code == KEY_UNKNOWN) {
        kbd_state.scancode_buffer_pos = 0;
        kbd_state.extended_scancode = false;
        kbd_state.break_code = false;
        return;
    }
    
    key_state_t state = kbd_state.break_code ? KEY_STATE_RELEASED : KEY_STATE_PRESSED;
    if (!ps2_keyboard_normalize_key_state(key_code, &state)) {
        kbd_state.scancode_buffer_pos = 0;
        kbd_state.extended_scancode = false;
        kbd_state.break_code = false;
        return;
    }
    
    if (state != KEY_STATE_REPEAT) {
        ps2_keyboard_update_modifier_state(key_code, state);
    }
    ps2_keyboard_send_event(key_code, state, kbd_state.scancode_buffer, kbd_state.scancode_buffer_pos);
    
    kbd_state.scancode_buffer_pos = 0;
    kbd_state.extended_scancode = false;
    kbd_state.break_code = false;
}

static bool ps2_keyboard_normalize_key_state(key_code_t key_code, key_state_t* state) {
    if (!state || key_code <= KEY_UNKNOWN || key_code >= KEY_MAX) {
        return false;
    }
    
    switch (*state) {
        case KEY_STATE_PRESSED:
            if (kbd_state.key_down[key_code]) {
                *state = KEY_STATE_REPEAT;
            } else {
                kbd_state.key_down[key_code] = true;
            }
            return true;
        case KEY_STATE_RELEASED:
            if (!kbd_state.key_down[key_code]) {
                return false;
            }
            kbd_state.key_down[key_code] = false;
            return true;
        case KEY_STATE_REPEAT:
        default:
            return true;
    }
}

static void ps2_keyboard_update_modifier_state(key_code_t key_code, key_state_t state) {
    bool pressed = (state == KEY_STATE_PRESSED);
    
    switch (key_code) {
        case KEY_LEFT_SHIFT:
            kbd_state.modifiers_left_shift = pressed;
            break;
        case KEY_RIGHT_SHIFT:
            kbd_state.modifiers_right_shift = pressed;
            break;
        case KEY_LEFT_CTRL:
            kbd_state.modifiers_left_ctrl = pressed;
            break;
        case KEY_RIGHT_CTRL:
            kbd_state.modifiers_right_ctrl = pressed;
            break;
        case KEY_LEFT_ALT:
            kbd_state.modifiers_left_alt = pressed;
            break;
        case KEY_RIGHT_ALT:
            kbd_state.modifiers_right_alt = pressed;
            break;
        case KEY_LEFT_GUI:
            kbd_state.modifiers_left_gui = pressed;
            break;
        case KEY_RIGHT_GUI:
            kbd_state.modifiers_right_gui = pressed;
            break;
        case KEY_CAPS_LOCK:
            if (pressed) {
                kbd_state.leds_caps_lock = !kbd_state.leds_caps_lock;
                ps2_keyboard_set_leds((kbd_state.leds_caps_lock ? KB_LED_CAPS_LOCK : 0) |
                                    (kbd_state.leds_num_lock ? KB_LED_NUM_LOCK : 0) |
                                    (kbd_state.leds_scroll_lock ? KB_LED_SCROLL_LOCK : 0));
            }
            break;
        case KEY_NUM_LOCK:
            if (pressed) {
                kbd_state.leds_num_lock = !kbd_state.leds_num_lock;
                ps2_keyboard_set_leds((kbd_state.leds_caps_lock ? KB_LED_CAPS_LOCK : 0) |
                                    (kbd_state.leds_num_lock ? KB_LED_NUM_LOCK : 0) |
                                    (kbd_state.leds_scroll_lock ? KB_LED_SCROLL_LOCK : 0));
            }
            break;
        case KEY_SCROLL_LOCK:
            if (pressed) {
                kbd_state.leds_scroll_lock = !kbd_state.leds_scroll_lock;
                ps2_keyboard_set_leds((kbd_state.leds_caps_lock ? KB_LED_CAPS_LOCK : 0) |
                                    (kbd_state.leds_num_lock ? KB_LED_NUM_LOCK : 0) |
                                    (kbd_state.leds_scroll_lock ? KB_LED_SCROLL_LOCK : 0));
            }
            break;
        default:
            break;
    }
}

static void ps2_keyboard_send_event(key_code_t key_code, key_state_t state, uint8* raw_scancode, uint8 length) {
    keyboard_event_t event;
    event.key_code = key_code;
    event.state = state;
    event.scancode_length = (length > sizeof(event.scancode_raw)) ? sizeof(event.scancode_raw) : length;
    kbd_memcpy(event.scancode_raw, raw_scancode, event.scancode_length);
    
    // Set modifier states
    event.shift = kbd_state.modifiers_left_shift || kbd_state.modifiers_right_shift;
    event.ctrl = kbd_state.modifiers_left_ctrl || kbd_state.modifiers_right_ctrl;
    event.alt = kbd_state.modifiers_left_alt || kbd_state.modifiers_right_alt;
    event.gui = kbd_state.modifiers_left_gui || kbd_state.modifiers_right_gui;
    event.caps_lock = kbd_state.leds_caps_lock;
    event.num_lock = kbd_state.leds_num_lock;
    event.scroll_lock = kbd_state.leds_scroll_lock;
    
    // Convert to ASCII / escape sequences
    char seq[KEYBOARD_MAX_SEQUENCE_LENGTH];
    const keyboard_layout_t* layout = ps2_get_active_layout();
    uint8 emitted = keyboard_layout_emit_chars(layout,
                                               key_code,
                                               event.shift,
                                               event.caps_lock,
                                               seq,
                                               sizeof(seq));
    event.ascii = (emitted > 0) ? seq[0] : 0;
    if (state == KEY_STATE_PRESSED && emitted > 0) {
        for (uint8 i = 0; i < emitted; ++i) {
            ps2_keyboard_enqueue_ascii(seq[i]);
        }
    }
    
    // Set timestamp (placeholder - would need timer implementation)
    event.timestamp = 0;
    
    if (event_callback) {
        event_callback(&event);
    }
}

static const keyboard_layout_t* ps2_keyboard_layout_from_id(keyboard_layout_id_t layout) {
    return keyboard_layout_get(layout);
}

void ps2_keyboard_select_layout(keyboard_layout_id_t layout) {
    if (layout >= KEYBOARD_LAYOUT_MAX) {
        layout = KEYBOARD_LAYOUT_US;
    }
    const keyboard_layout_t* resolved_layout = ps2_keyboard_layout_from_id(layout);
    if (!resolved_layout) {
        resolved_layout = keyboard_layout_get_default();
        layout = KEYBOARD_LAYOUT_US;
    }
    
    active_layout = resolved_layout;
    kbd_state.active_layout = layout;
}

bool ps2_keyboard_set_leds(uint8 led_state) {
    if (!ps2_send_keyboard_command(KB_CMD_SET_LEDS)) {
        return false;
    }
    
    if (!ps2_send_keyboard_data(led_state)) {
        return false;
    }
    
    return true;
}

bool ps2_keyboard_set_scancode_set(uint8 scancode_set) {
    if (!ps2_send_keyboard_command(KB_CMD_GET_SET_SCANCODE_SET)) {
        return false;
    }
    
    if (!ps2_send_keyboard_data(scancode_set)) {
        return false;
    }
    
    kbd_state.current_scancode_set = scancode_set;
    return true;
}

bool ps2_keyboard_set_typematic(uint8 rate, uint8 delay) {
    uint8 typematic_byte = (delay << 5) | (rate & 0x1F);
    
    if (!ps2_send_keyboard_command(KB_CMD_SET_TYPEMATIC)) {
        return false;
    }
    
    if (!ps2_send_keyboard_data(typematic_byte)) {
        return false;
    }
    
    return true;
}

bool ps2_keyboard_enable_scanning(void) {
    if (!ps2_send_keyboard_command(KB_CMD_ENABLE_SCANNING)) {
        return false;
    }
    
    kbd_state.scanning_enabled = true;
    return true;
}

bool ps2_keyboard_disable_scanning(void) {
    if (!ps2_send_keyboard_command(KB_CMD_DISABLE_SCANNING)) {
        return false;
    }
    
    kbd_state.scanning_enabled = false;
    return true;
}

bool ps2_keyboard_reset(void) {
    if (!ps2_send_keyboard_command(KB_CMD_RESET)) {
        return false;
    }
    
    // Wait for self-test response
    if (!ps2_controller_wait_output_ready()) {
        return false;
    }
    
    uint8 response = ps2_controller_read_data();
    return (response == PS2_RESPONSE_KEYBOARD_SELF_TEST_PASSED);
}

void ps2_keyboard_register_event_callback(keyboard_event_callback_t callback) {
    event_callback = callback;
}

keyboard_driver_state_t* ps2_keyboard_get_state(void) {
    return &kbd_state;
}
