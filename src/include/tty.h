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

// Returns true once the framebuffer TTY has successfully initialized.
bool tty_is_ready(void);

// Query current TTY dimensions (columns/rows). Returns false if TTY is not
// initialized yet.
bool tty_get_dimensions(uint16_t* cols, uint16_t* rows);

// Return the pixel dimensions of a single TTY cell based on the active font.
// Falls back to 8x16 if metrics are unavailable.
bool tty_get_cell_metrics(uint16_t* char_width, uint16_t* char_height);

// Read the character and attribute stored at a given cell. Returns false if
// the coordinates are out of bounds or the TTY is not ready.
bool tty_get_cell(uint16_t x, uint16_t y, char* ch, uint8_t* attr);

// Redraw a rectangular region of the TTY from its backing cell buffer.
void tty_redraw_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

#endif // TTY_H
