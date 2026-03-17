#include "include/ps2_controller.h"
#include "include/io_ports.h"
#include "include/screen.h"

#define PS2_TIMEOUT 1000000

// Helper function for hex printing
static void print_hex8(uint8 value) {
    char hex_chars[] = "0123456789ABCDEF";
    char hex_str[3];
    hex_str[0] = hex_chars[(value >> 4) & 0xF];
    hex_str[1] = hex_chars[value & 0xF];
    hex_str[2] = '\0';
    print(hex_str);
}

static ps2_controller_status_t controller_status;
static bool controller_initialized = false;
static bool controller_translation_enabled = true;
static bool mouse_port_available = false;

int ps2_controller_init(void) {
    if (controller_initialized) {
        return 0;
    }

    // Simple debug message to verify this function is called
    print("PS2_INIT_START\n");
    print("[PS/2] Starting comprehensive PS/2 controller initialization...\n");

    uint8 config_byte;
    bool dual_channel = false;

    // Step 1: Disable devices (already done in kernel.c)
    // Step 2: Flush output buffer (already done in kernel.c)

    // Step 3: Set Controller Configuration Byte
    // Read current configuration byte
    if (!ps2_controller_send_command(PS2_CMD_READ_CONFIG_BYTE)) {
        print("[PS/2] Failed to read config byte\n");
        return -1;
    }

    if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for config byte\n");
        return -1;
    }

    config_byte = ps2_controller_read_data();
    controller_translation_enabled = (config_byte & PS2_CONFIG_KEYBOARD_TRANSLATE) != 0;
    print("[PS/2] Current config byte: 0x");
    print_hex8(config_byte);
    print("\n");

     // Disable interrupts and translation during initialization
     config_byte &= ~(PS2_CONFIG_KEYBOARD_INTERRUPT | PS2_CONFIG_MOUSE_INTERRUPT);
     config_byte |= PS2_CONFIG_KEYBOARD_TRANSLATE;  // Enable translation for compatibility

    if (!ps2_controller_write_config(config_byte)) {
        print("[PS/2] Failed to write initial config byte\n");
        return -1;
    }

    // Step 4: Controller Self Test
    if (!ps2_controller_send_command(PS2_CMD_SELF_TEST)) {
        print("[PS/2] Failed to send self-test command\n");
        return -1;
    }

    if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for self-test result\n");
        return -1;
    }

    uint8 self_test_result = ps2_controller_read_data();
    if (self_test_result != PS2_RESPONSE_CONTROLLER_SELF_TEST_PASSED) {
        print("[PS/2] Self-test returned 0x");
        print_hex8(self_test_result);
        print(" (expected 0x55) - continuing anyway as many systems work despite this\n");
        // Don't fail - many emulators/systems work fine even if self-test fails
    } else {
        print("[PS/2] Controller self-test passed\n");
    }

    // Step 5: Determine if there are 2 channels
    // Re-enable second port and check if it affects config byte
    if (!ps2_controller_send_command(PS2_CMD_ENABLE_MOUSE_PORT)) {
        print("[PS/2] Warning: Could not enable mouse port for dual-channel test\n");
    }

    // Read config byte again
    if (!ps2_controller_send_command(PS2_CMD_READ_CONFIG_BYTE)) {
        print("[PS/2] Failed to re-read config byte for dual-channel test\n");
        return -1;
    }

    if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for config byte in dual-channel test\n");
        return -1;
    }

    uint8 config_after_mouse_enable = ps2_controller_read_data();

    // Check if bit 5 (mouse clock) is clear, indicating dual channel controller
    if ((config_after_mouse_enable & PS2_CONFIG_MOUSE_DISABLE) == 0) {
        dual_channel = true;
        print("[PS/2] Dual-channel controller detected\n");
    } else {
        print("[PS/2] Single-channel controller detected\n");
    }

    // Disable mouse port again for now
    if (!ps2_controller_send_command(PS2_CMD_DISABLE_MOUSE_PORT)) {
        print("[PS/2] Warning: Could not disable mouse port after dual-channel test\n");
    }

    // Step 6: Perform Interface Tests
    print("[PS/2] Testing keyboard port...\n");
    if (!ps2_controller_send_command(PS2_CMD_TEST_KEYBOARD_PORT)) {
        print("[PS/2] Failed to send keyboard port test command\n");
        return -1;
    }

    if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for keyboard port test result\n");
        return -1;
    }

    uint8 keyboard_test_result = ps2_controller_read_data();
    bool keyboard_ok = (keyboard_test_result == 0x00);

    if (keyboard_ok) {
        print("[PS/2] Keyboard port test passed\n");
    } else {
        print("[PS/2] Keyboard port test failed: 0x");
        print_hex8(keyboard_test_result);
        print(" (continuing anyway)\n");
    }

    if (dual_channel) {
        print("[PS/2] Testing mouse port...\n");
        if (!ps2_controller_send_command(PS2_CMD_TEST_MOUSE_PORT)) {
            print("[PS/2] Failed to send mouse port test command\n");
            return -1;
        }

        if (!ps2_controller_wait_output_ready()) {
            print("[PS/2] Timeout waiting for mouse port test result\n");
            return -1;
        }

        uint8 mouse_test_result = ps2_controller_read_data();
        bool mouse_ok = (mouse_test_result == 0x00);

        if (mouse_ok) {
            print("[PS/2] Mouse port test passed\n");
        } else {
            print("[PS/2] Mouse port test failed: 0x");
            print_hex8(mouse_test_result);
            print(" (continuing anyway)\n");
        }
    }

    // Step 7: Enable Devices
    print("[PS/2] Enabling keyboard port...\n");
    if (!ps2_controller_send_command(PS2_CMD_ENABLE_KEYBOARD_PORT)) {
        print("[PS/2] Failed to enable keyboard port\n");
        return -1;
    }

    if (dual_channel) {
        print("[PS/2] Enabling mouse port...\n");
        if (!ps2_controller_send_command(PS2_CMD_ENABLE_MOUSE_PORT)) {
            print("[PS/2] Warning: Failed to enable mouse port\n");
        }
    }

    // Step 8: Restore Controller Configuration Byte with interrupts enabled
    if (!ps2_controller_send_command(PS2_CMD_READ_CONFIG_BYTE)) {
        print("[PS/2] Failed to read final config byte\n");
        return -1;
    }

    if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for final config byte\n");
        return -1;
    }

    config_byte = ps2_controller_read_data();

    // Enable interrupts for working ports
    config_byte |= PS2_CONFIG_KEYBOARD_INTERRUPT;
    if (dual_channel) {
        config_byte |= PS2_CONFIG_MOUSE_INTERRUPT;
    }

     // Keep translation enabled for compatibility
     config_byte |= PS2_CONFIG_KEYBOARD_TRANSLATE;

    if (!ps2_controller_write_config(config_byte)) {
        print("[PS/2] Failed to write final config byte\n");
        return -1;
    }

    print("[PS/2] Final config byte: 0x");
    print_hex8(config_byte);
    print("\n");

    controller_initialized = true;
    print("[PS/2] PS/2 controller initialization completed successfully\n");
    return 0;
}

bool ps2_controller_send_command(uint8 command) {
    if (!ps2_controller_wait_input_clear()) {
        return false;
    }
    outportb(PS2_COMMAND_PORT, command);
    return true;
}

bool ps2_controller_send_data(uint8 data) {
    if (!ps2_controller_wait_input_clear()) {
        return false;
    }
    outportb(PS2_DATA_PORT, data);
    return true;
}

bool ps2_controller_send_mouse_command(uint8 command) {
    if (!ps2_controller_wait_input_clear()) {
        return false;
    }
    outportb(PS2_COMMAND_PORT, PS2_CMD_WRITE_TO_MOUSE);
    if (!ps2_controller_wait_input_clear()) {
        return false;
    }
    outportb(PS2_DATA_PORT, command);
    return true;
}

uint8 ps2_controller_read_data(void) {
    return inportb(PS2_DATA_PORT);
}

bool ps2_controller_read_config(uint8* config_byte) {
    if (!config_byte) {
        return false;
    }
    if (!ps2_controller_send_command(PS2_CMD_READ_CONFIG_BYTE)) {
        return false;
    }
    if (!ps2_controller_wait_output_ready()) {
        return false;
    }
    *config_byte = ps2_controller_read_data();
    return true;
}

bool ps2_controller_write_config(uint8 config_byte) {
    if (!ps2_controller_send_command(PS2_CMD_WRITE_CONFIG_BYTE)) {
        return false;
    }
    return ps2_controller_send_data(config_byte);
}

bool ps2_controller_wait_input_clear(void) {
    uint32 timeout = PS2_TIMEOUT;
    while (timeout-- > 0) {
        if (!(inportb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_BUFFER_FULL)) {
            return true;
        }
    }
    return false;
}

bool ps2_controller_wait_output_ready(void) {
    uint32 timeout = PS2_TIMEOUT;
    while (timeout-- > 0) {
        if (inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) {
            return true;
        }
    }
    return false;
}

static bool ps2_wait_for_device_response(bool expect_aux_data, uint8* response) {
    uint32 timeout = PS2_TIMEOUT;

    while (timeout-- > 0) {
        uint8 status = inportb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUTPUT_BUFFER_FULL)) {
            continue;
        }

        uint8 data = inportb(PS2_DATA_PORT);
        bool is_aux_data = (status & PS2_STATUS_AUX_OUTPUT_BUFFER) != 0;

        if (is_aux_data == expect_aux_data) {
            if (response) {
                *response = data;
            }
            return true;
        }

        /* Drop bytes from the other PS/2 port while waiting for our response. */
    }

    return false;
}

bool ps2_send_keyboard_command(uint8 command) {
    int retries = 3;
    
    while (retries-- > 0) {
        if (!ps2_controller_send_data(command)) {
            continue;
        }
        
        uint8 response = 0;
        if (!ps2_wait_for_device_response(false, &response)) {
            continue;
        }

        if (response == PS2_RESPONSE_ACK) {
            return true;
        } else if (response == PS2_RESPONSE_RESEND) {
            continue;  // Retry
        } else {
            // Unexpected response
            return false;
        }
    }
    
    return false;
}

bool ps2_send_keyboard_data(uint8 data) {
    int retries = 3;
    
    while (retries-- > 0) {
        if (!ps2_controller_send_data(data)) {
            continue;
        }
        
        uint8 response = 0;
        if (!ps2_wait_for_device_response(false, &response)) {
            continue;
        }

        if (response == PS2_RESPONSE_ACK) {
            return true;
        } else if (response == PS2_RESPONSE_RESEND) {
            continue;  // Retry
        } else {
            // Unexpected response
            return false;
        }
    }
    
    return false;
}

ps2_controller_status_t ps2_get_controller_status(void) {
    return controller_status;
}

bool ps2_controller_is_translation_enabled(void) {
    return controller_translation_enabled;
}

void ps2_enable_keyboard_interrupts(void) {
    controller_status.keyboard_interrupt_enabled = true;
}

void ps2_disable_keyboard_interrupts(void) {
    controller_status.keyboard_interrupt_enabled = false;
}

bool ps2_data_available(void) {
    return (inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) != 0;
}

bool ps2_keyboard_data_available(void) {
    uint8 status = inportb(PS2_STATUS_PORT);
    return (status & PS2_STATUS_OUTPUT_BUFFER_FULL) && !(status & PS2_STATUS_AUX_OUTPUT_BUFFER);
}

bool ps2_mouse_data_available(void) {
    uint8 status = inportb(PS2_STATUS_PORT);
    return (status & PS2_STATUS_OUTPUT_BUFFER_FULL) && (status & PS2_STATUS_AUX_OUTPUT_BUFFER);
}

void ps2_controller_minimal_init(void) {
    // Minimal initialization for keyboard fallback
    // Just enable the keyboard port and interrupts without tests
    print("[PS/2] Performing minimal keyboard initialization...\n");

    // Flush output buffer
    for (int i = 0; i < 10; i++) {
        if (inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) {
            inportb(PS2_DATA_PORT);
        }
    }

    // Try to enable keyboard port
    ps2_controller_send_command(PS2_CMD_ENABLE_KEYBOARD_PORT);

    // Try to read and modify config to enable keyboard interrupt
    uint8 config = 0;
    if (ps2_controller_read_config(&config)) {
         config |= PS2_CONFIG_KEYBOARD_INTERRUPT | PS2_CONFIG_KEYBOARD_TRANSLATE;
         config &= ~PS2_CONFIG_KEYBOARD_DISABLE;
         ps2_controller_write_config(config);
     } else {
         // If we can't read config, try writing a reasonable default
         // Enable keyboard interrupt, disable mouse, enable translation
         ps2_controller_send_command(PS2_CMD_WRITE_CONFIG_BYTE);
         ps2_controller_send_data(PS2_CONFIG_KEYBOARD_INTERRUPT | PS2_CONFIG_SYSTEM_FLAG | PS2_CONFIG_KEYBOARD_TRANSLATE);
     }

    controller_status.keyboard_enabled = true;
    controller_status.keyboard_interrupt_enabled = true;
    controller_initialized = true;

    print("[PS/2] Minimal keyboard initialization complete\n");
}
