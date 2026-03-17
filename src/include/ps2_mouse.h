#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include "types.h"
#include "interrupt.h"

// PS/2 Mouse Commands
#define PS2_MOUSE_CMD_RESET                     0xFF
#define PS2_MOUSE_CMD_RESEND                    0xFE
#define PS2_MOUSE_CMD_SET_DEFAULTS              0xF6
#define PS2_MOUSE_CMD_DISABLE_DATA_REPORTING    0xF5
#define PS2_MOUSE_CMD_ENABLE_DATA_REPORTING     0xF4
#define PS2_MOUSE_CMD_SET_SAMPLE_RATE           0xF3
#define PS2_MOUSE_CMD_GET_DEVICE_ID             0xF2
#define PS2_MOUSE_CMD_SET_REMOTE_MODE           0xF0
#define PS2_MOUSE_CMD_SET_WRAP_MODE             0xEE
#define PS2_MOUSE_CMD_RESET_WRAP_MODE           0xEC
#define PS2_MOUSE_CMD_READ_DATA                 0xEB
#define PS2_MOUSE_CMD_SET_STREAM_MODE           0xEA
#define PS2_MOUSE_CMD_STATUS_REQUEST            0xE9
#define PS2_MOUSE_CMD_SET_RESOLUTION            0xE8
#define PS2_MOUSE_CMD_SET_SCALING_2_1           0xE7
#define PS2_MOUSE_CMD_SET_SCALING_1_1           0xE6

// PS/2 Mouse packet sync bit (bit 3 must be set in first byte)
#define PS2_MOUSE_SYNC_BIT                      0x08

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    bool left_button;
    bool right_button;
    bool middle_button;
    bool x_overflow;
    bool y_overflow;
} ps2_mouse_event_t;

typedef struct {
    int x;
    int y;
    bool left_button;
    bool right_button;
    bool middle_button;
    bool x_overflow;
    bool y_overflow;
} ps2_mouse_state_t;

typedef void (*ps2_mouse_event_callback_t)(const ps2_mouse_event_t* event);

// Initialization and IRQ handler
int ps2_mouse_init(void);
void ps2_mouse_irq_handler(struct interrupt_frame* frame, uint32 error_code);

// Callback registration
void ps2_mouse_register_event_callback(ps2_mouse_event_callback_t callback);

// State access
ps2_mouse_state_t ps2_mouse_get_state(void);
bool ps2_mouse_is_ready(void);
bool ps2_mouse_has_scroll_wheel(void);

// Data reporting control
bool ps2_mouse_disable_reporting(void);
bool ps2_mouse_enable_reporting(void);
bool ps2_mouse_start_streaming(void);  // Call AFTER IRQ handler is installed

// Polling (for non-interrupt operation)
void ps2_mouse_poll(void);
void ps2_mouse_handle_byte(uint8 data);

// Cursor bounds and position
void ps2_mouse_set_bounds(int32_t width, int32_t height);
void ps2_mouse_set_position(int32_t x, int32_t y);

// Configuration
bool ps2_mouse_set_sample_rate(uint8 rate);
bool ps2_mouse_set_resolution(uint8 resolution);

// Debug statistics
void ps2_mouse_get_debug_stats(uint32* irq_count, uint32* bytes_received, uint32* packets_processed);

// Device presence checking and hot reload
bool ps2_mouse_is_present(void);
int ps2_mouse_reinit(void);

#endif
