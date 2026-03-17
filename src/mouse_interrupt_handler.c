#include "mouse_interrupt_handler.h"
#include "interrupt_driven_io.h"
#include "interrupt_management.h"
#include "input_event.h"
#include "input_mux.h"
#include "devfs.h"
#include "timer.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MOUSE_IRQ 12
#define MOUSE_VECTOR 0x2C
#define MOUSE_DATA_PORT 0x60
#define MOUSE_COMMAND_PORT 0x64
#define MOUSE_STATUS_PORT 0x64

#define MOUSE_EVENT_BUFFER_SIZE 256
#define MOUSE_MAX_CALLBACKS 16
#define MOUSE_PACKET_SIZE 3
#define MOUSE_WHEEL_PACKET_SIZE 4
#define MOUSE_TIMEOUT_MS 100

#define MOUSE_STATUS_OUTPUT_BUFFER_FULL 0x01
#define MOUSE_STATUS_INPUT_BUFFER_FULL 0x02
#define MOUSE_STATUS_AUX_OUTPUT_BUFFER_FULL 0x20

#define MOUSE_CMD_ENABLE_AUX 0xA8
#define MOUSE_CMD_DISABLE_AUX 0xA7
#define MOUSE_CMD_TEST_AUX 0xA9
#define MOUSE_CMD_WRITE_AUX 0xD4

#define MOUSE_DEV_RESET 0xFF
#define MOUSE_DEV_ENABLE_STREAMING 0xF4
#define MOUSE_DEV_DISABLE_STREAMING 0xF5
#define MOUSE_DEV_SET_SAMPLE_RATE 0xF3
#define MOUSE_DEV_GET_DEVICE_ID 0xF2
#define MOUSE_DEV_SET_RESOLUTION 0xE8
#define MOUSE_DEV_SET_SCALING_1_1 0xE6
#define MOUSE_DEV_SET_SCALING_2_1 0xE7

#define MOUSE_PACKET_LEFT_BUTTON 0x01
#define MOUSE_PACKET_RIGHT_BUTTON 0x02
#define MOUSE_PACKET_MIDDLE_BUTTON 0x04
#define MOUSE_PACKET_X_SIGN 0x10
#define MOUSE_PACKET_Y_SIGN 0x20
#define MOUSE_PACKET_X_OVERFLOW 0x40
#define MOUSE_PACKET_Y_OVERFLOW 0x80

typedef struct {
    uint8_t packet_data[MOUSE_WHEEL_PACKET_SIZE];
    size_t packet_index;
    size_t expected_packet_size;
    bool packet_ready;
} mouse_packet_state_t;

typedef struct {
    mouse_event_t events[MOUSE_EVENT_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} mouse_event_buffer_t;

typedef struct {
    mouse_event_callback_t callback;
    void *user_data;
    mouse_event_filter_t filter;
    bool enabled;
} mouse_callback_entry_t;

typedef struct {
    mouse_event_buffer_t event_buffer;
    mouse_packet_state_t packet_state;
    
    mouse_callback_entry_t callbacks[MOUSE_MAX_CALLBACKS];
    size_t callback_count;
    
    mouse_state_t current_state;
    mouse_config_t config;
    
    uint64_t total_interrupts;
    uint64_t total_packets;
    uint64_t total_events;
    uint64_t dropped_packets;
    uint64_t invalid_packets;
    
    io_device_handle_t device_handle;
    bool initialized;
    bool wheel_supported;
    mouse_device_type_t device_type;
} mouse_interrupt_context_t;

static mouse_interrupt_context_t mouse_ctx = {0};

static void dispatch_input_event(const input_event_t *ev) {
    if (!ev) {
        return;
    }

    if (devfs_is_initialized()) {
        devfs_mouse_queue_event(ev);
    }

    if (input_mux_is_initialized()) {
        input_mux_dispatch_event(ev);
    }
}

static uint8_t mouse_read_data(void) {
    return inb(MOUSE_DATA_PORT);
}

static uint8_t mouse_read_status(void) {
    return inb(MOUSE_STATUS_PORT);
}

static void mouse_write_command(uint8_t command) {
    while (mouse_read_status() & MOUSE_STATUS_INPUT_BUFFER_FULL);
    outb(MOUSE_COMMAND_PORT, command);
}

static void mouse_write_data(uint8_t data) {
    while (mouse_read_status() & MOUSE_STATUS_INPUT_BUFFER_FULL);
    outb(MOUSE_DATA_PORT, data);
}

static uint8_t mouse_read_response(void) {
    uint32_t timeout = 100000;
    while (timeout-- > 0) {
        if (mouse_read_status() & MOUSE_STATUS_OUTPUT_BUFFER_FULL) {
            return mouse_read_data();
        }
    }
    return 0xFE; // Timeout
}

static void mouse_send_command(uint8_t command) {
    mouse_write_command(MOUSE_CMD_WRITE_AUX);
    mouse_write_data(command);
}

static bool mouse_detect_wheel_support(void) {
    mouse_send_command(MOUSE_DEV_SET_SAMPLE_RATE);
    mouse_write_data(200);
    if (mouse_read_response() != 0xFA) return false;
    
    mouse_send_command(MOUSE_DEV_SET_SAMPLE_RATE);
    mouse_write_data(100);
    if (mouse_read_response() != 0xFA) return false;
    
    mouse_send_command(MOUSE_DEV_SET_SAMPLE_RATE);
    mouse_write_data(80);
    if (mouse_read_response() != 0xFA) return false;
    
    mouse_send_command(MOUSE_DEV_GET_DEVICE_ID);
    uint8_t device_id = mouse_read_response();
    
    if (device_id == 0x03) {
        mouse_ctx.device_type = MOUSE_TYPE_INTELLIMOUSE;
        return true;
    } else if (device_id == 0x04) {
        mouse_ctx.device_type = MOUSE_TYPE_INTELLIMOUSE_EXPLORER;
        return true;
    }
    
    mouse_ctx.device_type = MOUSE_TYPE_STANDARD;
    return false;
}

static bool mouse_buffer_push_event(const mouse_event_t *event) {
    if (mouse_ctx.event_buffer.count >= MOUSE_EVENT_BUFFER_SIZE) {
        return false;
    }
    
    mouse_ctx.event_buffer.events[mouse_ctx.event_buffer.tail] = *event;
    mouse_ctx.event_buffer.tail = (mouse_ctx.event_buffer.tail + 1) % MOUSE_EVENT_BUFFER_SIZE;
    mouse_ctx.event_buffer.count++;
    return true;
}

static bool mouse_buffer_pop_event(mouse_event_t *event) {
    if (mouse_ctx.event_buffer.count == 0) {
        return false;
    }
    
    *event = mouse_ctx.event_buffer.events[mouse_ctx.event_buffer.head];
    mouse_ctx.event_buffer.head = (mouse_ctx.event_buffer.head + 1) % MOUSE_EVENT_BUFFER_SIZE;
    mouse_ctx.event_buffer.count--;
    return true;
}

static void process_mouse_packet(void) {
    uint8_t *packet = mouse_ctx.packet_state.packet_data;
    
    if (!(packet[0] & 0x08)) {
        mouse_ctx.invalid_packets++;
        return;
    }
    
    mouse_event_t event = {0};
    event.timestamp = rdtsc();
    
    event.buttons.left = (packet[0] & MOUSE_PACKET_LEFT_BUTTON) != 0;
    event.buttons.right = (packet[0] & MOUSE_PACKET_RIGHT_BUTTON) != 0;
    event.buttons.middle = (packet[0] & MOUSE_PACKET_MIDDLE_BUTTON) != 0;
    
    int16_t delta_x = packet[1];
    if (packet[0] & MOUSE_PACKET_X_SIGN) {
        delta_x |= 0xFF00;
    }
    
    int16_t delta_y = packet[2];
    if (packet[0] & MOUSE_PACKET_Y_SIGN) {
        delta_y |= 0xFF00;
    }
    
    event.movement.delta_x = delta_x;
    event.movement.delta_y = -delta_y; // Invert Y for standard coordinate system
    
    if (packet[0] & MOUSE_PACKET_X_OVERFLOW) {
        event.movement.delta_x = 0;
    }
    if (packet[0] & MOUSE_PACKET_Y_OVERFLOW) {
        event.movement.delta_y = 0;
    }
    
    if (mouse_ctx.wheel_supported && mouse_ctx.packet_state.expected_packet_size == 4) {
        int8_t wheel_delta = (int8_t)(packet[3] & 0x0F);
        if (packet[3] & 0x08) {
            wheel_delta |= 0xF0;
        }
        event.wheel.delta_vertical = wheel_delta;
        
        if (mouse_ctx.device_type == MOUSE_TYPE_INTELLIMOUSE_EXPLORER) {
            event.buttons.button4 = (packet[3] & 0x10) != 0;
            event.buttons.button5 = (packet[3] & 0x20) != 0;
        }
    }
    
    mouse_buttons_t prev_buttons = mouse_ctx.current_state.buttons;

    mouse_ctx.current_state.position.x += event.movement.delta_x;
    mouse_ctx.current_state.position.y += event.movement.delta_y;
    
    if (mouse_ctx.config.enable_bounds_checking) {
        if (mouse_ctx.current_state.position.x < 0) {
            mouse_ctx.current_state.position.x = 0;
        }
        if (mouse_ctx.current_state.position.y < 0) {
            mouse_ctx.current_state.position.y = 0;
        }
        if (mouse_ctx.current_state.position.x >= (int32_t)mouse_ctx.config.screen_width) {
            mouse_ctx.current_state.position.x = (int32_t)mouse_ctx.config.screen_width - 1;
        }
        if (mouse_ctx.current_state.position.y >= (int32_t)mouse_ctx.config.screen_height) {
            mouse_ctx.current_state.position.y = (int32_t)mouse_ctx.config.screen_height - 1;
        }
    }
    
    event.position = mouse_ctx.current_state.position;
    
    event.event_type = 0;
    if (event.movement.delta_x != 0 || event.movement.delta_y != 0) {
        event.event_type |= MOUSE_EVENT_MOVEMENT;
    }
    if (event.buttons.left != prev_buttons.left ||
        event.buttons.right != prev_buttons.right ||
        event.buttons.middle != prev_buttons.middle ||
        event.buttons.button4 != prev_buttons.button4 ||
        event.buttons.button5 != prev_buttons.button5) {
        event.event_type |= MOUSE_EVENT_BUTTON;
    }
    if (event.wheel.delta_vertical != 0 || event.wheel.delta_horizontal != 0) {
        event.event_type |= MOUSE_EVENT_WHEEL;
    }
    
    mouse_ctx.current_state.buttons = event.buttons;
    
    mouse_buffer_push_event(&event);
    mouse_ctx.total_events++;
    
    for (size_t i = 0; i < mouse_ctx.callback_count; i++) {
        mouse_callback_entry_t *cb = &mouse_ctx.callbacks[i];
        if (cb->enabled && cb->callback) {
            bool should_call = !cb->filter.filter_enabled;
            
            if (cb->filter.filter_enabled) {
                if ((cb->filter.movement_only && (event.event_type & MOUSE_EVENT_MOVEMENT)) ||
                    (cb->filter.button_only && (event.event_type & MOUSE_EVENT_BUTTON)) ||
                    (cb->filter.wheel_only && (event.event_type & MOUSE_EVENT_WHEEL)) ||
                    (!cb->filter.movement_only && !cb->filter.button_only && !cb->filter.wheel_only)) {
                    should_call = true;
                }
            }
            
            if (should_call) {
                cb->callback(&event, cb->user_data);
            }
        }
    }

    /* Publish standardized input events for consumers (devfs, input mux) */
    if (event.event_type != 0) {
        input_event_t input_ev = {0};
        uint32_t ticks = timer_get_ticks();
        input_ev.tv_sec = ticks / 1000;
        input_ev.tv_usec = (ticks % 1000) * 1000;

        if (event.movement.delta_x != 0) {
            input_ev.type = EV_REL;
            input_ev.code = REL_X;
            input_ev.value = event.movement.delta_x;
            dispatch_input_event(&input_ev);
        }

        if (event.movement.delta_y != 0) {
            input_ev.type = EV_REL;
            input_ev.code = REL_Y;
            input_ev.value = event.movement.delta_y;
            dispatch_input_event(&input_ev);
        }

        if (event.wheel.delta_vertical != 0) {
            input_ev.type = EV_REL;
            input_ev.code = REL_WHEEL;
            input_ev.value = event.wheel.delta_vertical;
            dispatch_input_event(&input_ev);
        }

        if (event.wheel.delta_horizontal != 0) {
            input_ev.type = EV_REL;
            input_ev.code = REL_HWHEEL;
            input_ev.value = event.wheel.delta_horizontal;
            dispatch_input_event(&input_ev);
        }

        if (event.buttons.left != prev_buttons.left) {
            input_ev.type = EV_KEY;
            input_ev.code = BTN_LEFT;
            input_ev.value = event.buttons.left ? KEY_PRESS : KEY_RELEASE;
            dispatch_input_event(&input_ev);
        }

        if (event.buttons.right != prev_buttons.right) {
            input_ev.type = EV_KEY;
            input_ev.code = BTN_RIGHT;
            input_ev.value = event.buttons.right ? KEY_PRESS : KEY_RELEASE;
            dispatch_input_event(&input_ev);
        }

        if (event.buttons.middle != prev_buttons.middle) {
            input_ev.type = EV_KEY;
            input_ev.code = BTN_MIDDLE;
            input_ev.value = event.buttons.middle ? KEY_PRESS : KEY_RELEASE;
            dispatch_input_event(&input_ev);
        }

        if (event.buttons.button4 != prev_buttons.button4) {
            input_ev.type = EV_KEY;
            input_ev.code = BTN_FORWARD;
            input_ev.value = event.buttons.button4 ? KEY_PRESS : KEY_RELEASE;
            dispatch_input_event(&input_ev);
        }

        if (event.buttons.button5 != prev_buttons.button5) {
            input_ev.type = EV_KEY;
            input_ev.code = BTN_BACK;
            input_ev.value = event.buttons.button5 ? KEY_PRESS : KEY_RELEASE;
            dispatch_input_event(&input_ev);
        }

        input_ev.type = EV_SYN;
        input_ev.code = SYN_REPORT;
        input_ev.value = 0;
        dispatch_input_event(&input_ev);
    }
}

static io_operation_result_t mouse_interrupt_handler(void *device_data) {
    (void)device_data;
    mouse_ctx.total_interrupts++;
    
    uint8_t status = mouse_read_status();
    
    if (!(status & MOUSE_STATUS_AUX_OUTPUT_BUFFER_FULL)) {
        return (io_operation_result_t){
            .status = IO_STATUS_COMPLETED,
            .error = IO_SUCCESS,
            .bytes_transferred = 0
        };
    }
    
    uint8_t data = mouse_read_data();
    
    mouse_packet_state_t *packet = &mouse_ctx.packet_state;
    
    if (packet->packet_index == 0 && !(data & 0x08)) {
        mouse_ctx.invalid_packets++;
        return (io_operation_result_t){
            .status = IO_STATUS_COMPLETED,
            .error = IO_SUCCESS,
            .bytes_transferred = 1
        };
    }
    
    packet->packet_data[packet->packet_index] = data;
    packet->packet_index++;
    
    if (packet->packet_index >= packet->expected_packet_size) {
        process_mouse_packet();
        packet->packet_index = 0;
        packet->packet_ready = false;
        mouse_ctx.total_packets++;
    }
    
    return (io_operation_result_t){
        .status = IO_STATUS_COMPLETED,
        .error = IO_SUCCESS,
        .bytes_transferred = 1
    };
}

static io_operation_result_t mouse_initialize(void *device_data) {
    (void)device_data;
    mouse_write_command(MOUSE_CMD_ENABLE_AUX);
    
    mouse_write_command(MOUSE_CMD_TEST_AUX);
    uint8_t test_result = mouse_read_response();
    if (test_result != 0x00) {
        return (io_operation_result_t){
            .status = IO_STATUS_ERROR,
            .error = IO_ERROR_HARDWARE_ERROR,
            .bytes_transferred = 0
        };
    }
    
    mouse_send_command(MOUSE_DEV_RESET);
    uint8_t response = mouse_read_response();
    if (response != 0xFA) {
        return (io_operation_result_t){
            .status = IO_STATUS_ERROR,
            .error = IO_ERROR_HARDWARE_ERROR,
            .bytes_transferred = 0
        };
    }
    
    mouse_read_response();
    mouse_read_response();
    
    mouse_ctx.wheel_supported = mouse_detect_wheel_support();
    mouse_ctx.packet_state.expected_packet_size = mouse_ctx.wheel_supported ? 
        MOUSE_WHEEL_PACKET_SIZE : MOUSE_PACKET_SIZE;
    
    mouse_send_command(MOUSE_DEV_SET_RESOLUTION);
    mouse_write_data(mouse_ctx.config.resolution);
    if (mouse_read_response() != 0xFA) {
        return (io_operation_result_t){
            .status = IO_STATUS_ERROR,
            .error = IO_ERROR_HARDWARE_ERROR,
            .bytes_transferred = 0
        };
    }
    
    mouse_send_command(MOUSE_DEV_SET_SAMPLE_RATE);
    mouse_write_data(mouse_ctx.config.sample_rate);
    if (mouse_read_response() != 0xFA) {
        return (io_operation_result_t){
            .status = IO_STATUS_ERROR,
            .error = IO_ERROR_HARDWARE_ERROR,
            .bytes_transferred = 0
        };
    }
    
    mouse_send_command(MOUSE_DEV_ENABLE_STREAMING);
    if (mouse_read_response() != 0xFA) {
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

static void mouse_enable_interrupts(void *device_data, bool enable) {
    (void)device_data;
    if (enable) {
        mouse_write_command(MOUSE_CMD_ENABLE_AUX);
    } else {
        mouse_write_command(MOUSE_CMD_DISABLE_AUX);
    }
}

mouse_error_t mouse_interrupt_init(const mouse_config_t *config) {
    if (!config) {
        return MOUSE_ERROR_INVALID_PARAMS;
    }
    
    memset(&mouse_ctx, 0, sizeof(mouse_ctx));
    mouse_ctx.config = *config;
    mouse_ctx.device_type = MOUSE_TYPE_STANDARD;
    mouse_ctx.packet_state.expected_packet_size = MOUSE_PACKET_SIZE;
    
    io_device_descriptor_t device_desc = {
        .type = IO_DEVICE_INPUT,
        .name = "PS/2 Mouse",
        .interrupt_vector = MOUSE_VECTOR,
        .base_address = MOUSE_DATA_PORT,
        .memory_size = 0,
        .capabilities = {
            .supports_read = true,
            .supports_write = false,
            .supports_async = true,
            .supports_dma = false,
            .supports_scatter_gather = false,
            .supports_priority = false,
            .max_transfer_size = 4,
            .alignment_requirement = 1
        },
        .operations = {
            .initialize = mouse_initialize,
            .shutdown = NULL,
            .read = NULL,
            .write = NULL,
            .control = NULL,
            .interrupt_handler = mouse_interrupt_handler,
            .enable_interrupts = mouse_enable_interrupts,
            .cancel = NULL
        },
        .device_data = NULL
    };
    
    if (io_register_device(&device_desc, &mouse_ctx.device_handle) != IO_SUCCESS) {
        return MOUSE_ERROR_DEVICE_REGISTRATION_FAILED;
    }
    
    mouse_ctx.initialized = true;
    return MOUSE_SUCCESS;
}

mouse_error_t mouse_register_callback(mouse_event_callback_t callback, 
                                    void *user_data,
                                    const mouse_event_filter_t *filter) {
    if (!mouse_ctx.initialized || !callback) {
        return MOUSE_ERROR_INVALID_PARAMS;
    }
    
    if (mouse_ctx.callback_count >= MOUSE_MAX_CALLBACKS) {
        return MOUSE_ERROR_NO_SPACE;
    }
    
    mouse_callback_entry_t *entry = &mouse_ctx.callbacks[mouse_ctx.callback_count];
    entry->callback = callback;
    entry->user_data = user_data;
    entry->enabled = true;
    
    if (filter) {
        entry->filter = *filter;
    } else {
        entry->filter.filter_enabled = false;
    }
    
    mouse_ctx.callback_count++;
    return MOUSE_SUCCESS;
}

mouse_error_t mouse_get_event(mouse_event_t *event) {
    if (!mouse_ctx.initialized || !event) {
        return MOUSE_ERROR_INVALID_PARAMS;
    }
    
    if (!mouse_buffer_pop_event(event)) {
        return MOUSE_ERROR_NO_DATA;
    }
    
    return MOUSE_SUCCESS;
}

mouse_error_t mouse_get_state(mouse_state_t *state) {
    if (!mouse_ctx.initialized || !state) {
        return MOUSE_ERROR_INVALID_PARAMS;
    }
    
    *state = mouse_ctx.current_state;
    return MOUSE_SUCCESS;
}

mouse_error_t mouse_set_position(int32_t x, int32_t y) {
    if (!mouse_ctx.initialized) {
        return MOUSE_ERROR_NOT_INITIALIZED;
    }
    
    if (mouse_ctx.config.enable_bounds_checking) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= (int32_t)mouse_ctx.config.screen_width) {
            x = (int32_t)mouse_ctx.config.screen_width - 1;
        }
        if (y >= (int32_t)mouse_ctx.config.screen_height) {
            y = (int32_t)mouse_ctx.config.screen_height - 1;
        }
    }
    
    mouse_ctx.current_state.position.x = x;
    mouse_ctx.current_state.position.y = y;
    
    return MOUSE_SUCCESS;
}

mouse_error_t mouse_get_statistics(mouse_statistics_t *stats) {
    if (!mouse_ctx.initialized || !stats) {
        return MOUSE_ERROR_INVALID_PARAMS;
    }
    
    *stats = (mouse_statistics_t){
        .total_interrupts = mouse_ctx.total_interrupts,
        .total_packets = mouse_ctx.total_packets,
        .total_events = mouse_ctx.total_events,
        .dropped_packets = mouse_ctx.dropped_packets,
        .invalid_packets = mouse_ctx.invalid_packets,
        .buffer_events_pending = mouse_ctx.event_buffer.count,
        .callbacks_registered = mouse_ctx.callback_count,
        .wheel_supported = mouse_ctx.wheel_supported,
        .device_type = mouse_ctx.device_type
    };
    
    return MOUSE_SUCCESS;
}

void mouse_process_events(void) {
    if (!mouse_ctx.initialized) {
        return;
    }
    
    io_process_completions();
}

bool mouse_is_button_pressed(mouse_button_t button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT:
            return mouse_ctx.current_state.buttons.left;
        case MOUSE_BUTTON_RIGHT:
            return mouse_ctx.current_state.buttons.right;
        case MOUSE_BUTTON_MIDDLE:
            return mouse_ctx.current_state.buttons.middle;
        case MOUSE_BUTTON_4:
            return mouse_ctx.current_state.buttons.button4;
        case MOUSE_BUTTON_5:
            return mouse_ctx.current_state.buttons.button5;
        default:
            return false;
    }
}

bool mouse_is_initialized(void) {
    return mouse_ctx.initialized;
}

bool mouse_has_wheel_support(void) {
    return mouse_ctx.wheel_supported;
}

mouse_device_type_t mouse_get_device_type(void) {
    return mouse_ctx.device_type;
}

size_t mouse_get_pending_events(void) {
    return mouse_ctx.event_buffer.count;
}
