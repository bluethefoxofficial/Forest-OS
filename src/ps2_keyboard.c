#include "include/interrupt.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_controller.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/keyboard_layout.h"
#include "include/debuglog.h"
#include "include/ps2_mouse.h"
#include "include/input_event.h"
#include "include/input_mux.h"
#include "include/hotkey.h"
#include "include/devfs.h"
#include "include/timer.h"
#include "include/vfs.h"
#include "include/string.h"
#include "include/stdlib.h"

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

// Helper function for hex printing
static void print_hex8(uint8 value) {
    char hex_chars[] = "0123456789ABCDEF";
    char hex_str[3];
    hex_str[0] = hex_chars[(value >> 4) & 0xF];
    hex_str[1] = hex_chars[value & 0xF];
    hex_str[2] = '\0';
    print(hex_str);
}

static keyboard_driver_state_t kbd_state;
static keyboard_event_callback_t event_callback = NULL;

#define PS2_ASCII_BUFFER_SIZE 128
static char ascii_buffer[PS2_ASCII_BUFFER_SIZE];
static volatile uint32 ascii_buffer_head = 0;
static volatile uint32 ascii_buffer_tail = 0;

static keyboard_layout_id_t ps2_keyboard_layout_from_config(void) {
    const uint8* data = NULL;
    uint32 size = 0;
    if (!vfs_read_file("/usr/share/sysconf/keyboard.conf", &data, &size) || !data || size == 0) {
        return PS2_KEYBOARD_DEFAULT_LAYOUT;
    }

    keyboard_layout_id_t layout = PS2_KEYBOARD_DEFAULT_LAYOUT;
    const char* cursor = (const char*)data;
    const char* end = cursor + size;

    while (cursor < end) {
        while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')) {
            cursor++;
        }
        if (cursor >= end) {
            break;
        }
        if (*cursor == '#') {
            while (cursor < end && *cursor != '\n') {
                cursor++;
            }
            continue;
        }

        if ((end - cursor) >= 7 && strncmp(cursor, "layout=", 7) == 0) {
            cursor += 7;
            while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
                cursor++;
            }
            if ((end - cursor) >= 2 && strncmp(cursor, "gb", 2) == 0) {
                layout = KEYBOARD_LAYOUT_GB;
            } else if ((end - cursor) >= 2 && strncmp(cursor, "uk", 2) == 0) {
                layout = KEYBOARD_LAYOUT_GB;
            } else if ((end - cursor) >= 2 && strncmp(cursor, "us", 2) == 0) {
                layout = KEYBOARD_LAYOUT_US;
            }
        }

        while (cursor < end && *cursor != '\n') {
            cursor++;
        }
    }

    free((void*)data);
    return layout;
}

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
    // debuglog(DEBUG_INFO, "[KB] ps2 char '%c' (0x%02x)\n", *out_char, (uint8)*out_char);
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

/*
 * Mapping from internal key_code_t to Linux evdev keycodes
 * The internal key_code_t uses different values than Linux
 */
static uint16 keycode_to_linux[256] = {
    /* KEY_UNKNOWN (0) -> 0 (KEY_RESERVED) */
    [0] = 0,

    /* Function keys (KEY_F1 = 0x10) */
    [0x10] = 59,   /* KEY_F1 -> KEY_F1 */
    [0x11] = 60,   /* KEY_F2 -> KEY_F2 */
    [0x12] = 61,   /* KEY_F3 -> KEY_F3 */
    [0x13] = 62,   /* KEY_F4 -> KEY_F4 */
    [0x14] = 63,   /* KEY_F5 -> KEY_F5 */
    [0x15] = 64,   /* KEY_F6 -> KEY_F6 */
    [0x16] = 65,   /* KEY_F7 -> KEY_F7 */
    [0x17] = 66,   /* KEY_F8 -> KEY_F8 */
    [0x18] = 67,   /* KEY_F9 -> KEY_F9 */
    [0x19] = 68,   /* KEY_F10 -> KEY_F10 */
    [0x1A] = 87,   /* KEY_F11 -> KEY_F11 */
    [0x1B] = 88,   /* KEY_F12 -> KEY_F12 */

    /* Number row (KEY_ESC = 0x20) */
    [0x20] = 1,    /* KEY_ESC -> KEY_ESC */
    [0x21] = 2,    /* KEY_1 -> KEY_1 */
    [0x22] = 3,    /* KEY_2 -> KEY_2 */
    [0x23] = 4,    /* KEY_3 -> KEY_3 */
    [0x24] = 5,    /* KEY_4 -> KEY_4 */
    [0x25] = 6,    /* KEY_5 -> KEY_5 */
    [0x26] = 7,    /* KEY_6 -> KEY_6 */
    [0x27] = 8,    /* KEY_7 -> KEY_7 */
    [0x28] = 9,    /* KEY_8 -> KEY_8 */
    [0x29] = 10,   /* KEY_9 -> KEY_9 */
    [0x2A] = 11,   /* KEY_0 -> KEY_0 */
    [0x2B] = 12,   /* KEY_MINUS -> KEY_MINUS */
    [0x2C] = 13,   /* KEY_EQUALS -> KEY_EQUAL */
    [0x2D] = 14,   /* KEY_BACKSPACE -> KEY_BACKSPACE */
    [0x2E] = 15,   /* KEY_TAB -> KEY_TAB */

    /* Letters row 1 (KEY_Q = 0x30) */
    [0x30] = 16,   /* KEY_Q -> KEY_Q */
    [0x31] = 17,   /* KEY_W -> KEY_W */
    [0x32] = 18,   /* KEY_E -> KEY_E */
    [0x33] = 19,   /* KEY_R -> KEY_R */
    [0x34] = 20,   /* KEY_T -> KEY_T */
    [0x35] = 21,   /* KEY_Y -> KEY_Y */
    [0x36] = 22,   /* KEY_U -> KEY_U */
    [0x37] = 23,   /* KEY_I -> KEY_I */
    [0x38] = 24,   /* KEY_O -> KEY_O */
    [0x39] = 25,   /* KEY_P -> KEY_P */
    [0x3A] = 26,   /* KEY_LEFT_BRACKET -> KEY_LEFTBRACE */
    [0x3B] = 27,   /* KEY_RIGHT_BRACKET -> KEY_RIGHTBRACE */
    [0x3C] = 28,   /* KEY_ENTER -> KEY_ENTER */
    [0x3D] = 29,   /* KEY_LEFT_CTRL -> KEY_LEFTCTRL */
    [0x3E] = 30,   /* KEY_A -> KEY_A */
    [0x3F] = 31,   /* KEY_S -> KEY_S */

    /* Letters row 2 (KEY_D = 0x40) */
    [0x40] = 32,   /* KEY_D -> KEY_D */
    [0x41] = 33,   /* KEY_F -> KEY_F */
    [0x42] = 34,   /* KEY_G -> KEY_G */
    [0x43] = 35,   /* KEY_H -> KEY_H */
    [0x44] = 36,   /* KEY_J -> KEY_J */
    [0x45] = 37,   /* KEY_K -> KEY_K */
    [0x46] = 38,   /* KEY_L -> KEY_L */
    [0x47] = 39,   /* KEY_SEMICOLON -> KEY_SEMICOLON */
    [0x48] = 40,   /* KEY_APOSTROPHE -> KEY_APOSTROPHE */
    [0x49] = 41,   /* KEY_GRAVE -> KEY_GRAVE */
    [0x4A] = 42,   /* KEY_LEFT_SHIFT -> KEY_LEFTSHIFT */
    [0x4B] = 43,   /* KEY_BACKSLASH -> KEY_BACKSLASH */
    [0x4C] = 86,   /* KEY_OEM_102 -> KEY_102ND */
    [0x4D] = 44,   /* KEY_Z -> KEY_Z */
    [0x4E] = 45,   /* KEY_X -> KEY_X */
    [0x4F] = 46,   /* KEY_C -> KEY_C */

    /* Letters row 3 / keypad area (KEY_V is actually 0x4F+1=0x50 area) */
    /* Actually KEY_V is in the original enum differently - let me check */
    /* From ps2_keyboard.h: KEY_V is after KEY_C (0x4F), so it's part of 0x4F-0x50 */
    /* The enum has KEY_V = 0x50-something but actually the raw values suggest: */
    /* KEY_B = 0x50, KEY_N, KEY_M, KEY_COMMA, KEY_PERIOD, KEY_SLASH */
    [0x50] = 48,   /* KEY_B -> KEY_B */
    [0x51] = 49,   /* KEY_N -> KEY_N */
    [0x52] = 50,   /* KEY_M -> KEY_M */
    [0x53] = 51,   /* KEY_COMMA -> KEY_COMMA */
    [0x54] = 52,   /* KEY_PERIOD -> KEY_DOT */
    [0x55] = 53,   /* KEY_SLASH -> KEY_SLASH */
    [0x56] = 54,   /* KEY_RIGHT_SHIFT -> KEY_RIGHTSHIFT */
    [0x57] = 55,   /* KEY_KEYPAD_MULTIPLY -> KEY_KPASTERISK */
    [0x58] = 56,   /* KEY_LEFT_ALT -> KEY_LEFTALT */
    [0x59] = 57,   /* KEY_SPACE -> KEY_SPACE */
    [0x5A] = 58,   /* KEY_CAPS_LOCK -> KEY_CAPSLOCK */

    /* Keypad (KEY_KEYPAD_7 = 0x60) */
    [0x60] = 71,   /* KEY_KEYPAD_7 -> KEY_KP7 */
    [0x61] = 72,   /* KEY_KEYPAD_8 -> KEY_KP8 */
    [0x62] = 73,   /* KEY_KEYPAD_9 -> KEY_KP9 */
    [0x63] = 74,   /* KEY_KEYPAD_MINUS -> KEY_KPMINUS */
    [0x64] = 75,   /* KEY_KEYPAD_4 -> KEY_KP4 */
    [0x65] = 76,   /* KEY_KEYPAD_5 -> KEY_KP5 */
    [0x66] = 77,   /* KEY_KEYPAD_6 -> KEY_KP6 */
    [0x67] = 78,   /* KEY_KEYPAD_PLUS -> KEY_KPPLUS */
    [0x68] = 79,   /* KEY_KEYPAD_1 -> KEY_KP1 */
    [0x69] = 80,   /* KEY_KEYPAD_2 -> KEY_KP2 */
    [0x6A] = 81,   /* KEY_KEYPAD_3 -> KEY_KP3 */
    [0x6B] = 82,   /* KEY_KEYPAD_0 -> KEY_KP0 */
    [0x6C] = 83,   /* KEY_KEYPAD_PERIOD -> KEY_KPDOT */
    [0x6D] = 96,   /* KEY_KEYPAD_ENTER -> KEY_KPENTER */
    [0x6E] = 98,   /* KEY_KEYPAD_DIVIDE -> KEY_KPSLASH */

    /* Navigation (KEY_HOME = 0x70) */
    [0x70] = 102,  /* KEY_HOME -> KEY_HOME */
    [0x71] = 103,  /* KEY_UP -> KEY_UP */
    [0x72] = 104,  /* KEY_PAGE_UP -> KEY_PAGEUP */
    [0x73] = 105,  /* KEY_LEFT -> KEY_LEFT */
    [0x74] = 106,  /* KEY_RIGHT -> KEY_RIGHT */
    [0x75] = 107,  /* KEY_END -> KEY_END */
    [0x76] = 108,  /* KEY_DOWN -> KEY_DOWN */
    [0x77] = 109,  /* KEY_PAGE_DOWN -> KEY_PAGEDOWN */
    [0x78] = 110,  /* KEY_INSERT -> KEY_INSERT */
    [0x79] = 111,  /* KEY_DELETE -> KEY_DELETE */

    /* Modifiers (KEY_RIGHT_CTRL = 0x80) */
    [0x80] = 97,   /* KEY_RIGHT_CTRL -> KEY_RIGHTCTRL */
    [0x81] = 100,  /* KEY_RIGHT_ALT -> KEY_RIGHTALT */
    [0x82] = 125,  /* KEY_LEFT_GUI -> KEY_LEFTMETA */
    [0x83] = 126,  /* KEY_RIGHT_GUI -> KEY_RIGHTMETA */
    [0x84] = 127,  /* KEY_MENU -> KEY_COMPOSE */
    [0x85] = 69,   /* KEY_NUM_LOCK -> KEY_NUMLOCK */
    [0x86] = 70,   /* KEY_SCROLL_LOCK -> KEY_SCROLLLOCK */

    /* Multimedia (KEY_VOLUME_DOWN = 0x90) */
    [0x90] = 114,  /* KEY_VOLUME_DOWN -> KEY_VOLUMEDOWN */
    [0x91] = 115,  /* KEY_VOLUME_UP -> KEY_VOLUMEUP */
    [0x92] = 113,  /* KEY_MUTE -> KEY_MUTE */
    [0x93] = 116,  /* KEY_POWER -> KEY_POWER */

    /* Special (KEY_PRINT_SCREEN = 0xA0) */
    [0xA0] = 99,   /* KEY_PRINT_SCREEN -> KEY_SYSRQ */
    [0xA1] = 119,  /* KEY_PAUSE -> KEY_PAUSE */
};

/*
 * Convert internal key_code_t to Linux evdev keycode
 */
static inline uint16 ps2_keycode_to_linux(key_code_t keycode) {
    if (keycode < 256) {
        return keycode_to_linux[keycode];
    }
    return 0;
}

static uint8 ps2_keycode_to_hotkey_scancode(key_code_t key_code) {
    switch (key_code) {
        case KEY_F1:  return 0x3B;
        case KEY_F2:  return 0x3C;
        case KEY_F3:  return 0x3D;
        case KEY_F4:  return 0x3E;
        case KEY_F5:  return 0x3F;
        case KEY_F6:  return 0x40;
        case KEY_F7:  return 0x41;
        case KEY_F8:  return 0x42;
        case KEY_F9:  return 0x43;
        case KEY_F10: return 0x44;
        case KEY_F11: return 0x57;
        case KEY_F12: return 0x58;
        default:
            return 0;
    }
}

static const keyboard_layout_t* active_layout = NULL;
static inline const keyboard_layout_t* ps2_get_active_layout(void) {
    if (!active_layout) {
        active_layout = keyboard_layout_get_default();
    }
    return active_layout;
}

void ps2_keyboard_process_scancode(uint8 scancode);
static void ps2_keyboard_process_scancode_set1(uint8 scancode);
static void ps2_keyboard_process_scancode_set2(uint8 scancode);
static bool ps2_keyboard_normalize_key_state(key_code_t key_code, key_state_t* state);
static void ps2_keyboard_send_event(key_code_t key_code, key_state_t state, uint8* raw_scancode, uint8 length);
static void ps2_keyboard_update_modifier_state(key_code_t key_code, key_state_t state);
static const keyboard_layout_t* ps2_keyboard_layout_from_id(keyboard_layout_id_t layout);


int ps2_keyboard_init(void) {
    print("KB_INIT_START\n");
    print("[KB] Initializing PS/2 keyboard driver...\n");

    // Initialize driver state
    kbd_memset(&kbd_state, 0, sizeof(kbd_state));
    kbd_state.scanning_enabled = false;

    // Step 9: Reset keyboard device (as per PS/2 initialization sequence)
    print("[KB] Resetting keyboard device...\n");
    bool reset_ok = ps2_keyboard_reset();
    if (!reset_ok) {
        print("[KB] Warning: Keyboard reset failed/timeout - this may indicate no keyboard connected\n");
        print("[KB] Continuing with initialization anyway...\n");
    } else {
        print("[KB] Keyboard reset successful\n");
    }

    // Check if PS/2 controller translation is enabled
    // When translation is enabled, Set 2 scancodes are converted to Set 1 by hardware
    bool translation_enabled = ps2_controller_is_translation_enabled();

    if (translation_enabled) {
        // Translation is ON: hardware converts Set 2 to Set 1, so we MUST use Set 1 processing
        kbd_state.current_scancode_set = KB_SCANCODE_SET_1;
        print("[KB] PS/2 controller translation enabled - using scan code set 1\n");
    } else {
        // Translation is OFF: use Set 2 for better compatibility
        kbd_state.current_scancode_set = KB_SCANCODE_SET_2;

        // Try to set scan code set 2
        if (ps2_keyboard_set_scancode_set(KB_SCANCODE_SET_2)) {
            print("[KB] Successfully set scan code set 2\n");
        } else {
            print("[KB] Warning: Failed to set scan code set 2, falling back to set 1\n");
            kbd_state.current_scancode_set = KB_SCANCODE_SET_1;
            if (!ps2_keyboard_set_scancode_set(KB_SCANCODE_SET_1)) {
                print("[KB] Warning: Failed to set scan code set 1 either\n");
            }
        }

        // Verify which scan code set we're actually using
        uint8 detected_set = ps2_keyboard_get_scancode_set();
        if (detected_set == 0x41 || detected_set == 0x02) {
            kbd_state.current_scancode_set = KB_SCANCODE_SET_2;
            print("[KB] Verified: Using scan code set 2\n");
        } else if (detected_set == 0x43 || detected_set == 0x01) {
            kbd_state.current_scancode_set = KB_SCANCODE_SET_1;
            print("[KB] Verified: Using scan code set 1\n");
        } else {
            print("[KB] Warning: Could not verify scan code set (got 0x");
            print_hex8(detected_set);
            print(")\n");
        }
    }

    ps2_keyboard_select_layout(ps2_keyboard_layout_from_config());

    // Set reasonable typematic rate (delay=500ms, rate=10.9 chars/sec)
    if (!ps2_keyboard_set_typematic(0x2B, 0x01)) {
        print("[KB] Warning: Failed to set typematic rate (non-critical)\n");
    }

    // Set default LED state (all off)
    if (!ps2_keyboard_set_leds(0)) {
        print("[KB] Warning: Failed to set LED state (non-critical)\n");
    }

    // Step 10: Enable scanning - this is critical for keyboard input
    print("[KB] Enabling keyboard scanning...\n");
    bool scanning_enabled = false;
    for (int retry = 0; retry < 5; retry++) {  // Try more times
        if (ps2_keyboard_enable_scanning()) {
            scanning_enabled = true;
            print("[KB] Keyboard scanning enabled successfully\n");
            break;
        } else {
            print("[KB] Attempt ");
            print_hex8(retry + 1);
            print(" to enable scanning failed, retrying...\n");
            // Small delay between retries
            for (volatile int i = 0; i < 100000; i++);
        }
        // Brief delay between retries
        for (volatile int i = 0; i < 10000; i++);
    }

    if (!scanning_enabled) {
        print("[KB] Warning: Could not enable scanning via command\n");
        // Even if we couldn't send the enable command, the keyboard might
        // still be sending scancodes (especially in QEMU/emulators)
        kbd_state.scanning_enabled = true;  // Assume it's enabled
    }

    print("[KB] PS/2 keyboard driver initialized\n");
    return 0;  // Always return success - we'll handle input either way
}

void ps2_keyboard_irq_handler(struct interrupt_frame* frame, uint32 error_code) {
    (void)frame;
    (void)error_code;

    /*
     * Demultiplex the shared PS/2 output buffer here so keyboard and mouse
     * progress even if one IRQ arrives before the other.
     */
    for (int i = 0; i < 32; i++) {
        uint8 status = inportb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT_BUFFER_FULL)) {
            break;
        }

        uint8 data = ps2_controller_read_data();
        if (status & PS2_STATUS_AUX_OUTPUT_BUFFER) {
            ps2_mouse_handle_byte(data);
        } else {
            ps2_keyboard_process_scancode(data);
        }
    }

    pic_send_eoi(1);
}

void ps2_keyboard_process_scancode(uint8 scancode) {
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

static inline uint8 ps2_keyboard_current_led_mask(void) {
    return (kbd_state.leds_caps_lock ? KB_LED_CAPS_LOCK : 0) |
           (kbd_state.leds_num_lock ? KB_LED_NUM_LOCK : 0) |
           (kbd_state.leds_scroll_lock ? KB_LED_SCROLL_LOCK : 0);
}

static void ps2_keyboard_sync_leds_if_safe(void) {
    /*
     * Avoid synchronous PS/2 command transactions from IRQ key events once
     * mouse streaming is active, which can interleave with AUX traffic.
     */
    if (ps2_mouse_is_ready()) {
        return;
    }
    (void)ps2_keyboard_set_leds(ps2_keyboard_current_led_mask());
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
                ps2_keyboard_sync_leds_if_safe();
            }
            break;
        case KEY_NUM_LOCK:
            if (pressed) {
                kbd_state.leds_num_lock = !kbd_state.leds_num_lock;
                ps2_keyboard_sync_leds_if_safe();
            }
            break;
        case KEY_SCROLL_LOCK:
            if (pressed) {
                kbd_state.leds_scroll_lock = !kbd_state.leds_scroll_lock;
                ps2_keyboard_sync_leds_if_safe();
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

    if (state == KEY_STATE_PRESSED || state == KEY_STATE_RELEASED) {
        uint16 hotkey_mods = 0;
        if (event.ctrl) hotkey_mods |= HOTKEY_MOD_CTRL;
        if (event.alt) hotkey_mods |= HOTKEY_MOD_ALT;
        if (event.shift) hotkey_mods |= HOTKEY_MOD_SHIFT;
        if (event.gui) hotkey_mods |= HOTKEY_MOD_SUPER;

        uint8 hotkey_scancode = ps2_keycode_to_hotkey_scancode(key_code);
        if (hotkey_scancode) {
            hotkey_process_key_event(hotkey_scancode, state == KEY_STATE_PRESSED, hotkey_mods);
        }
    }

    // Generate ASCII characters for backward compatibility with ps2_keyboard_poll_ascii()
    // Treat repeats like presses so typematic events keep emitting characters
    char seq[KEYBOARD_MAX_SEQUENCE_LENGTH];
    const keyboard_layout_t* layout = ps2_get_active_layout();
    uint8 emitted = keyboard_layout_emit_chars(layout,
                                               key_code,
                                               event.shift,
                                               event.caps_lock,
                                               seq,
                                               sizeof(seq));
    event.ascii = (emitted > 0) ? seq[0] : 0;

    // Enqueue ASCII characters to the legacy buffer for ps2_keyboard_poll_ascii()
    if ((state == KEY_STATE_PRESSED || state == KEY_STATE_REPEAT) && emitted > 0) {
        for (uint8 i = 0; i < emitted; ++i) {
            ps2_keyboard_enqueue_ascii(seq[i]);
        }
    }

    // Set timestamp
    event.timestamp = timer_get_ticks();

    // Call legacy event callback
    if (event_callback) {
        event_callback(&event);
    }

    /*
     * Generate input_event_t for the new input subsystem
     * This dispatches to devfs and input multiplexer
     */
    input_event_t input_ev;

    // Get timestamp
    uint32 ticks = timer_get_ticks();
    input_ev.tv_sec = ticks / 1000;   // Assuming ticks are in ms
    input_ev.tv_usec = (ticks % 1000) * 1000;

    // Set event type and code
    input_ev.type = EV_KEY;
    input_ev.code = ps2_keycode_to_linux(key_code);

    // Set value based on key state
    switch (state) {
        case KEY_STATE_PRESSED:
            input_ev.value = KEY_PRESS;   // 1 = pressed
            break;
        case KEY_STATE_RELEASED:
            input_ev.value = KEY_RELEASE; // 0 = released
            break;
        case KEY_STATE_REPEAT:
            input_ev.value = KEY_REPEAT;  // 2 = repeat
            break;
        default:
            input_ev.value = 0;
            break;
    }

    // Queue to device file (for /dev/kbd readers)
    if (devfs_is_initialized()) {
        devfs_kbd_queue_event(&input_ev);
    }

    // Dispatch to input multiplexer (for hotkeys, canopy, etc.)
    if (input_mux_is_initialized()) {
        input_mux_dispatch_event(&input_ev);
    }

    // Generate SYN_REPORT event to mark end of this key event
    input_ev.type = EV_SYN;
    input_ev.code = SYN_REPORT;
    input_ev.value = 0;

    if (devfs_is_initialized()) {
        devfs_kbd_queue_event(&input_ev);
    }

    if (input_mux_is_initialized()) {
        input_mux_dispatch_event(&input_ev);
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

uint8 ps2_keyboard_get_scancode_set(void) {
    if (!ps2_send_keyboard_command(KB_CMD_GET_SET_SCANCODE_SET)) {
        return 0;
    }

    if (!ps2_send_keyboard_data(0)) {  // 0 = get current set
        return 0;
    }

    // Wait for response
    if (!ps2_controller_wait_output_ready()) {
        return 0;
    }

    uint8 response = ps2_controller_read_data();
    return response;
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
    print("[KB] Attempting keyboard reset...\n");
    
    // First try to send reset command
    if (!ps2_send_keyboard_command(KB_CMD_RESET)) {
        print("[KB] Failed to send reset command\n");
        return false;
    }
    
    // Wait longer for self-test response (keyboards can be slow)
    uint32 timeout_count = 0;
    const uint32 max_timeout = 5000000;  // Longer timeout for reset
    
    while (timeout_count < max_timeout) {
        if (ps2_keyboard_data_available()) {
            uint8 response = ps2_controller_read_data();
            print("[KB] Reset response: 0x");
            print_hex8(response);
            print("\n");
            
            if (response == PS2_RESPONSE_KEYBOARD_SELF_TEST_PASSED) {
                print("[KB] Keyboard reset successful\n");
                return true;
            } else if (response == PS2_RESPONSE_ERROR1 || response == PS2_RESPONSE_ERROR2) {
                print("[KB] Keyboard self-test failed\n");
                return false;
            }
            // Otherwise, might get ACK first, continue waiting
        }
        timeout_count++;
    }
    
    print("[KB] Keyboard reset timeout\n");
    return false;
}

void ps2_keyboard_register_event_callback(keyboard_event_callback_t callback) {
    event_callback = callback;
}

keyboard_driver_state_t* ps2_keyboard_get_state(void) {
    return &kbd_state;
}

// Debug function to check controller status and poll for data
void ps2_keyboard_debug_status(void) {
    uint8 status = inportb(PS2_STATUS_PORT);
    // debuglog(DEBUG_INFO, "[KB] Controller status: 0x%02x (output_full=%d, input_full=%d, aux_data=%d)\n",
    //          status,
    //          (status & 0x01) ? 1 : 0,
    //          (status & 0x02) ? 1 : 0,
    //          (status & 0x20) ? 1 : 0);

    // If output buffer is full, read what data is available
    if (status & 0x01) {
        uint8 data = ps2_controller_read_data();
        uint8 is_aux = (status & 0x20) ? 1 : 0;
        // debuglog(DEBUG_INFO, "[KB] Available %s data: 0x%02x\n", is_aux ? "AUX (mouse)" : "keyboard", data);

        // Process keyboard data if it's keyboard data
        if (!is_aux) {
            ps2_keyboard_process_scancode(data);
        }
    }
}

/**
 * Poll for keyboard data (for non-interrupt driven operation)
 * Similar to ps2_mouse_poll() - processes all pending keyboard data
 */
void ps2_keyboard_poll(void) {
    int bytes_processed = 0;
    const int max_bytes = 32;  // Prevent infinite loops

    while (bytes_processed < max_bytes) {
        uint8 status = inportb(PS2_STATUS_PORT);

        // Check if output buffer has data
        if (!(status & 0x01)) {
            break;  // No data available
        }

        // Check if this is mouse (AUX) data - DON'T consume it here!
        // Leave it in the buffer for ps2_mouse_poll() to handle
        if (status & 0x20) {
            // Mouse data present - stop keyboard polling and let mouse driver handle it
            break;
        }

        // Read and process keyboard data
        uint8 data = ps2_controller_read_data();
        ps2_keyboard_process_scancode(data);
        bytes_processed++;
    }
}

// Static flag to track if keyboard is present
static bool g_keyboard_present = false;
static uint32_t g_last_keyboard_activity = 0;

/**
 * Check if keyboard device is present and responding
 * Uses echo command to test communication
 */
bool ps2_keyboard_is_present(void) {
    // Try to send echo command
    if (!ps2_send_keyboard_command(KB_CMD_ECHO)) {
        return false;
    }
    
    // Wait for echo response with timeout
    uint32 timeout = 100000;
    while (timeout > 0) {
        if (ps2_keyboard_data_available()) {
            uint8 response = ps2_controller_read_data();
            if (response == KB_CMD_ECHO) {
                g_keyboard_present = true;
                return true;
            }
        }
        timeout--;
    }
    
    g_keyboard_present = false;
    return false;
}

/**
 * Reinitialize keyboard after disconnect/reconnect
 * Returns 0 on success, negative on failure
 */
int ps2_keyboard_reinit(void) {
    print("[KB] Reinitializing keyboard...\n");
    
    // Check if keyboard is present
    if (!ps2_keyboard_is_present()) {
        print("[KB] No keyboard detected\n");
        g_keyboard_present = false;
        return -1;
    }
    
    // Perform full initialization
    int result = ps2_keyboard_init();
    if (result == 0) {
        g_keyboard_present = true;
        print("[KB] Keyboard reinitialized successfully\n");
    } else {
        g_keyboard_present = false;
        print("[KB] Keyboard reinitialization failed\n");
    }
    
    return result;
}
