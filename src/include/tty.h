#ifndef TTY_H
#define TTY_H

#include <stdbool.h>
#include <stdint.h>

// Framebuffer-based teletype interface that uses the graphics subsystem for all
// text rendering. The implementation understands common ANSI escape sequences
// (SGR colors/styles, cursor movement, clears, save/restore cursor) and renders
// text directly to the framebuffer with full truecolor support.

// Initialize the TTY subsystem. This requires the graphics subsystem to be
// initialized first and will set up framebuffer-based text rendering.
// Returns true on success, false on failure.
bool tty_init(void);

// Clear the entire screen and reset the cursor to the top-left corner using
// the current attribute settings.
void tty_clear(void);

// Write a single character to the TTY, interpreting control characters and
// ANSI sequences.
void tty_putc(char c);

// Write a string to the TTY. ANSI sequences embedded in the string are
// interpreted to update colors, cursor position, and screen clearing.
void tty_write_ansi(const char* text);

// Convenience wrapper for strings; ANSI content is also honored here so that
// callers don't need to choose between the two entry points.
void tty_write(const char* text);

// Update the current text attribute (foreground/background pair encoded using
// the existing text attribute nibble layout).
void tty_set_attr(uint8_t attr);
uint8_t tty_get_attr(void);

// Report whether the TTY is currently using the graphics subsystem for text
// output. Always returns true for framebuffer-only TTY.
bool tty_uses_graphics_backend(void);

// Attempt to enable the graphics backend. Always returns true if graphics
// subsystem is initialized, since framebuffer TTY requires graphics.
bool tty_try_enable_graphics_backend(void);

#endif // TTY_H
