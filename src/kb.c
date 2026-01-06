#include "include/kb.h"
#include "include/ps2_keyboard.h"
#include "include/keyboard_layout.h"
#include "include/debuglog.h"
#include "include/interrupt.h"
#include "include/cpu_ops.h"
#include "include/task.h"
#include "include/signal.h"
#include "include/ps2_mouse.h"

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
    debuglog(DEBUG_INFO, "[KB] legacy char '%c' (0x%02x)\n", *out_char, (uint8)*out_char);
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

    // Check PS/2 status: bit 0 = output buffer full, bit 5 = data from mouse (AUX)
    uint8 status = inportb(0x64);

    // If AUX data is pending, hand it to the mouse driver instead of discarding
    if (status & 0x20) {
        ps2_mouse_poll();
        status = inportb(0x64);
    }

    // Now check if keyboard data is available
    if ((status & 0x01) == 0) {
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
    debuglog(DEBUG_INFO, "[KB] legacy scancode 0x%02x (%s)\n", scancode, release ? "release" : "press");

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
        debuglog(DEBUG_INFO, "[KB] legacy emit '%c' (scancode=0x%02x)\n", seq[i], scancode);
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
        if (ps2_read_char(out_char)) {
            return true;
        }
        // Fallback to legacy polling if PS/2 path didn't deliver a character.
        // This mirrors the old direct-port reader the user reported working.
        static bool warned = false;
        if (legacy_read_char(out_char)) {
            if (!warned) {
                debuglog(DEBUG_WARN, "[KB] PS/2 read empty; falling back to legacy scancode polling\n");
                warned = true;
            }
            return true;
        }
        return false;
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

    // Allow keyboard IRQs to fire while we wait for user input so the TTY
    // actually receives characters when running inside a syscall.
    bool interrupts_were_enabled = irq_are_enabled();
    if (!interrupts_were_enabled) {
        irq_enable_safe();
    }

    while (1) {
        char ch = 0;
        bool have_char = false;

        if (current_driver_mode == KEYBOARD_DRIVER_PS2) {
            have_char = ps2_read_char(&ch);
        } else {
            have_char = legacy_read_char(&ch);
        }

        if (!have_char) {
            // Let CPU sleep until the next interrupt to keep input responsive.
            __asm__ __volatile__("hlt");
            continue;
        }

        if (ch == '\r') {
            ch = '\n';
        }

        if (ch == '\b') {
            if (i > 0) {
                printch('\b');
                i--;
                buffstr[i] = '\0';
            }
            continue;
        }

        // Ctrl+C (ASCII ETX) - terminate current task with SIGINT to mimic shell interrupt.
        if ((uint8)ch == 0x03) {
            print("\n^C\n");
            if (current_task) {
                task_exit(SIGINT, "SIGINT");
            }
            buffstr[0] = '\0';
            break;
        }

        if (ch == '\n') {
            printch('\n');
            buffstr[i] = '\0';
            break;
        }

        if (i < KB_BUFFER_MAX - 1) {
            printch(ch);
            buffstr[i++] = ch;
            buffstr[i] = '\0';
        } else {
            buffstr[KB_BUFFER_MAX - 1] = '\0';
            break;
        }
    }

    if (!interrupts_were_enabled) {
        irq_disable_safe();
    }

    return buffstr;
}
