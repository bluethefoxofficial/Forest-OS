#include "include/kb.h"
#include "include/ps2_keyboard.h"
#include "include/keyboard_layout.h"

#ifndef USERSPACE_BUILD
#include "include/panic.h"
#endif


#define KB_BUFFER_MAX 200
static keyboard_driver_mode_t current_driver_mode = KEYBOARD_DRIVER_LEGACY;

#define LEGACY_ASCII_BUFFER_SIZE 32

static const keyboard_layout_t* legacy_layout = NULL;
static inline const keyboard_layout_t* legacy_get_layout(void) {
    if (!legacy_layout) {
        legacy_layout = keyboard_layout_get_default();
    }
    return legacy_layout;
}
static bool legacy_extended_scancode = false;
static bool legacy_shift_left = false;
static bool legacy_shift_right = false;
static bool legacy_caps_lock = false;

static char legacy_ascii_buffer[LEGACY_ASCII_BUFFER_SIZE];
static volatile uint8 legacy_ascii_head = 0;
static volatile uint8 legacy_ascii_tail = 0;

static void legacy_reset_state(void) {
    legacy_layout = keyboard_layout_get_default();
    legacy_extended_scancode = false;
    legacy_shift_left = false;
    legacy_shift_right = false;
    legacy_caps_lock = false;
    legacy_ascii_head = 0;
    legacy_ascii_tail = 0;
}

static void legacy_enqueue_char(char ch) {
    uint8 next_head = (legacy_ascii_head + 1) % LEGACY_ASCII_BUFFER_SIZE;
    if (next_head == legacy_ascii_tail) {
        legacy_ascii_tail = (legacy_ascii_tail + 1) % LEGACY_ASCII_BUFFER_SIZE;
    }
    legacy_ascii_buffer[legacy_ascii_head] = ch;
    legacy_ascii_head = next_head;
}

static bool legacy_dequeue_char(char* out_char) {
    if (!out_char) {
        return false;
    }
    if (legacy_ascii_head == legacy_ascii_tail) {
        return false;
    }
    *out_char = legacy_ascii_buffer[legacy_ascii_tail];
    legacy_ascii_tail = (legacy_ascii_tail + 1) % LEGACY_ASCII_BUFFER_SIZE;
    return true;
}

void keyboard_set_driver_mode(keyboard_driver_mode_t mode) {
    current_driver_mode = mode;
    if (mode == KEYBOARD_DRIVER_PS2) {
        ps2_keyboard_clear_ascii_buffer();
    } else {
        legacy_reset_state();
    }
}

keyboard_driver_mode_t keyboard_get_driver_mode(void) {
    return current_driver_mode;
}

static bool legacy_update_modifier_state(key_code_t key_code, bool pressed) {
    switch (key_code) {
        case KEY_LEFT_SHIFT:
            legacy_shift_left = pressed;
            return true;
        case KEY_RIGHT_SHIFT:
            legacy_shift_right = pressed;
            return true;
        case KEY_CAPS_LOCK:
            if (pressed) {
                legacy_caps_lock = !legacy_caps_lock;
            }
            return true;
        default:
            return false;
    }
}

static bool legacy_read_char(char* out_char) {
    if (!out_char) {
        return false;
    }

    if (legacy_dequeue_char(out_char)) {
        return true;
    }

    if ((inportb(0x64) & 0x1) == 0) {
        return false;
    }

    uint8 scancode = inportb(0x60);

    if (scancode == 0xE0) {
        legacy_extended_scancode = true;
        return false;
    }
    if (scancode == 0xE1) {
        legacy_extended_scancode = false;
        return false;
    }

    bool release = (scancode & 0x80U) != 0;
    scancode &= 0x7F;

    key_code_t key_code = keyboard_scancode_set1_lookup(scancode, legacy_extended_scancode);
    legacy_extended_scancode = false;

    if (key_code == KEY_UNKNOWN) {
        return false;
    }

    if (release) {
        legacy_update_modifier_state(key_code, false);
        return legacy_dequeue_char(out_char);
    }

    if (legacy_update_modifier_state(key_code, true)) {
        return false;
    }

    char seq[KEYBOARD_MAX_SEQUENCE_LENGTH];
    bool shift = legacy_shift_left || legacy_shift_right;
    uint8 emitted = keyboard_layout_emit_chars(legacy_get_layout(),
                                               key_code,
                                               shift,
                                               legacy_caps_lock,
                                               seq,
                                               sizeof(seq));
    if (emitted == 0) {
        return false;
    }

    for (uint8 i = 0; i < emitted; ++i) {
        legacy_enqueue_char(seq[i]);
    }

    return legacy_dequeue_char(out_char);
}

static bool ps2_read_char(char* out_char) {
    return ps2_keyboard_poll_ascii(out_char);
}

bool keyboard_poll_char(char* out_char) {
    if (!out_char) {
        return false;
    }

    if (current_driver_mode == KEYBOARD_DRIVER_PS2) {
        return ps2_read_char(out_char);
    }

    return legacy_read_char(out_char);
}

// Read a string from the keyboard with guardrails against scancode garbage/overflows.
string readStr() {
    string buffstr = (string)malloc(KB_BUFFER_MAX);
    if (!buffstr) {
#ifndef USERSPACE_BUILD
        PANIC("Keyboard buffer allocation failed");
#else
        return 0;
#endif
    }

    uint8 i = 0;
    buffstr[0] = '\0';

    while (1) {
        char ch = 0;
        bool have_char = false;

        if (current_driver_mode == KEYBOARD_DRIVER_PS2) {
            have_char = ps2_read_char(&ch);
        } else {
            have_char = legacy_read_char(&ch);
        }

        if (!have_char) {
            continue;
        }

        if (ch == '\b') {
            if (i > 0) {
                printch('\b');
                i--;
                buffstr[i] = '\0';
            }
            continue;
        }

        if (ch == '\n') {
            printch('\n');
            buffstr[i] = '\0';
            return buffstr;
        }

        if (i < KB_BUFFER_MAX - 1) {
            printch(ch);
            buffstr[i++] = ch;
            buffstr[i] = '\0';
        } else {
            buffstr[KB_BUFFER_MAX - 1] = '\0';
            return buffstr;
        }
    }
}
