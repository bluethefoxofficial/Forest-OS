#ifndef KEYBOARD_LAYOUT_H
#define KEYBOARD_LAYOUT_H

#include "types.h"
#include "ps2_keyboard.h"

#define KEYBOARD_MAX_SEQUENCE_LENGTH 8

typedef struct {
    char normal[KEY_MAX];
    char shifted[KEY_MAX];
} keyboard_layout_t;

const keyboard_layout_t* keyboard_layout_get(keyboard_layout_id_t layout);
const keyboard_layout_t* keyboard_layout_get_default(void);
uint8 keyboard_layout_emit_chars(const keyboard_layout_t* layout,
                                 key_code_t key_code,
                                 bool shift,
                                 bool caps_lock,
                                 char* out_buffer,
                                 uint8 buffer_size);
char keyboard_layout_lookup_char(const keyboard_layout_t* layout,
                                 key_code_t key_code,
                                 bool shift,
                                 bool caps_lock);
key_code_t keyboard_scancode_set1_lookup(uint8 scancode, bool extended);

#endif
