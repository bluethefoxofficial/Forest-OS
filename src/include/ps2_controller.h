#ifndef PS2_CONTROLLER_H
#define PS2_CONTROLLER_H

#include "types.h"

// PS/2 Controller ports
#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

// PS/2 Controller status register bits
#define PS2_STATUS_OUTPUT_BUFFER_FULL  0x01
#define PS2_STATUS_INPUT_BUFFER_FULL   0x02
#define PS2_STATUS_SYSTEM_FLAG         0x04
#define PS2_STATUS_COMMAND_DATA_FLAG   0x08
#define PS2_STATUS_KEYBOARD_LOCK       0x10
#define PS2_STATUS_AUX_OUTPUT_BUFFER   0x20
#define PS2_STATUS_TIMEOUT_ERROR       0x40
#define PS2_STATUS_PARITY_ERROR        0x80

// PS/2 Controller commands
#define PS2_CMD_READ_CONFIG_BYTE       0x20
#define PS2_CMD_WRITE_CONFIG_BYTE      0x60
#define PS2_CMD_DISABLE_MOUSE_PORT     0xA7
#define PS2_CMD_ENABLE_MOUSE_PORT      0xA8
#define PS2_CMD_TEST_MOUSE_PORT        0xA9
#define PS2_CMD_SELF_TEST              0xAA
#define PS2_CMD_TEST_KEYBOARD_PORT     0xAB
#define PS2_CMD_DISABLE_KEYBOARD_PORT  0xAD
#define PS2_CMD_ENABLE_KEYBOARD_PORT   0xAE
#define PS2_CMD_WRITE_TO_MOUSE         0xD4

// PS/2 Controller configuration byte bits
#define PS2_CONFIG_KEYBOARD_INTERRUPT  0x01
#define PS2_CONFIG_MOUSE_INTERRUPT     0x02
#define PS2_CONFIG_SYSTEM_FLAG         0x04
#define PS2_CONFIG_KEYBOARD_DISABLE    0x10
#define PS2_CONFIG_MOUSE_DISABLE       0x20
#define PS2_CONFIG_KEYBOARD_TRANSLATE  0x40

// PS/2 response codes
#define PS2_RESPONSE_ACK                        0xFA
#define PS2_RESPONSE_RESEND                     0xFE
#define PS2_RESPONSE_CONTROLLER_SELF_TEST_PASSED 0x55
#define PS2_RESPONSE_KEYBOARD_SELF_TEST_PASSED   0xAA
#define PS2_RESPONSE_ECHO                       0xEE
#define PS2_RESPONSE_ERROR1                     0xFC
#define PS2_RESPONSE_ERROR2                     0xFD

// Function prototypes
typedef struct {
    bool keyboard_enabled;
    bool mouse_enabled;
    bool keyboard_interrupt_enabled;
    bool mouse_interrupt_enabled;
    bool translation_enabled;
} ps2_controller_status_t;

// Initialize PS/2 controller
int ps2_controller_init(void);

// Send command to PS/2 controller
bool ps2_controller_send_command(uint8 command);

// Send data to PS/2 controller
bool ps2_controller_send_data(uint8 data);

// Send command to mouse (prefixed with 0xD4)
bool ps2_controller_send_mouse_command(uint8 command);

// Read data from PS/2 controller
uint8 ps2_controller_read_data(void);

// Read/write configuration byte
bool ps2_controller_read_config(uint8* config_byte);
bool ps2_controller_write_config(uint8 config_byte);

// Wait for input buffer to be clear
bool ps2_controller_wait_input_clear(void);

// Wait for output buffer to have data
bool ps2_controller_wait_output_ready(void);

// Send command to keyboard
bool ps2_send_keyboard_command(uint8 command);

// Send data to keyboard
bool ps2_send_keyboard_data(uint8 data);

// Get controller status
ps2_controller_status_t ps2_get_controller_status(void);
bool ps2_controller_is_translation_enabled(void);

// Enable/disable keyboard interrupts
void ps2_enable_keyboard_interrupts(void);
void ps2_disable_keyboard_interrupts(void);

// Check if data is available
bool ps2_data_available(void);

// Check if keyboard-specific data is available
bool ps2_keyboard_data_available(void);

// Check if mouse-specific data is available  
bool ps2_mouse_data_available(void);

#endif
