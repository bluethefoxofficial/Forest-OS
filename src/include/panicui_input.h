#ifndef PANICUI_INPUT_H
#define PANICUI_INPUT_H

#include "graphics/graphics_manager.h"

void panicui_init_input(void);
void panicui_process_input_event(const input_event_t* event);

#endif // PANICUI_INPUT_H