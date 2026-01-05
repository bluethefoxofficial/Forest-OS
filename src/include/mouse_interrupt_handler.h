#ifndef MOUSE_INTERRUPT_HANDLER_H
#define MOUSE_INTERRUPT_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MOUSE_SUCCESS = 0,
    MOUSE_ERROR_INVALID_PARAMS,
    MOUSE_ERROR_NOT_INITIALIZED,
    MOUSE_ERROR_NO_DATA,
    MOUSE_ERROR_NO_SPACE,
    MOUSE_ERROR_DEVICE_REGISTRATION_FAILED,
    MOUSE_ERROR_HARDWARE_ERROR,
    MOUSE_ERROR_TIMEOUT
} mouse_error_t;

typedef enum {
    MOUSE_TYPE_STANDARD = 0,
    MOUSE_TYPE_INTELLIMOUSE = 1,
    MOUSE_TYPE_INTELLIMOUSE_EXPLORER = 2
} mouse_device_type_t;

typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT = 1,
    MOUSE_BUTTON_MIDDLE = 2,
    MOUSE_BUTTON_4 = 3,
    MOUSE_BUTTON_5 = 4
} mouse_button_t;

typedef enum {
    MOUSE_EVENT_MOVEMENT = 0x01,
    MOUSE_EVENT_BUTTON = 0x02,
    MOUSE_EVENT_WHEEL = 0x04
} mouse_event_type_t;

typedef struct {
    int32_t x;
    int32_t y;
} mouse_position_t;

typedef struct {
    int16_t delta_x;
    int16_t delta_y;
} mouse_movement_t;

typedef struct {
    bool left;
    bool right;
    bool middle;
    bool button4;
    bool button5;
} mouse_buttons_t;

typedef struct {
    int8_t delta_vertical;
    int8_t delta_horizontal;
} mouse_wheel_t;

typedef struct {
    mouse_position_t position;
    mouse_buttons_t buttons;
} mouse_state_t;

typedef struct {
    mouse_event_type_t event_type;
    mouse_position_t position;
    mouse_movement_t movement;
    mouse_buttons_t buttons;
    mouse_wheel_t wheel;
    uint64_t timestamp;
} mouse_event_t;

typedef struct {
    bool enable_bounds_checking;
    bool enable_acceleration;
    bool enable_wheel_support;
    bool enable_statistics;
    uint32_t screen_width;
    uint32_t screen_height;
    uint8_t resolution;
    uint8_t sample_rate;
    uint32_t buffer_size;
} mouse_config_t;

typedef struct {
    bool filter_enabled;
    bool movement_only;
    bool button_only;
    bool wheel_only;
    bool specific_button;
    mouse_button_t button_filter;
} mouse_event_filter_t;

typedef struct {
    uint64_t total_interrupts;
    uint64_t total_packets;
    uint64_t total_events;
    uint64_t dropped_packets;
    uint64_t invalid_packets;
    uint32_t buffer_events_pending;
    uint32_t callbacks_registered;
    bool wheel_supported;
    mouse_device_type_t device_type;
} mouse_statistics_t;

typedef void (*mouse_event_callback_t)(const mouse_event_t *event, void *user_data);

mouse_error_t mouse_interrupt_init(const mouse_config_t *config);

mouse_error_t mouse_register_callback(mouse_event_callback_t callback, 
                                    void *user_data,
                                    const mouse_event_filter_t *filter);

mouse_error_t mouse_get_event(mouse_event_t *event);

mouse_error_t mouse_get_state(mouse_state_t *state);

mouse_error_t mouse_set_position(int32_t x, int32_t y);

mouse_error_t mouse_get_statistics(mouse_statistics_t *stats);

void mouse_process_events(void);

bool mouse_is_button_pressed(mouse_button_t button);

bool mouse_is_initialized(void);

bool mouse_has_wheel_support(void);

mouse_device_type_t mouse_get_device_type(void);

size_t mouse_get_pending_events(void);

static inline const char* mouse_error_to_string(mouse_error_t error) {
    switch (error) {
        case MOUSE_SUCCESS:
            return "Success";
        case MOUSE_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case MOUSE_ERROR_NOT_INITIALIZED:
            return "Mouse not initialized";
        case MOUSE_ERROR_NO_DATA:
            return "No mouse data available";
        case MOUSE_ERROR_NO_SPACE:
            return "No space for additional callbacks";
        case MOUSE_ERROR_DEVICE_REGISTRATION_FAILED:
            return "Failed to register mouse device";
        case MOUSE_ERROR_HARDWARE_ERROR:
            return "Mouse hardware error";
        case MOUSE_ERROR_TIMEOUT:
            return "Mouse operation timeout";
        default:
            return "Unknown mouse error";
    }
}

static inline const char* mouse_device_type_to_string(mouse_device_type_t type) {
    switch (type) {
        case MOUSE_TYPE_STANDARD:
            return "Standard PS/2 Mouse";
        case MOUSE_TYPE_INTELLIMOUSE:
            return "Microsoft IntelliMouse";
        case MOUSE_TYPE_INTELLIMOUSE_EXPLORER:
            return "Microsoft IntelliMouse Explorer";
        default:
            return "Unknown Mouse Type";
    }
}

static inline const char* mouse_button_to_string(mouse_button_t button) {
    switch (button) {
        case MOUSE_BUTTON_LEFT:
            return "Left Button";
        case MOUSE_BUTTON_RIGHT:
            return "Right Button";
        case MOUSE_BUTTON_MIDDLE:
            return "Middle Button";
        case MOUSE_BUTTON_4:
            return "Button 4";
        case MOUSE_BUTTON_5:
            return "Button 5";
        default:
            return "Unknown Button";
    }
}

static inline mouse_config_t mouse_default_config(void) {
    return (mouse_config_t){
        .enable_bounds_checking = true,
        .enable_acceleration = false,
        .enable_wheel_support = true,
        .enable_statistics = true,
        .screen_width = 1024,
        .screen_height = 768,
        .resolution = 2,  // 4 counts/mm
        .sample_rate = 100,  // 100 samples/second
        .buffer_size = 256
    };
}

static inline mouse_config_t mouse_gaming_config(void) {
    return (mouse_config_t){
        .enable_bounds_checking = true,
        .enable_acceleration = true,
        .enable_wheel_support = true,
        .enable_statistics = false,
        .screen_width = 1920,
        .screen_height = 1080,
        .resolution = 3,  // 8 counts/mm
        .sample_rate = 200,  // 200 samples/second
        .buffer_size = 512
    };
}

static inline mouse_config_t mouse_precision_config(void) {
    return (mouse_config_t){
        .enable_bounds_checking = false,
        .enable_acceleration = false,
        .enable_wheel_support = true,
        .enable_statistics = true,
        .screen_width = 3840,
        .screen_height = 2160,
        .resolution = 3,  // 8 counts/mm
        .sample_rate = 200,
        .buffer_size = 128
    };
}

static inline mouse_event_filter_t create_movement_filter(void) {
    return (mouse_event_filter_t){
        .filter_enabled = true,
        .movement_only = true,
        .button_only = false,
        .wheel_only = false,
        .specific_button = false,
        .button_filter = MOUSE_BUTTON_LEFT
    };
}

static inline mouse_event_filter_t create_button_filter(mouse_button_t button) {
    return (mouse_event_filter_t){
        .filter_enabled = true,
        .movement_only = false,
        .button_only = true,
        .wheel_only = false,
        .specific_button = true,
        .button_filter = button
    };
}

static inline mouse_event_filter_t create_wheel_filter(void) {
    return (mouse_event_filter_t){
        .filter_enabled = true,
        .movement_only = false,
        .button_only = false,
        .wheel_only = true,
        .specific_button = false,
        .button_filter = MOUSE_BUTTON_LEFT
    };
}

static inline mouse_position_t mouse_position(int32_t x, int32_t y) {
    return (mouse_position_t){ .x = x, .y = y };
}

static inline mouse_movement_t mouse_movement(int16_t delta_x, int16_t delta_y) {
    return (mouse_movement_t){ .delta_x = delta_x, .delta_y = delta_y };
}

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t value);
extern uint64_t rdtsc(void);

#endif // MOUSE_INTERRUPT_HANDLER_H