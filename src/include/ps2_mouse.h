#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include "types.h"
#include "interrupt.h"

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

typedef void (*ps2_mouse_event_callback_t)(ps2_mouse_event_t* event);

int ps2_mouse_init(void);
void ps2_mouse_irq_handler(struct interrupt_frame* frame, uint32 error_code);
void ps2_mouse_register_event_callback(ps2_mouse_event_callback_t callback);
ps2_mouse_state_t ps2_mouse_get_state(void);
bool ps2_mouse_disable_reporting(void);

#endif
