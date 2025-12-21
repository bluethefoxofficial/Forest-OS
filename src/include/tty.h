#ifndef TTY_H
#define TTY_H

#include <stdbool.h>
#include <stdint.h>

// Simple teletype interface that can drive either the graphics text backend or
// the legacy VGA console. The implementation understands common ANSI escape
// sequences (SGR colors/styles, cursor movement, clears, save/restore cursor)
// so that higher level code can emit familiar terminal control strings without
// worrying about which display stack is currently active.

// Initialize the TTY subsystem. This will try to use the graphics manager's
// text mode when available and automatically fall back to the legacy console
// when graphics hardware is unavailable or not initialized.
void tty_init(void);

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
// output.
bool tty_uses_graphics_backend(void);

// Attempt to switch the TTY to the graphics backend, selecting a text mode
// when available or falling back to a common framebuffer resolution so that
// glyph rendering continues to function. Returns true when graphics output is
// active.
bool tty_try_enable_graphics_backend(void);

#endif // TTY_H
