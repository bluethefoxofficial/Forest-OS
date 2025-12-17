#ifndef KB_H
#define KB_H
#include "screen.h"
#include "util.h"
#include "keyboard_layout.h"

typedef enum {
    KEYBOARD_DRIVER_LEGACY = 0,
    KEYBOARD_DRIVER_PS2,
} keyboard_driver_mode_t;

void keyboard_set_driver_mode(keyboard_driver_mode_t mode);
keyboard_driver_mode_t keyboard_get_driver_mode(void);
bool keyboard_poll_char(char* out_char);
string readStr();

#endif
