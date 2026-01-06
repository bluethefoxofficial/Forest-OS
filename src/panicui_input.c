/*
 * Forest OS PanicUI Input - Stub Implementation
 *
 * The TTY-based panic screen handles input directly in panicui.c.
 * These functions are stubs for compatibility.
 */

#include "include/panicui_input.h"
#include "include/graphics/graphics_manager.h"

void panicui_init_input(void) {
    // No-op for TTY implementation - input handled directly in panicui.c
}

void panicui_process_input_event(const input_event_t* event) {
    (void)event;
}
