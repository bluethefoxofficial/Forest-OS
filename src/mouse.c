#include "include/interrupt.h"
#include "include/ps2_mouse.h"
#include "include/ps2_controller.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/util.h"

#define PS2_MOUSE_CMD_RESET                 0xFF
#define PS2_MOUSE_CMD_SET_DEFAULTS          0xF6
#define PS2_MOUSE_CMD_ENABLE_DATA_REPORTING 0xF4
#define PS2_MOUSE_CMD_DISABLE_DATA_REPORTING 0xF5
#define PS2_MOUSE_CMD_SET_SAMPLE_RATE       0xF3
#define PS2_MOUSE_CMD_GET_DEVICE_ID         0xF2

#define PS2_MOUSE_SYNC_BIT                  0x08

static ps2_mouse_state_t mouse_state;
static ps2_mouse_event_callback_t mouse_callback = NULL;
static uint8 mouse_packet[4];
static uint8 mouse_packet_index = 0;
static uint8 mouse_device_id = 0;

static bool ps2_mouse_send_command(uint8 command);
static bool ps2_mouse_reset(void);
static void ps2_mouse_process_packet(void);

int ps2_mouse_init(void) {
    print("[MOUSE] Initializing PS/2 mouse driver...\n");

    if (ps2_controller_init() != 0) {
        print("[MOUSE] PS/2 controller not ready\n");
        return -1;
    }

    // Enable mouse port (controller_init already tried, but ensure it stays enabled)
    if (!ps2_controller_send_command(PS2_CMD_ENABLE_MOUSE_PORT)) {
        print("[MOUSE] Failed to enable mouse port\n");
        return -1;
    }

    // Enable mouse interrupts and clear translation
    uint8 config;
    if (ps2_controller_read_config(&config)) {
        config &= ~(PS2_CONFIG_KEYBOARD_TRANSLATE | PS2_CONFIG_MOUSE_DISABLE);
        config |= PS2_CONFIG_MOUSE_INTERRUPT;
        ps2_controller_write_config(config);
    }

    if (!ps2_mouse_reset()) {
        print("[MOUSE] Mouse reset failed\n");
        return -1;
    }

    // Restore defaults
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_SET_DEFAULTS)) {
        print("[MOUSE] Failed to apply defaults\n");
        return -1;
    }

    // Enable data reporting
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_ENABLE_DATA_REPORTING)) {
        print("[MOUSE] Failed to enable data reporting\n");
        return -1;
    }

    mouse_state.x = 0;
    mouse_state.y = 0;
    mouse_state.left_button = false;
    mouse_state.right_button = false;
    mouse_state.middle_button = false;
    mouse_packet_index = 0;

    print("[MOUSE] PS/2 mouse driver initialized (ID: 0x");
    char hex_chars[] = "0123456789ABCDEF";
    char id_str[3];
    id_str[0] = hex_chars[(mouse_device_id >> 4) & 0xF];
    id_str[1] = hex_chars[mouse_device_id & 0xF];
    id_str[2] = '\0';
    print(id_str);
    print(")\n");

    return 0;
}

void ps2_mouse_irq_handler(struct interrupt_frame* frame, uint32 error_code) {
    (void)frame;
    (void)error_code;

    if (ps2_mouse_data_available()) {
        uint8 data = ps2_controller_read_data();

        // Synchronize packets: first byte always has bit 3 set
        if (mouse_packet_index == 0 && !(data & PS2_MOUSE_SYNC_BIT)) {
            // Still need to acknowledge IRQ even if packet discarded below
        } else {
            mouse_packet[mouse_packet_index++] = data;

            // Standard 3-byte packet
            if (mouse_packet_index == 3) {
                ps2_mouse_process_packet();
                mouse_packet_index = 0;
            }
        }
    }

    pic_send_eoi(12);
}

static void ps2_mouse_process_packet(void) {
    int dx = (int8)mouse_packet[1];
    int dy = (int8)mouse_packet[2];

    mouse_state.left_button = (mouse_packet[0] & 0x01) != 0;
    mouse_state.right_button = (mouse_packet[0] & 0x02) != 0;
    mouse_state.middle_button = (mouse_packet[0] & 0x04) != 0;
    mouse_state.x_overflow = (mouse_packet[0] & 0x40) != 0;
    mouse_state.y_overflow = (mouse_packet[0] & 0x80) != 0;

    // PS/2 Y movement is negative when moving up, invert for screen-friendly coords
    mouse_state.x += dx;
    mouse_state.y -= dy;

    ps2_mouse_event_t event;
    event.dx = dx;
    event.dy = dy;
    event.x = mouse_state.x;
    event.y = mouse_state.y;
    event.left_button = mouse_state.left_button;
    event.right_button = mouse_state.right_button;
    event.middle_button = mouse_state.middle_button;
    event.x_overflow = mouse_state.x_overflow;
    event.y_overflow = mouse_state.y_overflow;

    if (mouse_callback) {
        mouse_callback(&event);
    }
}

static bool ps2_mouse_send_command(uint8 command) {
    int retries = 3;

    while (retries-- > 0) {
        if (!ps2_controller_send_mouse_command(command)) {
            continue;
        }

        if (!ps2_controller_wait_output_ready()) {
            continue;
        }

        uint8 response = ps2_controller_read_data();
        if (response == PS2_RESPONSE_ACK) {
            return true;
        } else if (response == PS2_RESPONSE_RESEND) {
            continue;
        } else {
            return false;
        }
    }

    return false;
}

static bool ps2_mouse_reset(void) {
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_RESET)) {
        return false;
    }

    // Self-test result
    if (!ps2_controller_wait_output_ready()) {
        return false;
    }
    uint8 self_test = ps2_controller_read_data();
    if (self_test != PS2_RESPONSE_KEYBOARD_SELF_TEST_PASSED) {
        return false;
    }

    // Device ID
    if (!ps2_controller_wait_output_ready()) {
        return false;
    }
    mouse_device_id = ps2_controller_read_data();

    return true;
}

void ps2_mouse_register_event_callback(ps2_mouse_event_callback_t callback) {
    mouse_callback = callback;
}

ps2_mouse_state_t ps2_mouse_get_state(void) {
    return mouse_state;
}

bool ps2_mouse_disable_reporting(void) {
    return ps2_mouse_send_command(PS2_MOUSE_CMD_DISABLE_DATA_REPORTING);
}
