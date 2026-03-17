/**
 * PS/2 Mouse Driver for Forest OS
 *
 * Supports standard 3-button mice and IntelliMouse (scroll wheel) protocol.
 * Follows the OSDev PS/2 mouse specification.
 */

#include "include/interrupt.h"
#include "include/ps2_mouse.h"
#include "include/ps2_controller.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/debuglog.h"
#include "include/input_event.h"
#include "include/input_mux.h"
#include "include/devfs.h"
#include "include/timer.h"
#include "include/ps2_keyboard.h"

#define PS2_MOUSE_TIMEOUT 1000000

// Enable verbose serial debug output for mouse (set to 0 for production)
#define MOUSE_DEBUG_SERIAL 0

// Serial port for debug output
#define SERIAL_COM1 0x3F8

// Serial output helpers for mouse debugging
static inline void mouse_serial_char(char c) {
    while ((inportb(SERIAL_COM1 + 5) & 0x20) == 0);
    outportb(SERIAL_COM1, c);
}

static void mouse_serial_str(const char* s) {
    while (*s) {
        mouse_serial_char(*s++);
    }
}

static void mouse_serial_hex8(uint8 val) {
    const char hex[] = "0123456789ABCDEF";
    mouse_serial_char(hex[(val >> 4) & 0xF]);
    mouse_serial_char(hex[val & 0xF]);
}

static void mouse_serial_hex32(uint32 val) {
    mouse_serial_hex8((val >> 24) & 0xFF);
    mouse_serial_hex8((val >> 16) & 0xFF);
    mouse_serial_hex8((val >> 8) & 0xFF);
    mouse_serial_hex8(val & 0xFF);
}

static void mouse_serial_int(int val) {
    char buf[16];
    int i = 0;
    if (val < 0) {
        mouse_serial_char('-');
        val = -val;
    }
    if (val == 0) {
        mouse_serial_char('0');
        return;
    }
    while (val > 0 && i < 15) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        mouse_serial_char(buf[--i]);
    }
}

// Mouse state
static ps2_mouse_state_t mouse_state;
static ps2_mouse_event_callback_t mouse_callback = NULL;

// Packet buffer (4 bytes for IntelliMouse, 3 for standard)
static uint8 mouse_packet[4];
static uint8 mouse_packet_index = 0;
static uint8 mouse_packet_size = 3;  // 3 for standard, 4 for IntelliMouse

// Device info
static uint8 mouse_device_id = 0;
static bool mouse_ready = false;
static bool mouse_has_scroll_wheel = false;

// Previous button states for detecting state changes
static bool prev_left_button = false;
static bool prev_right_button = false;
static bool prev_middle_button = false;

// Screen bounds for cursor clamping
static int32_t mouse_screen_width = 0;
static int32_t mouse_screen_height = 0;

// Debug counters for tracking mouse data flow
static volatile uint32 mouse_irq_count = 0;
static volatile uint32 mouse_bytes_received = 0;
static volatile uint32 mouse_packets_processed = 0;

// Forward declarations
static bool ps2_mouse_send_command(uint8 command);
static bool ps2_mouse_send_command_with_arg(uint8 command, uint8 arg);
static bool ps2_mouse_reset(void);
static void ps2_mouse_process_packet(void);
static void ps2_mouse_process_input_stream(void);
static bool ps2_mouse_try_enable_intellimouse(void);
static void ps2_mouse_flush_buffer(void);
static bool ps2_mouse_read_aux_response(uint8* out_byte);
static void ps2_mouse_push_byte(uint8 data);

// Helper for hex printing
static void mouse_print_hex8(uint8 value) {
    char hex_chars[] = "0123456789ABCDEF";
    char hex_str[3];
    hex_str[0] = hex_chars[(value >> 4) & 0xF];
    hex_str[1] = hex_chars[value & 0xF];
    hex_str[2] = '\0';
    print(hex_str);
}

/*
 * Helper to dispatch an input_event_t to devfs and input mux
 */
static void ps2_mouse_dispatch_input_event(const input_event_t* ev) {
    // Queue to device file (for /dev/mouse readers)
    if (devfs_is_initialized()) {
        devfs_mouse_queue_event(ev);
    }

    // Dispatch to input multiplexer (for canopy, etc.)
    if (input_mux_is_initialized()) {
        input_mux_dispatch_event(ev);
    }
}

/**
 * Initialize the PS/2 mouse driver
 */
int ps2_mouse_init(void) {
    print("[MOUSE] Initializing PS/2 mouse driver...\n");
#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("\n[MOUSE-INIT] Starting PS/2 mouse initialization\n");
    mouse_serial_str("[MOUSE-INIT] Current bounds: ");
    mouse_serial_int(mouse_screen_width);
    mouse_serial_str("x");
    mouse_serial_int(mouse_screen_height);
    mouse_serial_str("\n");
#endif

    // Ensure PS/2 controller is initialized
    if (ps2_controller_init() != 0) {
        print("[MOUSE] PS/2 controller not ready\n");
        return -1;
    }

    // Flush any stale data in the buffer
    ps2_mouse_flush_buffer();

    // Enable the auxiliary (mouse) port
    if (!ps2_controller_send_command(PS2_CMD_ENABLE_MOUSE_PORT)) {
        print("[MOUSE] Failed to enable mouse port\n");
        return -1;
    }

    // Configure controller: enable mouse interrupts, keep keyboard settings
    uint8 config;
    if (ps2_controller_read_config(&config)) {
        config &= ~PS2_CONFIG_MOUSE_DISABLE;  // Clear mouse disable bit
        config |= PS2_CONFIG_MOUSE_INTERRUPT; // Enable mouse IRQ (IRQ12)
        ps2_controller_write_config(config);
    }

    // Reset the mouse device
    if (!ps2_mouse_reset()) {
        print("[MOUSE] Mouse reset failed - no mouse connected?\n");
        return -1;
    }

    print("[MOUSE] Mouse detected, ID: 0x");
    mouse_print_hex8(mouse_device_id);
    print("\n");

    // Set default parameters FIRST (before IntelliMouse detection)
    // This ensures a clean state before we try special protocols
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_SET_DEFAULTS)) {
        print("[MOUSE] Warning: Failed to set defaults\n");
    }

    // Set sample rate to 100 samples/second (good balance of responsiveness)
    if (!ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_SAMPLE_RATE, 100)) {
        print("[MOUSE] Warning: Failed to set sample rate\n");
    }

    // Set resolution to 4 counts/mm (highest precision)
    if (!ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_RESOLUTION, 0x03)) {
        print("[MOUSE] Warning: Failed to set resolution\n");
    }

    // Try to enable IntelliMouse protocol for scroll wheel support
    // IMPORTANT: Do this AFTER SET_DEFAULTS to prevent it from being reset
    if (ps2_mouse_try_enable_intellimouse()) {
        print("[MOUSE] IntelliMouse scroll wheel enabled\n");
        mouse_has_scroll_wheel = true;
        mouse_packet_size = 4;
    } else {
        print("[MOUSE] Standard 3-button mouse mode\n");
        mouse_has_scroll_wheel = false;
        mouse_packet_size = 3;
    }

    // Explicitly select stream mode (some controllers power up in remote mode)
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_SET_STREAM_MODE)) {
        print("[MOUSE] Warning: Failed to set stream mode\n");
    }

    // NOTE: We do NOT enable data reporting here anymore.
    // Data reporting will be enabled by ps2_mouse_start_streaming()
    // AFTER the IRQ handler is installed. This prevents data loss
    // from packets arriving before the handler is ready.

    // Initialize state - preserve position if bounds were already set (e.g., by early graphics init)
    // Otherwise start at center of bounds or (0,0) if no bounds
    if (mouse_screen_width > 0 && mouse_screen_height > 0) {
        // Bounds were set before init - set position to center
        mouse_state.x = mouse_screen_width / 2;
        mouse_state.y = mouse_screen_height / 2;
        print("[MOUSE] Position initialized to center: ");
        mouse_print_hex8((uint8)(mouse_state.x >> 8));
        mouse_print_hex8((uint8)(mouse_state.x & 0xFF));
        print(",");
        mouse_print_hex8((uint8)(mouse_state.y >> 8));
        mouse_print_hex8((uint8)(mouse_state.y & 0xFF));
        print("\n");
    } else {
        mouse_state.x = 0;
        mouse_state.y = 0;
    }
    mouse_state.left_button = false;
    mouse_state.right_button = false;
    mouse_state.middle_button = false;
    mouse_state.x_overflow = false;
    mouse_state.y_overflow = false;
    mouse_packet_index = 0;

    print("[MOUSE] PS/2 mouse driver initialized successfully\n");
    mouse_ready = true;

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("[MOUSE-INIT] Init complete! ready=1 pkt_size=");
    mouse_serial_int(mouse_packet_size);
    mouse_serial_str(" pos=(");
    mouse_serial_int(mouse_state.x);
    mouse_serial_str(",");
    mouse_serial_int(mouse_state.y);
    mouse_serial_str(") bounds=(");
    mouse_serial_int(mouse_screen_width);
    mouse_serial_str(",");
    mouse_serial_int(mouse_screen_height);
    mouse_serial_str(")\n");
#endif

    return 0;
}

/**
 * Try to enable IntelliMouse protocol (scroll wheel support)
 * This is done by sending a magic sequence of sample rates: 200, 100, 80
 */
static bool ps2_mouse_try_enable_intellimouse(void) {
    // Magic sequence to enable IntelliMouse
    if (!ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_SAMPLE_RATE, 200)) return false;
    if (!ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_SAMPLE_RATE, 100)) return false;
    if (!ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_SAMPLE_RATE, 80)) return false;

    // Get device ID - should be 0x03 for IntelliMouse
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_GET_DEVICE_ID)) {
        return false;
    }

    uint8 new_id = 0;
    if (!ps2_mouse_read_aux_response(&new_id)) {
        return false;
    }

    if (new_id == 0x03) {
        mouse_device_id = new_id;
        return true;
    }

    return false;
}

/**
 * Flush any pending data from the mouse buffer
 */
static void ps2_mouse_flush_buffer(void) {
    for (int i = 0; i < 64; i++) {
        uint8 status = inportb(PS2_STATUS_PORT);
        if ((status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0) {
            break;
        }
        /* Drain both keyboard and mouse bytes to start clean */
        (void)ps2_controller_read_data();
    }
}

/**
 * Wait for a byte coming specifically from the mouse (AUX bit set).
 * Discards keyboard bytes that might arrive while we're waiting.
 */
static bool ps2_mouse_read_aux_response(uint8* out_byte) {
    uint32 timeout = PS2_MOUSE_TIMEOUT;

    while (timeout-- > 0) {
        uint8 status = inportb(PS2_STATUS_PORT);
        if (status & PS2_STATUS_OUTPUT_BUFFER_FULL) {
            uint8 data = ps2_controller_read_data();
            if (status & PS2_STATUS_AUX_OUTPUT_BUFFER) {
                if (out_byte) {
                    *out_byte = data;
                }
                return true;
            }
            /* Keyboard byte – ignore and continue waiting */
        }
    }

    return false;
}

/**
 * IRQ12 handler for mouse interrupts
 */
void ps2_mouse_irq_handler(struct interrupt_frame* frame, uint32 error_code) {
    (void)frame;
    (void)error_code;

    mouse_irq_count++;

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("[MOUSE-IRQ] #");
    mouse_serial_int(mouse_irq_count);
#endif

    if (!mouse_ready) {
#if MOUSE_DEBUG_SERIAL
        mouse_serial_str(" NOT_READY\n");
#endif
        // Drain any data even if not ready to prevent buffer overflow
        while (ps2_mouse_data_available()) {
            (void)ps2_controller_read_data();
        }
        pic_send_eoi(12);
        return;
    }

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
#if MOUSE_DEBUG_SERIAL
            mouse_serial_str(" MOUSE_DATA=0x");
            mouse_serial_hex8(data);
            mouse_serial_str(" pkt_idx=");
            mouse_serial_int(mouse_packet_index);
#endif
            ps2_mouse_handle_byte(data);
        } else {
            ps2_keyboard_process_scancode(data);
        }
    }

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("\n");
#endif

    // Send EOI (PIC)
    pic_send_eoi(12);
}

/**
 * Process a complete mouse packet
 */
static void ps2_mouse_process_packet(void) {
    mouse_packets_processed++;
    uint8 status = mouse_packet[0];

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("[MOUSE-PKT] #");
    mouse_serial_int(mouse_packets_processed);
    mouse_serial_str(" raw=[0x");
    mouse_serial_hex8(mouse_packet[0]);
    mouse_serial_str(",0x");
    mouse_serial_hex8(mouse_packet[1]);
    mouse_serial_str(",0x");
    mouse_serial_hex8(mouse_packet[2]);
    if (mouse_packet_size == 4) {
        mouse_serial_str(",0x");
        mouse_serial_hex8(mouse_packet[3]);
    }
    mouse_serial_str("]");
#endif

    // Check for overflow - if set, packet data is unreliable
    if ((status & 0xC0) != 0) {
#if MOUSE_DEBUG_SERIAL
        mouse_serial_str(" OVERFLOW_DISCARD\n");
#endif
        // X or Y overflow set, discard this packet
        return;
    }

    // Extract button states
    bool left = (status & 0x01) != 0;
    bool right = (status & 0x02) != 0;
    bool middle = (status & 0x04) != 0;

    // Extract movement deltas with sign extension
    int dx = mouse_packet[1];
    int dy = mouse_packet[2];

    // Apply sign bits from status byte
    if (status & 0x10) dx |= 0xFFFFFF00;  // Sign extend X
    if (status & 0x20) dy |= 0xFFFFFF00;  // Sign extend Y

    // PS/2 reports positive Y for upward motion
    // Invert for screen coordinates (Y increases downward)
    dy = -dy;

    // Extract scroll wheel delta (IntelliMouse only)
    int scroll = 0;
    if (mouse_has_scroll_wheel && mouse_packet_size == 4) {
        scroll = (int8)mouse_packet[3];  // Sign-extended 8-bit value
    }

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str(" dx=");
    mouse_serial_int(dx);
    mouse_serial_str(" dy=");
    mouse_serial_int(dy);
    mouse_serial_str(" btn=");
    mouse_serial_char(left ? 'L' : '-');
    mouse_serial_char(right ? 'R' : '-');
    mouse_serial_char(middle ? 'M' : '-');
#endif

    // Update absolute position
    int old_x = mouse_state.x;
    int old_y = mouse_state.y;
    mouse_state.x += dx;
    mouse_state.y += dy;

    // Clamp to screen bounds
    if (mouse_screen_width > 0 && mouse_screen_height > 0) {
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= mouse_screen_width) mouse_state.x = mouse_screen_width - 1;
        if (mouse_state.y >= mouse_screen_height) mouse_state.y = mouse_screen_height - 1;
    }

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str(" pos=(");
    mouse_serial_int(old_x);
    mouse_serial_str(",");
    mouse_serial_int(old_y);
    mouse_serial_str(")->(");
    mouse_serial_int(mouse_state.x);
    mouse_serial_str(",");
    mouse_serial_int(mouse_state.y);
    mouse_serial_str(") bounds=(");
    mouse_serial_int(mouse_screen_width);
    mouse_serial_str(",");
    mouse_serial_int(mouse_screen_height);
    mouse_serial_str(")\n");
#endif

    // Update button states
    mouse_state.left_button = left;
    mouse_state.right_button = right;
    mouse_state.middle_button = middle;
    mouse_state.x_overflow = false;
    mouse_state.y_overflow = false;

    // Build and dispatch legacy event
    ps2_mouse_event_t event;
    event.dx = dx;
    event.dy = dy;
    event.x = mouse_state.x;
    event.y = mouse_state.y;
    event.left_button = left;
    event.right_button = right;
    event.middle_button = middle;
    event.x_overflow = false;
    event.y_overflow = false;

    if (mouse_callback) {
        mouse_callback(&event);
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

    // Generate relative X movement event
    if (dx != 0) {
        input_ev.type = EV_REL;
        input_ev.code = REL_X;
        input_ev.value = dx;
        ps2_mouse_dispatch_input_event(&input_ev);
    }

    // Generate relative Y movement event
    if (dy != 0) {
        input_ev.type = EV_REL;
        input_ev.code = REL_Y;
        input_ev.value = dy;
        ps2_mouse_dispatch_input_event(&input_ev);
    }

    // Generate scroll wheel event
    if (scroll != 0) {
        input_ev.type = EV_REL;
        input_ev.code = REL_WHEEL;
        input_ev.value = scroll;
        ps2_mouse_dispatch_input_event(&input_ev);
    }

    // Generate button events on state change
    if (left != prev_left_button) {
        input_ev.type = EV_KEY;
        input_ev.code = BTN_LEFT;
        input_ev.value = left ? KEY_PRESS : KEY_RELEASE;
        ps2_mouse_dispatch_input_event(&input_ev);
        prev_left_button = left;
    }

    if (right != prev_right_button) {
        input_ev.type = EV_KEY;
        input_ev.code = BTN_RIGHT;
        input_ev.value = right ? KEY_PRESS : KEY_RELEASE;
        ps2_mouse_dispatch_input_event(&input_ev);
        prev_right_button = right;
    }

    if (middle != prev_middle_button) {
        input_ev.type = EV_KEY;
        input_ev.code = BTN_MIDDLE;
        input_ev.value = middle ? KEY_PRESS : KEY_RELEASE;
        ps2_mouse_dispatch_input_event(&input_ev);
        prev_middle_button = middle;
    }

    // Generate SYN_REPORT event to mark end of this input packet
    input_ev.type = EV_SYN;
    input_ev.code = SYN_REPORT;
    input_ev.value = 0;
    ps2_mouse_dispatch_input_event(&input_ev);
}

/**
 * Process incoming mouse data stream
 */
static void ps2_mouse_process_input_stream(void) {
    int bytes_processed = 0;
    const int max_bytes = 32;  // Prevent infinite loops

    while (bytes_processed < max_bytes) {
        uint8 status = inportb(PS2_STATUS_PORT);
        bool output_full = (status & PS2_STATUS_OUTPUT_BUFFER_FULL) != 0;
        bool is_mouse = (status & PS2_STATUS_AUX_OUTPUT_BUFFER) != 0;

        if (!output_full) {
            break;
        }

        // Only process mouse data (AUX bit set), never keyboard data
        // This prevents stealing keyboard scancodes and packet corruption
        if (!is_mouse) {
            break;
        }

        uint8 data = ps2_controller_read_data();
        bytes_processed++;
        mouse_bytes_received++;

        ps2_mouse_push_byte(data);
    }

    // Safety check: reset packet state if we processed too many bytes
    // This prevents getting stuck in a bad state
    if (mouse_packet_index > mouse_packet_size) {
        mouse_packet_index = 0;
    }
}

/**
 * Poll for mouse data (for non-interrupt driven operation)
 */
void ps2_mouse_poll(void) {
#if MOUSE_DEBUG_SERIAL
    static uint32 poll_count = 0;
    poll_count++;
    // Log every 500th poll to show we're being called
    if ((poll_count % 500) == 1) {
        uint8 status = inportb(PS2_STATUS_PORT);
        mouse_serial_str("[MOUSE-POLL] #");
        mouse_serial_int(poll_count);
        mouse_serial_str(" ready=");
        mouse_serial_int(mouse_ready ? 1 : 0);
        mouse_serial_str(" status=0x");
        mouse_serial_hex8(status);
        mouse_serial_str(" irq_cnt=");
        mouse_serial_int(mouse_irq_count);
        mouse_serial_str(" bytes=");
        mouse_serial_int(mouse_bytes_received);
        mouse_serial_str(" pkts=");
        mouse_serial_int(mouse_packets_processed);
        mouse_serial_str("\n");
    }
#endif

    if (!mouse_ready) {
        return;
    }

    bool interrupts_were_enabled = irq_save_and_disable_safe();
    ps2_mouse_process_input_stream();
    irq_restore_safe(interrupts_were_enabled);
}

/**
 * Send a command to the mouse device
 */
static bool ps2_mouse_send_command(uint8 command) {
    int retries = 3;

    while (retries-- > 0) {
        // Send command through the controller to the mouse
        if (!ps2_controller_send_mouse_command(command)) {
            continue;
        }

        // Wait for acknowledgment
        uint8 response = 0;
        if (!ps2_mouse_read_aux_response(&response)) {
            continue;
        }

        if (response == PS2_RESPONSE_ACK) {
            return true;
        } else if (response == PS2_RESPONSE_RESEND) {
            // Mouse wants us to resend
            continue;
        } else {
            // Unexpected response
            return false;
        }
    }

    return false;
}

/**
 * Send a command with a data argument to the mouse
 */
static bool ps2_mouse_send_command_with_arg(uint8 command, uint8 arg) {
    if (!ps2_mouse_send_command(command)) {
        return false;
    }
    return ps2_mouse_send_command(arg);
}

/**
 * Reset the mouse device and perform self-test
 */
static bool ps2_mouse_reset(void) {
    // Send reset command
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_RESET)) {
        return false;
    }

    // Wait for self-test result (0xAA = passed)
    uint8 self_test = 0;
    if (!ps2_mouse_read_aux_response(&self_test)) {
        return false;
    }

    if (self_test != 0xAA) {
        print("[MOUSE] Self-test failed: 0x");
        mouse_print_hex8(self_test);
        print("\n");
        return false;
    }

    // Read device ID (0x00 = standard mouse)
    if (!ps2_mouse_read_aux_response(&mouse_device_id)) {
        return false;
    }
    return true;
}

/**
 * Register a callback for mouse events
 */
void ps2_mouse_register_event_callback(ps2_mouse_event_callback_t callback) {
    mouse_callback = callback;
}

/**
 * Get the current mouse state
 */
ps2_mouse_state_t ps2_mouse_get_state(void) {
#if MOUSE_DEBUG_SERIAL
    static uint32 get_state_count = 0;
    get_state_count++;
    // Only log every 100th call to avoid flooding serial
    if ((get_state_count % 100) == 1) {
        mouse_serial_str("[MOUSE-GET] call #");
        mouse_serial_int(get_state_count);
        mouse_serial_str(" pos=(");
        mouse_serial_int(mouse_state.x);
        mouse_serial_str(",");
        mouse_serial_int(mouse_state.y);
        mouse_serial_str(") btn=");
        mouse_serial_char(mouse_state.left_button ? 'L' : '-');
        mouse_serial_char(mouse_state.right_button ? 'R' : '-');
        mouse_serial_char(mouse_state.middle_button ? 'M' : '-');
        mouse_serial_str(" irq=");
        mouse_serial_int(mouse_irq_count);
        mouse_serial_str(" pkts=");
        mouse_serial_int(mouse_packets_processed);
        mouse_serial_str("\n");
    }
#endif
    return mouse_state;
}

/**
 * Disable mouse data reporting
 */
bool ps2_mouse_disable_reporting(void) {
    if (!mouse_ready) return false;
    return ps2_mouse_send_command(PS2_MOUSE_CMD_DISABLE_DATA_REPORTING);
}

/**
 * Enable mouse data reporting
 */
bool ps2_mouse_enable_reporting(void) {
    if (!mouse_ready) return false;
    return ps2_mouse_send_command(PS2_MOUSE_CMD_ENABLE_DATA_REPORTING);
}

/**
 * Start mouse data streaming - call this AFTER installing IRQ handler
 * This function enables data reporting and flushes any stale data.
 * Unlike ps2_mouse_enable_reporting(), this works even before mouse_ready is set
 * and is designed to be called from kernel initialization.
 */
bool ps2_mouse_start_streaming(void) {
    // Flush any stale data that arrived during initialization
    ps2_mouse_flush_buffer();

    // Reset packet state to ensure clean start
    mouse_packet_index = 0;

    // Enable data reporting - mouse will now send packets on movement
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_ENABLE_DATA_REPORTING)) {
        print("[MOUSE] Failed to enable data reporting\n");
        return false;
    }

    print("[MOUSE] Data streaming enabled\n");

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("[MOUSE] Data streaming started, waiting for movement...\n");
#endif

    return true;
}

/**
 * Feed a raw data byte into the packet assembler (IRQ or polled path)
 */
static void ps2_mouse_push_byte(uint8 data) {
    // Packet synchronization: first byte must have bit 3 set
    if (mouse_packet_index == 0 && !(data & PS2_MOUSE_SYNC_BIT)) {
#if MOUSE_DEBUG_SERIAL
        mouse_serial_str("[MOUSE-SYNC] byte 0x");
        mouse_serial_hex8(data);
        mouse_serial_str(" REJECTED (no sync bit)\n");
#endif
        return;
    }

    mouse_packet[mouse_packet_index++] = data;

#if MOUSE_DEBUG_SERIAL
    mouse_serial_str("[MOUSE-PUSH] idx=");
    mouse_serial_int(mouse_packet_index - 1);
    mouse_serial_str(" data=0x");
    mouse_serial_hex8(data);
    mouse_serial_str(" need=");
    mouse_serial_int(mouse_packet_size);
    mouse_serial_str("\n");
#endif

    if (mouse_packet_index >= mouse_packet_size) {
        ps2_mouse_process_packet();
        mouse_packet_index = 0;
    }
}

void ps2_mouse_handle_byte(uint8 data) {
    if (!mouse_ready) {
        return;
    }

    mouse_bytes_received++;
    ps2_mouse_push_byte(data);
}

/**
 * Set the screen bounds for cursor clamping
 */
void ps2_mouse_set_bounds(int32_t width, int32_t height) {
    mouse_screen_width = width;
    mouse_screen_height = height;

    // Center the cursor in the new bounds
    mouse_state.x = width / 2;
    mouse_state.y = height / 2;
}

/**
 * Set the absolute cursor position
 */
void ps2_mouse_set_position(int32_t x, int32_t y) {
    mouse_state.x = x;
    mouse_state.y = y;

    // Clamp to bounds if set
    if (mouse_screen_width > 0 && mouse_screen_height > 0) {
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= mouse_screen_width) mouse_state.x = mouse_screen_width - 1;
        if (mouse_state.y >= mouse_screen_height) mouse_state.y = mouse_screen_height - 1;
    }
}

/**
 * Check if the mouse has a scroll wheel
 */
bool ps2_mouse_has_scroll_wheel(void) {
    return mouse_has_scroll_wheel;
}

/**
 * Check if the mouse driver is ready
 */
bool ps2_mouse_is_ready(void) {
    return mouse_ready;
}

/**
 * Set the mouse sample rate (samples per second)
 * Valid values: 10, 20, 40, 60, 80, 100, 200
 */
bool ps2_mouse_set_sample_rate(uint8 rate) {
    if (!mouse_ready) return false;
    return ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_SAMPLE_RATE, rate);
}

/**
 * Set the mouse resolution
 * 0 = 1 count/mm, 1 = 2 count/mm, 2 = 4 count/mm, 3 = 8 count/mm
 */
bool ps2_mouse_set_resolution(uint8 resolution) {
    if (!mouse_ready) return false;
    if (resolution > 3) resolution = 3;
    return ps2_mouse_send_command_with_arg(PS2_MOUSE_CMD_SET_RESOLUTION, resolution);
}

/**
 * Get debug statistics for mouse driver
 */
void ps2_mouse_get_debug_stats(uint32* irq_count, uint32* bytes_received, uint32* packets_processed) {
    if (irq_count) *irq_count = mouse_irq_count;
    if (bytes_received) *bytes_received = mouse_bytes_received;
    if (packets_processed) *packets_processed = mouse_packets_processed;
}

// Static flag to track if mouse is present
static bool g_mouse_present = false;

/**
 * Check if mouse device is present and responding
 * Uses get device ID command to test communication
 */
bool ps2_mouse_is_present(void) {
    // Send get device ID command
    if (!ps2_mouse_send_command(PS2_MOUSE_CMD_GET_DEVICE_ID)) {
        return false;
    }

    // Wait for response with timeout
    uint8 device_id = 0;
    if (!ps2_mouse_read_aux_response(&device_id)) {
        return false;
    }

    // Any valid device ID (0x00 for standard, 0x03 for IntelliMouse) means present
    if (device_id == 0x00 || device_id == 0x03 || device_id == 0x04) {
        g_mouse_present = true;
        return true;
    }

    g_mouse_present = false;
    return false;
}

/**
 * Reinitialize mouse after disconnect/reconnect
 * Returns 0 on success, negative on failure
 */
int ps2_mouse_reinit(void) {
    print("[MOUSE] Reinitializing mouse...\n");

    // Check if mouse is present
    if (!ps2_mouse_is_present()) {
        print("[MOUSE] No mouse detected\n");
        g_mouse_present = false;
        return -1;
    }

    // Perform full initialization
    int result = ps2_mouse_init();
    if (result == 0) {
        g_mouse_present = true;
        print("[MOUSE] Mouse reinitialized successfully\n");
    } else {
        g_mouse_present = false;
        print("[MOUSE] Mouse reinitialization failed\n");
    }

    return result;
}
