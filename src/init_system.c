#include "include/init_system.h"

#include "include/debuglog.h"
#include "include/graphics_init.h"
#include "include/graphics/graphics_manager.h"
#include "include/tty.h"

static struct {
    init_display_config_t display;
    bool display_applied;
    bool using_graphics;
} init_state = {
    .display = {
        .graphics_requested = false,
        .width = 800,
        .height = 600,
        .bpp = 32,
    },
    .display_applied = false,
    .using_graphics = false,
};

void init_system_init(void) {
    init_state.display.graphics_requested = true;  // Always request graphics for framebuffer TTY
    init_state.display.width = 800;
    init_state.display.height = 600;
    init_state.display.bpp = 32;
    init_state.display_applied = false;
    init_state.using_graphics = false;
}

void init_system_request_graphics(uint32_t width, uint32_t height, uint32_t bpp) {
    init_state.display.graphics_requested = true;
    if (width) init_state.display.width = width;
    if (height) init_state.display.height = height;
    if (bpp) init_state.display.bpp = bpp;
}

bool init_system_graphics_requested(void) {
    return init_state.display.graphics_requested;
}

bool init_system_apply_display(void) {
    if (init_state.display_applied) {
        return init_state.using_graphics;
    }

    init_state.display_applied = true;

    if (!init_state.display.graphics_requested) {
        debuglog(DEBUG_INFO, "Init: graphics not requested; staying in text mode\n");
        return false;
    }

    graphics_result_t gres = initialize_graphics_subsystem();
    if (gres != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "Init: graphics init failed (%d); using legacy TTY\n", gres);
        init_state.using_graphics = false;
        return false;
    }

    // Try the requested resolution, otherwise allow the TTY helper to fall back
    // to a safe graphics mode.
    if (init_state.display.width && init_state.display.height && init_state.display.bpp) {
        graphics_set_mode(init_state.display.width, init_state.display.height,
                          init_state.display.bpp, 60);
    }

    if (!tty_try_enable_graphics_backend()) {
        debuglog(DEBUG_WARN, "Init: graphics backend unavailable; using legacy TTY\n");
        init_state.using_graphics = false;
        return false;
    }

    // Show a small banner so virtual machines don't present a blank screen when
    // graphics mode is active.
    graphics_clear_screen(COLOR_BLACK);
    tty_clear();
    tty_write_ansi("\x1b[36m[INIT]\x1b[0m Graphics console activated via init system\n");
    tty_write_ansi("\x1b[90m[hint]\x1b[0m Defaulting to TTY unless explicitly requested.\n\n");

    init_state.using_graphics = true;
    return true;
}
