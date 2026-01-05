#include "keyboard_interrupt_handler.h"
#include "interrupt_driven_io.h"
#include "interrupt_management.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define KEYBOARD_IRQ 1
#define KEYBOARD_VECTOR 0x21
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEYBOARD_COMMAND_PORT 0x64

#define KEYBOARD_STATUS_OUTPUT_BUFFER_FULL 0x01
#define KEYBOARD_STATUS_INPUT_BUFFER_FULL 0x02
#define KEYBOARD_STATUS_SYSTEM_FLAG 0x04
#define KEYBOARD_STATUS_COMMAND_DATA 0x08
#define KEYBOARD_STATUS_KEYBOARD_LOCK 0x10
#define KEYBOARD_STATUS_AUX_OUTPUT_BUFFER_FULL 0x20
#define KEYBOARD_STATUS_TIMEOUT_ERROR 0x40
#define KEYBOARD_STATUS_PARITY_ERROR 0x80

#define KEYBOARD_BUFFER_SIZE 256
#define KEYBOARD_MAX_CALLBACKS 32
#define KEYBOARD_REPEAT_DELAY_MS 500
#define KEYBOARD_REPEAT_RATE_MS 50

/* keyboard_modifier_state_t is defined in keyboard_interrupt_handler.h */

typedef struct {
    uint8_t scancodes[KEYBOARD_BUFFER_SIZE];
    keyboard_key_event_t events[KEYBOARD_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} keyboard_buffer_t;

typedef struct {
    keyboard_event_callback_t callback;
    void *user_data;
    keyboard_event_filter_t filter;
    bool enabled;
} keyboard_callback_entry_t;

typedef struct {
    keyboard_buffer_t scancode_buffer;
    keyboard_buffer_t event_buffer;
    keyboard_modifier_state_t modifiers;
    
    keyboard_callback_entry_t callbacks[KEYBOARD_MAX_CALLBACKS];
    size_t callback_count;
    
    keyboard_layout_t current_layout;
    keyboard_config_t config;
    
    uint64_t total_interrupts;
    uint64_t total_scancodes;
    uint64_t total_key_events;
    uint64_t dropped_scancodes;
    uint64_t repeat_events;
    
    io_device_handle_t device_handle;
    bool initialized;
} keyboard_interrupt_context_t;

static keyboard_interrupt_context_t kbd_ctx = {0};

static const uint8_t scancode_to_ascii_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t scancode_to_ascii_us_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint8_t keyboard_read_data(void) {
    return inb(KEYBOARD_DATA_PORT);
}

static uint8_t keyboard_read_status(void) {
    return inb(KEYBOARD_STATUS_PORT);
}

static void keyboard_write_command(uint8_t command) {
    while (keyboard_read_status() & KEYBOARD_STATUS_INPUT_BUFFER_FULL);
    outb(KEYBOARD_COMMAND_PORT, command);
}

static void keyboard_write_data(uint8_t data) {
    while (keyboard_read_status() & KEYBOARD_STATUS_INPUT_BUFFER_FULL);
    outb(KEYBOARD_DATA_PORT, data);
}

static bool keyboard_buffer_push_scancode(uint8_t scancode) {
    if (kbd_ctx.scancode_buffer.count >= KEYBOARD_BUFFER_SIZE) {
        kbd_ctx.dropped_scancodes++;
        return false;
    }
    
    kbd_ctx.scancode_buffer.scancodes[kbd_ctx.scancode_buffer.tail] = scancode;
    kbd_ctx.scancode_buffer.tail = (kbd_ctx.scancode_buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
    kbd_ctx.scancode_buffer.count++;
    return true;
}

static bool keyboard_buffer_push_event(const keyboard_key_event_t *event) {
    if (kbd_ctx.event_buffer.count >= KEYBOARD_BUFFER_SIZE) {
        return false;
    }
    
    kbd_ctx.event_buffer.events[kbd_ctx.event_buffer.tail] = *event;
    kbd_ctx.event_buffer.tail = (kbd_ctx.event_buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
    kbd_ctx.event_buffer.count++;
    return true;
}

static bool keyboard_buffer_pop_scancode(uint8_t *scancode) {
    if (kbd_ctx.scancode_buffer.count == 0) {
        return false;
    }
    
    *scancode = kbd_ctx.scancode_buffer.scancodes[kbd_ctx.scancode_buffer.head];
    kbd_ctx.scancode_buffer.head = (kbd_ctx.scancode_buffer.head + 1) % KEYBOARD_BUFFER_SIZE;
    kbd_ctx.scancode_buffer.count--;
    return true;
}

static bool keyboard_buffer_pop_event(keyboard_key_event_t *event) {
    if (kbd_ctx.event_buffer.count == 0) {
        return false;
    }
    
    *event = kbd_ctx.event_buffer.events[kbd_ctx.event_buffer.head];
    kbd_ctx.event_buffer.head = (kbd_ctx.event_buffer.head + 1) % KEYBOARD_BUFFER_SIZE;
    kbd_ctx.event_buffer.count--;
    return true;
}

static keyboard_key_code_t scancode_to_keycode(uint8_t scancode) {
    switch (scancode) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return KEY_1;
        case 0x03: return KEY_2;
        case 0x04: return KEY_3;
        case 0x05: return KEY_4;
        case 0x06: return KEY_5;
        case 0x07: return KEY_6;
        case 0x08: return KEY_7;
        case 0x09: return KEY_8;
        case 0x0A: return KEY_9;
        case 0x0B: return KEY_0;
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x14: return KEY_T;
        case 0x15: return KEY_Y;
        case 0x16: return KEY_U;
        case 0x17: return KEY_I;
        case 0x18: return KEY_O;
        case 0x19: return KEY_P;
        case 0x1A: return KEY_LEFT_BRACKET;
        case 0x1B: return KEY_RIGHT_BRACKET;
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_LEFT_CTRL;
        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x21: return KEY_F;
        case 0x22: return KEY_G;
        case 0x23: return KEY_H;
        case 0x24: return KEY_J;
        case 0x25: return KEY_K;
        case 0x26: return KEY_L;
        case 0x27: return KEY_SEMICOLON;
        case 0x28: return KEY_QUOTE;
        case 0x29: return KEY_GRAVE;
        case 0x2A: return KEY_LEFT_SHIFT;
        case 0x2B: return KEY_BACKSLASH;
        case 0x2C: return KEY_Z;
        case 0x2D: return KEY_X;
        case 0x2E: return KEY_C;
        case 0x2F: return KEY_V;
        case 0x30: return KEY_B;
        case 0x31: return KEY_N;
        case 0x32: return KEY_M;
        case 0x33: return KEY_COMMA;
        case 0x34: return KEY_PERIOD;
        case 0x35: return KEY_SLASH;
        case 0x36: return KEY_RIGHT_SHIFT;
        case 0x37: return KEY_KP_MULTIPLY;
        case 0x38: return KEY_LEFT_ALT;
        case 0x39: return KEY_SPACE;
        case 0x3A: return KEY_CAPS_LOCK;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x45: return KEY_NUM_LOCK;
        case 0x46: return KEY_SCROLL_LOCK;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        default: return KEY_UNKNOWN;
    }
}

static char keycode_to_ascii(keyboard_key_code_t keycode, bool shift_pressed, bool caps_lock) {
    if (keycode >= 128) {
        return 0;
    }
    
    bool use_shift = shift_pressed ^ (caps_lock && keycode >= KEY_A && keycode <= KEY_Z);
    
    if (use_shift) {
        return scancode_to_ascii_us_shift[keycode];
    } else {
        return scancode_to_ascii_us[keycode];
    }
}

static void update_modifier_state(uint8_t scancode, bool key_pressed) {
    switch (scancode) {
        case 0x2A:  // Left Shift
        case 0x36:  // Right Shift
            kbd_ctx.modifiers.shift_pressed = key_pressed;
            break;
        case 0x1D:  // Left Ctrl
            kbd_ctx.modifiers.ctrl_pressed = key_pressed;
            break;
        case 0x38:  // Left Alt
            kbd_ctx.modifiers.alt_pressed = key_pressed;
            break;
        case 0x3A:  // Caps Lock
            if (key_pressed) {
                kbd_ctx.modifiers.caps_lock = !kbd_ctx.modifiers.caps_lock;
            }
            break;
        case 0x45:  // Num Lock
            if (key_pressed) {
                kbd_ctx.modifiers.num_lock = !kbd_ctx.modifiers.num_lock;
            }
            break;
        case 0x46:  // Scroll Lock
            if (key_pressed) {
                kbd_ctx.modifiers.scroll_lock = !kbd_ctx.modifiers.scroll_lock;
            }
            break;
    }
}

static void process_scancode(uint8_t scancode) {
    bool key_pressed = !(scancode & 0x80);
    uint8_t base_scancode = scancode & 0x7F;
    
    if (scancode == 0xE0) {
        kbd_ctx.modifiers.extended_scancode = true;
        return;
    }
    
    update_modifier_state(base_scancode, key_pressed);
    
    keyboard_key_code_t keycode = scancode_to_keycode(base_scancode);
    if (keycode == KEY_UNKNOWN) {
        return;
    }
    
    keyboard_key_event_t event = {0};
    event.keycode = keycode;
    event.scancode = base_scancode;
    event.pressed = key_pressed;
    event.extended = kbd_ctx.modifiers.extended_scancode;
    event.shift_pressed = kbd_ctx.modifiers.shift_pressed;
    event.ctrl_pressed = kbd_ctx.modifiers.ctrl_pressed;
    event.alt_pressed = kbd_ctx.modifiers.alt_pressed;
    event.caps_lock = kbd_ctx.modifiers.caps_lock;
    event.num_lock = kbd_ctx.modifiers.num_lock;
    event.scroll_lock = kbd_ctx.modifiers.scroll_lock;
    event.ascii = keycode_to_ascii(keycode, event.shift_pressed, event.caps_lock);
    event.timestamp = rdtsc();
    
    if (key_pressed) {
        kbd_ctx.modifiers.last_scancode = base_scancode;
        kbd_ctx.modifiers.last_keypress_time = event.timestamp;
        
        if (kbd_ctx.config.enable_key_repeat) {
            kbd_ctx.modifiers.repeat_start_time = event.timestamp + 
                (kbd_ctx.config.repeat_delay_ms * tsc_frequency_hz) / 1000;
            kbd_ctx.modifiers.repeat_active = false;
        }
    }
    
    keyboard_buffer_push_event(&event);
    kbd_ctx.total_key_events++;
    
    for (size_t i = 0; i < kbd_ctx.callback_count; i++) {
        keyboard_callback_entry_t *cb = &kbd_ctx.callbacks[i];
        if (cb->enabled && cb->callback) {
            if (!cb->filter.filter_enabled ||
                (cb->filter.key_pressed_only && key_pressed) ||
                (cb->filter.key_released_only && !key_pressed) ||
                (cb->filter.modifier_keys_only && 
                 (keycode == KEY_LEFT_SHIFT || keycode == KEY_RIGHT_SHIFT ||
                  keycode == KEY_LEFT_CTRL || keycode == KEY_LEFT_ALT))) {
                cb->callback(&event, cb->user_data);
            }
        }
    }
    
    kbd_ctx.modifiers.extended_scancode = false;
}

static void handle_key_repeat(void) {
    if (!kbd_ctx.config.enable_key_repeat || 
        kbd_ctx.modifiers.last_scancode == 0) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    
    if (!kbd_ctx.modifiers.repeat_active && 
        current_time >= kbd_ctx.modifiers.repeat_start_time) {
        kbd_ctx.modifiers.repeat_active = true;
    }
    
    if (kbd_ctx.modifiers.repeat_active) {
        uint64_t repeat_interval = (kbd_ctx.config.repeat_rate_ms * tsc_frequency_hz) / 1000;
        if (current_time >= kbd_ctx.modifiers.last_keypress_time + repeat_interval) {
            process_scancode(kbd_ctx.modifiers.last_scancode);
            kbd_ctx.repeat_events++;
            kbd_ctx.modifiers.last_keypress_time = current_time;
        }
    }
}

static io_operation_result_t keyboard_interrupt_handler(void *device_data) {
    kbd_ctx.total_interrupts++;
    
    uint8_t status = keyboard_read_status();
    
    if (!(status & KEYBOARD_STATUS_OUTPUT_BUFFER_FULL)) {
        return (io_operation_result_t){
            .status = IO_STATUS_COMPLETED,
            .error = IO_SUCCESS,
            .bytes_transferred = 0
        };
    }
    
    uint8_t scancode = keyboard_read_data();
    
    if (!keyboard_buffer_push_scancode(scancode)) {
        kbd_ctx.dropped_scancodes++;
    } else {
        kbd_ctx.total_scancodes++;
        process_scancode(scancode);
    }
    
    return (io_operation_result_t){
        .status = IO_STATUS_COMPLETED,
        .error = IO_SUCCESS,
        .bytes_transferred = 1
    };
}

static io_operation_result_t keyboard_initialize(void *device_data) {
    keyboard_write_command(0xAE);
    
    keyboard_write_data(0xF4);
    
    uint8_t response = keyboard_read_data();
    if (response != 0xFA) {
        return (io_operation_result_t){
            .status = IO_STATUS_ERROR,
            .error = IO_ERROR_HARDWARE_ERROR,
            .bytes_transferred = 0
        };
    }
    
    return (io_operation_result_t){
        .status = IO_STATUS_COMPLETED,
        .error = IO_SUCCESS,
        .bytes_transferred = 0
    };
}

static void keyboard_enable_interrupts(void *device_data, bool enable) {
    if (enable) {
        keyboard_write_command(0xAE);
    } else {
        keyboard_write_command(0xAD);
    }
}

keyboard_error_t keyboard_interrupt_init(const keyboard_config_t *config) {
    if (!config) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    memset(&kbd_ctx, 0, sizeof(kbd_ctx));
    kbd_ctx.config = *config;
    kbd_ctx.current_layout = KEYBOARD_LAYOUT_US;
    
    io_device_descriptor_t device_desc = {
        .type = IO_DEVICE_INPUT,
        .name = "PS/2 Keyboard",
        .interrupt_vector = KEYBOARD_VECTOR,
        .base_address = KEYBOARD_DATA_PORT,
        .memory_size = 0,
        .capabilities = {
            .supports_read = true,
            .supports_write = false,
            .supports_async = true,
            .supports_dma = false,
            .supports_scatter_gather = false,
            .supports_priority = false,
            .max_transfer_size = 1,
            .alignment_requirement = 1
        },
        .operations = {
            .initialize = keyboard_initialize,
            .shutdown = NULL,
            .read = NULL,
            .write = NULL,
            .control = NULL,
            .interrupt_handler = keyboard_interrupt_handler,
            .enable_interrupts = keyboard_enable_interrupts,
            .cancel = NULL
        },
        .device_data = NULL
    };
    
    if (io_register_device(&device_desc, &kbd_ctx.device_handle) != IO_SUCCESS) {
        return KEYBOARD_ERROR_DEVICE_REGISTRATION_FAILED;
    }
    
    kbd_ctx.initialized = true;
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_register_callback(keyboard_event_callback_t callback, 
                                          void *user_data,
                                          const keyboard_event_filter_t *filter) {
    if (!kbd_ctx.initialized || !callback) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    if (kbd_ctx.callback_count >= KEYBOARD_MAX_CALLBACKS) {
        return KEYBOARD_ERROR_NO_SPACE;
    }
    
    keyboard_callback_entry_t *entry = &kbd_ctx.callbacks[kbd_ctx.callback_count];
    entry->callback = callback;
    entry->user_data = user_data;
    entry->enabled = true;
    
    if (filter) {
        entry->filter = *filter;
    } else {
        entry->filter.filter_enabled = false;
    }
    
    kbd_ctx.callback_count++;
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_get_key_event(keyboard_key_event_t *event) {
    if (!kbd_ctx.initialized || !event) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    if (kbd_ctx.config.enable_key_repeat) {
        handle_key_repeat();
    }
    
    if (!keyboard_buffer_pop_event(event)) {
        return KEYBOARD_ERROR_NO_DATA;
    }
    
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_get_scancode(uint8_t *scancode) {
    if (!kbd_ctx.initialized || !scancode) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    if (!keyboard_buffer_pop_scancode(scancode)) {
        return KEYBOARD_ERROR_NO_DATA;
    }
    
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_get_modifier_state(keyboard_modifier_state_t *state) {
    if (!kbd_ctx.initialized || !state) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    *state = kbd_ctx.modifiers;
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_set_leds(bool caps_lock, bool num_lock, bool scroll_lock) {
    if (!kbd_ctx.initialized) {
        return KEYBOARD_ERROR_NOT_INITIALIZED;
    }
    
    uint8_t led_state = 0;
    if (caps_lock) led_state |= 0x04;
    if (num_lock) led_state |= 0x02;
    if (scroll_lock) led_state |= 0x01;
    
    keyboard_write_data(0xED);
    keyboard_write_data(led_state);
    
    uint8_t response = keyboard_read_data();
    if (response != 0xFA) {
        return KEYBOARD_ERROR_HARDWARE_ERROR;
    }
    
    return KEYBOARD_SUCCESS;
}

keyboard_error_t keyboard_get_statistics(keyboard_statistics_t *stats) {
    if (!kbd_ctx.initialized || !stats) {
        return KEYBOARD_ERROR_INVALID_PARAMS;
    }
    
    *stats = (keyboard_statistics_t){
        .total_interrupts = kbd_ctx.total_interrupts,
        .total_scancodes = kbd_ctx.total_scancodes,
        .total_key_events = kbd_ctx.total_key_events,
        .dropped_scancodes = kbd_ctx.dropped_scancodes,
        .repeat_events = kbd_ctx.repeat_events,
        .buffer_scancodes_pending = kbd_ctx.scancode_buffer.count,
        .buffer_events_pending = kbd_ctx.event_buffer.count,
        .callbacks_registered = kbd_ctx.callback_count
    };
    
    return KEYBOARD_SUCCESS;
}

void keyboard_process_events(void) {
    if (!kbd_ctx.initialized) {
        return;
    }
    
    io_process_completions();
    
    if (kbd_ctx.config.enable_key_repeat) {
        handle_key_repeat();
    }
}

bool keyboard_is_key_pressed(keyboard_key_code_t keycode) {
    switch (keycode) {
        case KEY_LEFT_SHIFT:
        case KEY_RIGHT_SHIFT:
            return kbd_ctx.modifiers.shift_pressed;
        case KEY_LEFT_CTRL:
            return kbd_ctx.modifiers.ctrl_pressed;
        case KEY_LEFT_ALT:
            return kbd_ctx.modifiers.alt_pressed;
        case KEY_CAPS_LOCK:
            return kbd_ctx.modifiers.caps_lock;
        case KEY_NUM_LOCK:
            return kbd_ctx.modifiers.num_lock;
        case KEY_SCROLL_LOCK:
            return kbd_ctx.modifiers.scroll_lock;
        default:
            return false;
    }
}

bool keyboard_is_initialized(void) {
    return kbd_ctx.initialized;
}

size_t keyboard_get_pending_events(void) {
    return kbd_ctx.event_buffer.count;
}