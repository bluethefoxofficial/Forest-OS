#include "include/kb.h"
#include "include/ps2_keyboard.h"
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
static keyboard_driver_mode_t current_driver_mode = KEYBOARD_DRIVER_PS2;

#define LEGACY_ASCII_BUFFER_SIZE 32
#define SERIAL_ASCII_BUFFER_SIZE 256

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

static char serial_ascii_buffer[SERIAL_ASCII_BUFFER_SIZE];
static volatile uint16 serial_ascii_head = 0;
static volatile uint16 serial_ascii_tail = 0;

// Serial keyboard state tracking
#define SERIAL_MAX_KEYS 10
static uint8 serial_pressed_keys[SERIAL_MAX_KEYS];
static uint8 serial_pressed_count = 0;
static bool serial_modifiers[8]; // Ctrl, Alt, Shift, etc.

// Serial input state machine
typedef enum {
    SERIAL_STATE_NORMAL,
    SERIAL_STATE_ESC,
    SERIAL_STATE_CSI,
    SERIAL_STATE_SS3
} serial_input_state_t;

static serial_input_state_t serial_input_state = SERIAL_STATE_NORMAL;
static char serial_escape_buffer[16];
static uint8 serial_escape_pos = 0;

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

static void serial_enqueue_char(char ch) {
    uint16 next_head = (serial_ascii_head + 1) % SERIAL_ASCII_BUFFER_SIZE;
    if (next_head == serial_ascii_tail) {
        // Buffer full, discard oldest character
        serial_ascii_tail = (serial_ascii_tail + 1) % SERIAL_ASCII_BUFFER_SIZE;
    }
    serial_ascii_buffer[serial_ascii_head] = ch;
    serial_ascii_head = next_head;
}

static bool serial_dequeue_char(char* out_char) {
    if (!out_char) {
        return false;
    }
    if (serial_ascii_head == serial_ascii_tail) {
        return false;
    }
    *out_char = serial_ascii_buffer[serial_ascii_tail];
    serial_ascii_tail = (serial_ascii_tail + 1) % SERIAL_ASCII_BUFFER_SIZE;
    return true;
}

void keyboard_serial_init(void) {
    // Initialize serial keyboard state
    serial_ascii_head = 0;
    serial_ascii_tail = 0;
    serial_pressed_count = 0;
    serial_input_state = SERIAL_STATE_NORMAL;
    serial_escape_pos = 0;

    // Clear modifiers
    for (int i = 0; i < 8; i++) {
        serial_modifiers[i] = false;
    }
}

void keyboard_set_driver_mode(keyboard_driver_mode_t mode) {
    current_driver_mode = mode;
    if (mode == KEYBOARD_DRIVER_PS2) {
        ps2_keyboard_clear_ascii_buffer();
    } else if (mode == KEYBOARD_DRIVER_SERIAL) {
        keyboard_serial_init();
    } else {
        legacy_reset_state();
    }
}

static void serial_add_pressed_key(uint8 key_code) {
    // Check if key is already pressed
    for (int i = 0; i < serial_pressed_count; i++) {
        if (serial_pressed_keys[i] == key_code) {
            return; // Already pressed
        }
    }

    // Add to pressed keys if space available
    if (serial_pressed_count < SERIAL_MAX_KEYS) {
        serial_pressed_keys[serial_pressed_count++] = key_code;
    }
}

static void serial_remove_pressed_key(uint8 key_code) {
    for (int i = 0; i < serial_pressed_count; i++) {
        if (serial_pressed_keys[i] == key_code) {
            // Remove by shifting remaining keys
            for (int j = i; j < serial_pressed_count - 1; j++) {
                serial_pressed_keys[j] = serial_pressed_keys[j + 1];
            }
            serial_pressed_count--;
            break;
        }
    }
}

static void serial_process_escape_sequence(void) {
    // Handle VT100 escape sequences
    if (serial_escape_pos >= 2 && serial_escape_buffer[1] == '[') {
        // CSI sequences
        if (serial_escape_pos >= 3) {
            char final_char = serial_escape_buffer[serial_escape_pos - 1];
            char intermediate = (serial_escape_pos >= 4) ? serial_escape_buffer[serial_escape_pos - 2] : 0;

            // Handle special keys
            uint8 key_code = 0;
            switch (final_char) {
                case 'A': key_code = 0x10; break; // Up arrow
                case 'B': key_code = 0x11; break; // Down arrow
                case 'C': key_code = 0x12; break; // Right arrow
                case 'D': key_code = 0x13; break; // Left arrow
                case '~':
                    // Function keys, etc.
                    if (serial_escape_pos >= 4) {
                        char param = serial_escape_buffer[2];
                        switch (param) {
                            case '2': key_code = 0x14; break; // Insert
                            case '3': key_code = 0x7F; break; // Delete
                            case '5': key_code = 0x15; break; // Page Up
                            case '6': key_code = 0x16; break; // Page Down
                            case '1': case '7': key_code = 0x17; break; // Home
                            case '4': case '8': key_code = 0x18; break; // End
                        }
                    }
                    break;
            }

            if (key_code) {
                serial_add_pressed_key(key_code);
                serial_enqueue_char(key_code);
            }
        }
    } else if (serial_escape_pos == 1) {
        // Single character escape sequences
        switch (serial_escape_buffer[0]) {
            case 'O': // SS3 sequences (F1-F4)
                break;
        }
    }
}

static void serial_process_character(char c) {
    switch (serial_input_state) {
        case SERIAL_STATE_NORMAL:
            if (c == '\x1B') { // ESC
                serial_input_state = SERIAL_STATE_ESC;
                serial_escape_pos = 0;
            } else {
                // Handle regular characters and control sequences
                char output = c;

                // Check for control key combinations
                if ((c >= 1 && c <= 26) && c != '\n' && c != '\r' && c != '\t' && c != '\b') {
                    // This is a Ctrl+letter combination (Ctrl+A = 1, Ctrl+B = 2, etc.)
                    // Convert back to the letter and mark Ctrl as pressed
                    char letter = 'a' + (c - 1);
                    serial_modifiers[0] = true; // Ctrl pressed
                    output = letter;
                    serial_add_pressed_key(c); // Track the control code
                } else if (c >= 32 || c == '\n' || c == '\r' || c == '\t' || c == '\b') {
                    // Regular printable character or control character
                    serial_add_pressed_key(c);
                }

                // Apply modifiers to output
                if (serial_modifiers[0] && output >= 'a' && output <= 'z') {
                    // Convert to control character
                    output = output - 'a' + 1;
                }

                // Special handling for debug toggle
                if (c == 4) { // Ctrl+D
                    output = 4; // Keep as-is for debug toggle
                }

                serial_enqueue_char(output);

                // Track modifier state based on key events
                // Modifiers are maintained until explicitly released
                // For serial input, modifiers are inferred from escape sequences
            }
            break;

        case SERIAL_STATE_ESC:
            serial_escape_buffer[serial_escape_pos++] = c;
            if (c == '[') {
                serial_input_state = SERIAL_STATE_CSI;
            } else if (c == 'O') {
                serial_input_state = SERIAL_STATE_SS3;
            } else {
                // Single character escape sequence
                serial_process_escape_sequence();
                serial_input_state = SERIAL_STATE_NORMAL;
            }
            break;

        case SERIAL_STATE_CSI:
        case SERIAL_STATE_SS3:
            serial_escape_buffer[serial_escape_pos++] = c;
            if ((c >= '@' && c <= '~') || serial_escape_pos >= sizeof(serial_escape_buffer)) {
                // End of escape sequence
                serial_process_escape_sequence();
                serial_input_state = SERIAL_STATE_NORMAL;
            }
            break;
    }
}

void keyboard_serial_interrupt_handler(struct interrupt_frame* frame, uint32_t error_code) {
    (void)frame;
    (void)error_code;

    // Process all available characters
    while (inportb(0x3F8 + 5) & 0x01) {  // Data ready bit
        char c = inportb(0x3F8);  // Read character
        serial_process_character(c);
    }

    // Send EOI to PIC
    pic_send_eoi(4);
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

static bool serial_read_char(char* out_char) {
    return serial_dequeue_char(out_char);
}

bool keyboard_poll_char(char* out_char) {
    if (!out_char) {
        return false;
    }

    // Try serial input first (for serial console)
    if (serial_read_char(out_char)) {
        return true;
    }

    // Fall back to PS2/legacy keyboard
    if (current_driver_mode == KEYBOARD_DRIVER_PS2) {
        if (ps2_read_char(out_char)) {
            return true;
        }
    }

    if (legacy_read_char(out_char)) {
        return true;
    }

    return false;
}

void keyboard_clear_buffers(void) {
    serial_ascii_head = 0;
    serial_ascii_tail = 0;
    legacy_ascii_head = 0;
    legacy_ascii_tail = 0;
    ps2_keyboard_clear_ascii_buffer();
}

// Debug function to get pressed key count
uint8 keyboard_get_serial_pressed_count(void) {
    return serial_pressed_count;
}

// Debug function to get pressed keys
const uint8* keyboard_get_serial_pressed_keys(void) {
    return serial_pressed_keys;
}

// Convert keyboard scancode to ASCII character
char keyboard_scancode_to_ascii(uint8 keycode) {
    // For now, implement a simple mapping for common scancodes
    switch (keycode) {
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return '-';
        case 0x0D: return '=';
        case 0x0E: return '\b';  // Backspace
        case 0x0F: return '\t';  // Tab
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1A: return '[';
        case 0x1B: return ']';
        case 0x1C: return '\n';  // Enter
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
        case 0x2B: return '\\';
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x39: return ' ';  // Space
        default: return 0;  // Unknown or non-printable
    }
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
