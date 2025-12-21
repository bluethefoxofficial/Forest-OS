#ifndef INIT_SYSTEM_H
#define INIT_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

// Minimal init system to coordinate early display policy. By default the kernel
// boots into the text TTY, but callers can request graphics and let the init
// layer negotiate the switch once the graphics stack is available.

typedef struct {
    bool graphics_requested;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} init_display_config_t;

void init_system_init(void);
void init_system_request_graphics(uint32_t width, uint32_t height, uint32_t bpp);
bool init_system_graphics_requested(void);

// Attempts to apply the current display policy. Returns true if the TTY is now
// running atop the graphics backend (either text mode or framebuffer text
// rendering), or false if the legacy console remains active.
bool init_system_apply_display(void);

#endif // INIT_SYSTEM_H
