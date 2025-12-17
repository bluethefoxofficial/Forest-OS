#include <stddef.h>

#include "include/keyboard_layout.h"

#ifndef KEYBOARD_DEFAULT_LAYOUT
#ifdef PS2_KEYBOARD_DEFAULT_LAYOUT
#define KEYBOARD_DEFAULT_LAYOUT PS2_KEYBOARD_DEFAULT_LAYOUT
#else
#define KEYBOARD_DEFAULT_LAYOUT KEYBOARD_LAYOUT_US
#endif
#endif

static const keyboard_layout_t keyboard_layout_us = {
    .normal = {
        [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
        [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8',
        [KEY_9] = '9', [KEY_0] = '0', [KEY_MINUS] = '-', [KEY_EQUALS] = '=',
        [KEY_TAB] = '\t', [KEY_SPACE] = ' ', [KEY_ENTER] = '\n',
        [KEY_BACKSPACE] = '\b',
        [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r',
        [KEY_T] = 't', [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i',
        [KEY_O] = 'o', [KEY_P] = 'p', [KEY_LEFT_BRACKET] = '[',
        [KEY_RIGHT_BRACKET] = ']',
        [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f',
        [KEY_G] = 'g', [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k',
        [KEY_L] = 'l', [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'',
        [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '\\',
        [KEY_Z] = 'z', [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v',
        [KEY_B] = 'b', [KEY_N] = 'n', [KEY_M] = 'm', [KEY_COMMA] = ',',
        [KEY_PERIOD] = '.', [KEY_SLASH] = '/',
        [KEY_KEYPAD_0] = '0', [KEY_KEYPAD_1] = '1', [KEY_KEYPAD_2] = '2',
        [KEY_KEYPAD_3] = '3', [KEY_KEYPAD_4] = '4', [KEY_KEYPAD_5] = '5',
        [KEY_KEYPAD_6] = '6', [KEY_KEYPAD_7] = '7', [KEY_KEYPAD_8] = '8',
        [KEY_KEYPAD_9] = '9', [KEY_KEYPAD_PERIOD] = '.', [KEY_KEYPAD_PLUS] = '+',
        [KEY_KEYPAD_MINUS] = '-', [KEY_KEYPAD_MULTIPLY] = '*',
        [KEY_KEYPAD_DIVIDE] = '/', [KEY_KEYPAD_ENTER] = '\n',
    },
    .shifted = {
        [KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$',
        [KEY_5] = '%', [KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*',
        [KEY_9] = '(', [KEY_0] = ')', [KEY_MINUS] = '_', [KEY_EQUALS] = '+',
        [KEY_TAB] = '\t', [KEY_SPACE] = ' ', [KEY_ENTER] = '\n',
        [KEY_BACKSPACE] = '\b',
        [KEY_Q] = 'Q', [KEY_W] = 'W', [KEY_E] = 'E', [KEY_R] = 'R',
        [KEY_T] = 'T', [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I',
        [KEY_O] = 'O', [KEY_P] = 'P', [KEY_LEFT_BRACKET] = '{',
        [KEY_RIGHT_BRACKET] = '}',
        [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F',
        [KEY_G] = 'G', [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K',
        [KEY_L] = 'L', [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"',
        [KEY_GRAVE] = '~', [KEY_BACKSLASH] = '|',
        [KEY_Z] = 'Z', [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V',
        [KEY_B] = 'B', [KEY_N] = 'N', [KEY_M] = 'M', [KEY_COMMA] = '<',
        [KEY_PERIOD] = '>', [KEY_SLASH] = '?',
        [KEY_KEYPAD_0] = '0', [KEY_KEYPAD_1] = '1', [KEY_KEYPAD_2] = '2',
        [KEY_KEYPAD_3] = '3', [KEY_KEYPAD_4] = '4', [KEY_KEYPAD_5] = '5',
        [KEY_KEYPAD_6] = '6', [KEY_KEYPAD_7] = '7', [KEY_KEYPAD_8] = '8',
        [KEY_KEYPAD_9] = '9', [KEY_KEYPAD_PERIOD] = '.', [KEY_KEYPAD_PLUS] = '+',
        [KEY_KEYPAD_MINUS] = '-', [KEY_KEYPAD_MULTIPLY] = '*',
        [KEY_KEYPAD_DIVIDE] = '/', [KEY_KEYPAD_ENTER] = '\n',
    },
};

static const keyboard_layout_t keyboard_layout_gb = {
    .normal = {
        [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
        [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8',
        [KEY_9] = '9', [KEY_0] = '0', [KEY_MINUS] = '-', [KEY_EQUALS] = '=',
        [KEY_TAB] = '\t', [KEY_SPACE] = ' ', [KEY_ENTER] = '\n',
        [KEY_BACKSPACE] = '\b',
        [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r',
        [KEY_T] = 't', [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i',
        [KEY_O] = 'o', [KEY_P] = 'p', [KEY_LEFT_BRACKET] = '[',
        [KEY_RIGHT_BRACKET] = ']',
        [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f',
        [KEY_G] = 'g', [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k',
        [KEY_L] = 'l', [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'',
        [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '#', [KEY_OEM_102] = '\\',
        [KEY_Z] = 'z', [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v',
        [KEY_B] = 'b', [KEY_N] = 'n', [KEY_M] = 'm', [KEY_COMMA] = ',',
        [KEY_PERIOD] = '.', [KEY_SLASH] = '/',
        [KEY_KEYPAD_0] = '0', [KEY_KEYPAD_1] = '1', [KEY_KEYPAD_2] = '2',
        [KEY_KEYPAD_3] = '3', [KEY_KEYPAD_4] = '4', [KEY_KEYPAD_5] = '5',
        [KEY_KEYPAD_6] = '6', [KEY_KEYPAD_7] = '7', [KEY_KEYPAD_8] = '8',
        [KEY_KEYPAD_9] = '9', [KEY_KEYPAD_PERIOD] = '.', [KEY_KEYPAD_PLUS] = '+',
        [KEY_KEYPAD_MINUS] = '-', [KEY_KEYPAD_MULTIPLY] = '*',
        [KEY_KEYPAD_DIVIDE] = '/', [KEY_KEYPAD_ENTER] = '\n',
    },
    .shifted = {
        [KEY_1] = '!', [KEY_2] = '\"', [KEY_3] = '\xA3', [KEY_4] = '$',
        [KEY_5] = '%', [KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*',
        [KEY_9] = '(', [KEY_0] = ')', [KEY_MINUS] = '_', [KEY_EQUALS] = '+',
        [KEY_TAB] = '\t', [KEY_SPACE] = ' ', [KEY_ENTER] = '\n',
        [KEY_BACKSPACE] = '\b',
        [KEY_Q] = 'Q', [KEY_W] = 'W', [KEY_E] = 'E', [KEY_R] = 'R',
        [KEY_T] = 'T', [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I',
        [KEY_O] = 'O', [KEY_P] = 'P', [KEY_LEFT_BRACKET] = '{',
        [KEY_RIGHT_BRACKET] = '}',
        [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F',
        [KEY_G] = 'G', [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K',
        [KEY_L] = 'L', [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '@',
        [KEY_GRAVE] = '\xAC', [KEY_BACKSLASH] = '~', [KEY_OEM_102] = '|',
        [KEY_Z] = 'Z', [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V',
        [KEY_B] = 'B', [KEY_N] = 'N', [KEY_M] = 'M', [KEY_COMMA] = '<',
        [KEY_PERIOD] = '>', [KEY_SLASH] = '?',
        [KEY_KEYPAD_0] = '0', [KEY_KEYPAD_1] = '1', [KEY_KEYPAD_2] = '2',
        [KEY_KEYPAD_3] = '3', [KEY_KEYPAD_4] = '4', [KEY_KEYPAD_5] = '5',
        [KEY_KEYPAD_6] = '6', [KEY_KEYPAD_7] = '7', [KEY_KEYPAD_8] = '8',
        [KEY_KEYPAD_9] = '9', [KEY_KEYPAD_PERIOD] = '.', [KEY_KEYPAD_PLUS] = '+',
        [KEY_KEYPAD_MINUS] = '-', [KEY_KEYPAD_MULTIPLY] = '*',
        [KEY_KEYPAD_DIVIDE] = '/', [KEY_KEYPAD_ENTER] = '\n',
    },
};

const keyboard_layout_t* keyboard_layout_get(keyboard_layout_id_t layout) {
    switch (layout) {
        case KEYBOARD_LAYOUT_GB:
            return &keyboard_layout_gb;
        case KEYBOARD_LAYOUT_US:
        default:
            return &keyboard_layout_us;
    }
}

const keyboard_layout_t* keyboard_layout_get_default(void) {
    return keyboard_layout_get(KEYBOARD_DEFAULT_LAYOUT);
}

char keyboard_layout_lookup_char(const keyboard_layout_t* layout,
                                 key_code_t key_code,
                                 bool shift,
                                 bool caps_lock) {
    if (!layout || key_code >= KEY_MAX) {
        return 0;
    }

    bool use_shift = shift;
    if (key_code >= KEY_A && key_code <= KEY_Z) {
        use_shift = shift ^ caps_lock;
    }

    const char* table = use_shift ? layout->shifted : layout->normal;
    return table[key_code];
}

uint8 keyboard_layout_emit_chars(const keyboard_layout_t* layout,
                                 key_code_t key_code,
                                 bool shift,
                                 bool caps_lock,
                                 char* out_buffer,
                                 uint8 buffer_size) {
    if (!out_buffer || buffer_size == 0) {
        return 0;
    }

    const struct {
        key_code_t key;
        char seq[3];
    } arrow_map[] = {
        { KEY_UP, { '\x1b', '[', 'A' } },
        { KEY_DOWN, { '\x1b', '[', 'B' } },
        { KEY_RIGHT, { '\x1b', '[', 'C' } },
        { KEY_LEFT, { '\x1b', '[', 'D' } },
    };

    for (size_t i = 0; i < sizeof(arrow_map) / sizeof(arrow_map[0]); ++i) {
        if (key_code == arrow_map[i].key) {
            uint8 emit = (buffer_size < 3) ? buffer_size : 3;
            for (uint8 j = 0; j < emit; ++j) {
                out_buffer[j] = arrow_map[i].seq[j];
            }
            return emit;
        }
    }

    char ch = keyboard_layout_lookup_char(layout ? layout : keyboard_layout_get_default(),
                                          key_code,
                                          shift,
                                          caps_lock);
    if (ch == 0) {
        return 0;
    }

    out_buffer[0] = ch;
    return 1;
}

key_code_t keyboard_scancode_set1_lookup(uint8 scancode, bool extended) {
    static const key_code_t scancode_set1_to_keycode[128] = {
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
        [0x43] = KEY_F9, [0x44] = KEY_F10,
        [0x45] = KEY_NUM_LOCK, [0x46] = KEY_SCROLL_LOCK,
        [0x47] = KEY_KEYPAD_7, [0x48] = KEY_KEYPAD_8, [0x49] = KEY_KEYPAD_9,
        [0x4A] = KEY_KEYPAD_MINUS, [0x4B] = KEY_KEYPAD_4, [0x4C] = KEY_KEYPAD_5,
        [0x4D] = KEY_KEYPAD_6, [0x4E] = KEY_KEYPAD_PLUS, [0x4F] = KEY_KEYPAD_1,
        [0x50] = KEY_KEYPAD_2, [0x51] = KEY_KEYPAD_3, [0x52] = KEY_KEYPAD_0,
        [0x53] = KEY_KEYPAD_PERIOD,
        [0x56] = KEY_OEM_102,
        [0x57] = KEY_F11, [0x58] = KEY_F12,
    };

    static const key_code_t extended_scancode_set1_to_keycode[128] = {
        [0x1C] = KEY_KEYPAD_ENTER, [0x1D] = KEY_RIGHT_CTRL,
        [0x35] = KEY_KEYPAD_DIVIDE, [0x38] = KEY_RIGHT_ALT,
        [0x47] = KEY_HOME, [0x48] = KEY_UP, [0x49] = KEY_PAGE_UP,
        [0x4B] = KEY_LEFT, [0x4D] = KEY_RIGHT, [0x4F] = KEY_END,
        [0x50] = KEY_DOWN, [0x51] = KEY_PAGE_DOWN, [0x52] = KEY_INSERT,
        [0x53] = KEY_DELETE, [0x5B] = KEY_LEFT_GUI, [0x5C] = KEY_RIGHT_GUI,
        [0x5D] = KEY_MENU,
        [0x10] = KEY_WWW_SEARCH, [0x19] = KEY_MUTE, [0x20] = KEY_VOLUME_DOWN,
        [0x22] = KEY_CALCULATOR, [0x24] = KEY_WWW_STOP, [0x2E] = KEY_VOLUME_UP,
        [0x30] = KEY_WWW_FORWARD, [0x32] = KEY_WWW_BACK,
        [0x3A] = KEY_MY_COMPUTER, [0x48] = KEY_EMAIL, [0x50] = KEY_MEDIA_SELECT,
        [0x5E] = KEY_POWER, [0x5F] = KEY_SLEEP, [0x63] = KEY_WAKE,
        [0x65] = KEY_WWW_SEARCH, [0x66] = KEY_WWW_FAVORITES, [0x67] = KEY_WWW_REFRESH,
    };

    const key_code_t* table = extended ? extended_scancode_set1_to_keycode : scancode_set1_to_keycode;
    if (scancode >= 128) {
        return KEY_UNKNOWN;
    }
    key_code_t key = table[scancode];
    return key ? key : KEY_UNKNOWN;
}
