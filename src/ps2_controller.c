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

int ps2_controller_init(void) {
    if (controller_initialized) {
        return 0;
    }

    uint8 config_byte;
    bool mouse_port_available = true;
    
    print("[PS/2] Initializing PS/2 controller...\n");
    
    // Disable both ports
    if (!ps2_controller_send_command(PS2_CMD_DISABLE_KEYBOARD_PORT)) {
        print("[PS/2] Failed to disable keyboard port\n");
        return -1;
    }
    
    if (!ps2_controller_send_command(PS2_CMD_DISABLE_MOUSE_PORT)) {
        print("[PS/2] Failed to disable mouse port\n");
        return -1;
    }
    
    // Flush output buffer multiple times to ensure clean state
    for (int flush_count = 0; flush_count < 10; flush_count++) {
        if (!(inportb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL)) {
            break;
        }
        inportb(PS2_DATA_PORT);
        // Small delay to let controller settle
        for (volatile int delay = 0; delay < 1000; delay++);
    }
    
    // Read configuration byte
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
    
    // Modify configuration byte: disable translation, interrupts during init
    config_byte &= ~(PS2_CONFIG_KEYBOARD_INTERRUPT | PS2_CONFIG_MOUSE_INTERRUPT | PS2_CONFIG_KEYBOARD_TRANSLATE);
    config_byte |= (PS2_CONFIG_KEYBOARD_DISABLE | PS2_CONFIG_MOUSE_DISABLE);
    
    // Write configuration byte
    if (!ps2_controller_write_config(config_byte)) {
        print("[PS/2] Failed to write config byte\n");
        return -1;
    }
    
    // Controller self-test
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
        print("[PS/2] Self-test failed: 0x");
        print_hex8(self_test_result);
        print("\n");
        return -1;
    }
    
    print("[PS/2] Self-test passed\n");
    
    // Test keyboard port - but don't fail completely if it doesn't pass
    // Some emulators and systems without keyboards will fail this test
    // but still accept keyboard input fine
    bool keyboard_port_ok = false;
    if (!ps2_controller_send_command(PS2_CMD_TEST_KEYBOARD_PORT)) {
        print("[PS/2] Warning: Failed to send keyboard port test command\n");
    } else if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Warning: Timeout waiting for keyboard port test result\n");
    } else {
        uint8 kbd_test_result = ps2_controller_read_data();
        if (kbd_test_result != 0x00) {
            print("[PS/2] Warning: Keyboard port test returned: 0x");
            print_hex8(kbd_test_result);
            print(" (continuing anyway)\n");
        } else {
            print("[PS/2] Keyboard port test passed\n");
            keyboard_port_ok = true;
        }
    }
    (void)keyboard_port_ok;  // We continue regardless
    
    // Enable keyboard port
    if (!ps2_controller_send_command(PS2_CMD_ENABLE_KEYBOARD_PORT)) {
        print("[PS/2] Failed to enable keyboard port\n");
        return -1;
    }

    // Test mouse port
    if (!ps2_controller_send_command(PS2_CMD_TEST_MOUSE_PORT)) {
        print("[PS/2] Failed to send mouse port test command\n");
        mouse_port_available = false;
    } else if (!ps2_controller_wait_output_ready()) {
        print("[PS/2] Timeout waiting for mouse port test result\n");
        mouse_port_available = false;
    } else {
        uint8 mouse_test_result = ps2_controller_read_data();
        if (mouse_test_result != 0x00) {
            print("[PS/2] Mouse port test failed: 0x");
            print_hex8(mouse_test_result);
            print("\n");
            mouse_port_available = false;
        } else {
            print("[PS/2] Mouse port test passed\n");
        }
    }

    // Enable mouse port if available
    if (mouse_port_available) {
        if (!ps2_controller_send_command(PS2_CMD_ENABLE_MOUSE_PORT)) {
            print("[PS/2] Failed to enable mouse port\n");
            mouse_port_available = false;
        }
    }

    // Re-read and enable interrupts
    if (!ps2_controller_read_config(&config_byte)) {
        print("[PS/2] Failed to read config byte for interrupt enable\n");
        return -1;
    }

    config_byte &= ~(PS2_CONFIG_KEYBOARD_DISABLE | PS2_CONFIG_MOUSE_DISABLE);
    config_byte |= PS2_CONFIG_KEYBOARD_INTERRUPT;
    if (mouse_port_available) {
        config_byte |= PS2_CONFIG_MOUSE_INTERRUPT;
    }

    if (!ps2_controller_write_config(config_byte)) {
        print("[PS/2] Failed to apply interrupt config\n");
        return -1;
    }

    print("[PS/2] Final config byte: 0x");
    print_hex8(config_byte);
    print("\n");
    
    // Update status
    controller_status.keyboard_enabled = true;
    controller_status.mouse_enabled = mouse_port_available;
    controller_status.keyboard_interrupt_enabled = true;
    controller_status.mouse_interrupt_enabled = mouse_port_available;
    // Translation is now disabled as part of our initialization
    controller_translation_enabled = false;
    controller_status.translation_enabled = false;
    controller_initialized = true;
    
    print("[PS/2] Controller initialization complete\n");
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

bool ps2_send_keyboard_command(uint8 command) {
    int retries = 3;
    
    while (retries-- > 0) {
        if (!ps2_controller_send_data(command)) {
            continue;
        }
        
        if (!ps2_controller_wait_output_ready()) {
            continue;
        }
        
        uint8 response = ps2_controller_read_data();
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
        
        if (!ps2_controller_wait_output_ready()) {
            continue;
        }
        
        uint8 response = ps2_controller_read_data();
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
        config |= PS2_CONFIG_KEYBOARD_INTERRUPT;
        config &= ~PS2_CONFIG_KEYBOARD_DISABLE;
        ps2_controller_write_config(config);
    } else {
        // If we can't read config, try writing a reasonable default
        // Enable keyboard interrupt, disable mouse, no translation
        ps2_controller_send_command(PS2_CMD_WRITE_CONFIG_BYTE);
        ps2_controller_send_data(PS2_CONFIG_KEYBOARD_INTERRUPT | PS2_CONFIG_SYSTEM_FLAG);
    }

    controller_status.keyboard_enabled = true;
    controller_status.keyboard_interrupt_enabled = true;
    controller_initialized = true;

    print("[PS/2] Minimal keyboard initialization complete\n");
}
