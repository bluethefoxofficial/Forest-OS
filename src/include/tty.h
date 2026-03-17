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
// Note: Returns false during boot mode to allow fast VGA text output.
bool tty_is_ready(void);

// Exit boot mode and switch to framebuffer TTY for graphics rendering.
// Call this after early boot is complete (e.g., before starting desktop).
void tty_exit_boot_mode(void);

// Check if TTY is still in boot mode (using fast VGA text output).
bool tty_in_boot_mode(void);

// Graphics app mode - when a graphical application (like CanopyDM) owns the
// display, TTY output to the framebuffer is suppressed.
void tty_set_graphics_app_active(bool active);
bool tty_is_graphics_app_active(void);

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

// TTY status bar management
void tty_draw_status_bar(void);
void tty_clear_status_bar(void);
void tty_set_status_bar_visible(bool visible);
bool tty_is_status_bar_visible(void);

// Force a full redraw of the current TTY screen.
void tty_force_redraw(void);

// Virtual terminal management
// VT 1-2: Graphical (handled by display manager)
// VT 3-12: TTY terminals with separate buffers
#define TTY_VT_COUNT 12
#define TTY_FIRST_TTY_VT 3  // First TTY virtual terminal (F3)
#define TTY_LAST_TTY_VT 12 // Last TTY virtual terminal (F12)

// Switch to a different virtual terminal (1-12)
// Returns true on success, false if VT is invalid or not a TTY VT
bool tty_switch_vt(uint8_t vt_number);

// Get current virtual terminal number (1-12)
uint8_t tty_get_current_vt(void);

// Check if current VT is a TTY (not graphical)
bool tty_is_active(void);

// Initialize all virtual terminal buffers
bool tty_init_vt_buffers(void);

// Crash screen for unhandled exceptions (direct framebuffer access)
void tty_show_crash_screen(const char* title, const char* message, uint64_t eip, uint64_t error_code, uint64_t cr2);

// Unix TTY device support
// Virtual terminal constants
#define MAX_VIRTUAL_TTYS 64
#define TTY_BUFFER_SIZE 4096

typedef struct terminal_size {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixel;
    uint16_t ypixel;
} winsize_t;

typedef struct termios {
    uint32_t c_iflag;    /* input flags */
    uint32_t c_oflag;    /* output flags */
    uint32_t c_cflag;    /* control flags */
    uint32_t c_lflag;    /* local flags */
    uint8_t  c_cc[19];   /* control characters */
} termios_t;

typedef struct virtual_tty {
    uint32_t tty_number;
    bool active;
    
    /* Input buffer */
    uint8_t input_buffer[TTY_BUFFER_SIZE];
    uint32_t input_head;
    uint32_t input_tail;
    
    /* Output buffer */
    uint8_t output_buffer[TTY_BUFFER_SIZE];
    uint32_t output_head;
    uint32_t output_tail;
    
    /* Terminal attributes */
    uint8_t current_attr;
    termios_t termios;
    winsize_t winsize;
} virtual_tty_t;

/* Global virtual terminal array - declared in tty_devices.c */
extern virtual_tty_t g_virtual_ttys[MAX_VIRTUAL_TTYS];

// TTY ioctl commands
#define TIOCGETA    0x5401  /* Get termios structure */
#define TIOCSETA    0x5402  /* Set termios structure (drain) */
#define TIOCSETAW   0x5403  /* Set termios structure (wait) */
#define TIOCSETAF   0x5404  /* Set termios structure (flush) */
#define TIOCGWINSZ  0x5413  /* Get window size */
#define TIOCSWINSZ  0x5414  /* Set window size */
#define TIOCOUTQ    0x5411  /* Get output queue size */
#define TIOCINQ     0x5412  /* Get input queue size */

// Termios flags - input flags
#define IGNBRK      0x00000001  /* Ignore break condition */
#define BRKINT      0x00000002  /* Signal interrupt on break */
#define IGNPAR      0x00000004  /* Ignore characters with parity errors */
#define PARMRK      0x00000008  /* Mark parity and framing errors */
#define INPCK       0x00000010  /* Enable input parity check */
#define ISTRIP      0x00000020  /* Strip 8th bit off characters */
#define INLCR       0x00000040  /* Translate NL to CR on input */
#define IGNCR       0x00000080  /* Ignore CR on input */
#define ICRNL       0x00000100  /* Translate CR to NL on input */
#define IXON        0x00000200  /* Enable XON/XOFF flow control */
#define IXOFF       0x00000400  /* Enable XON/XOFF input flow control */
#define IXANY       0x00000800  /* Allow any character to restart output */
#define IMAXBEL     0x00002000  /* Ring bell on input queue full */

// Termios flags - output flags
#define OPOST       0x00000001  /* Enable output processing */
#define ONLCR       0x00000002  /* Translate NL to CR-NL on output */
#define OCRNL       0x00000004  /* Translate CR to NL on output */
#define ONOCR       0x00000008  /* No CR output at column 0 */
#define ONLRET      0x00000010  /* NL performs CR function */
#define OFILL       0x00000020  /* Use fill characters for delay */
#define OFDEL       0x00000040  /* Fill with delete characters */

// Termios flags - control flags
#define CIGNORE     0x00000001  /* Ignore control flags */
#define CSIZE       0x00000030  /* Character size mask */
#define   CS5       0x00000000  /* 5 bits */
#define   CS6       0x00000010  /* 6 bits */
#define   CS7       0x00000020  /* 7 bits */
#define   CS8       0x00000030  /* 8 bits */
#define CSTOPB      0x00000040  /* Send two stop bits */
#define CREAD       0x00000080  /* Enable receiver */
#define PARENB      0x00000100  /* Enable parity generation and checking */
#define PARODD      0x00000200  /* Use odd parity */
#define HUPCL       0x00000400  /* Hang up on last close */
#define CLOCAL      0x00000800  /* Ignore modem status lines */

// Termios flags - local flags
#define ISIG        0x00000080  /* Enable signals */
#define ICANON      0x00000002  /* Canonical input mode */
#define ECHO        0x00000008  /* Echo input characters */
#define ECHOE       0x00000010  /* Echo erase character as backspace */
#define ECHOK       0x00000020  /* Echo kill character */
#define ECHONL      0x00000040  /* Echo newline */
#define NOFLSH      0x00000100  /* Disable flush on signal */
#define TOSTOP      0x00000200  /* Send SIGTTOU for background writes */
#define IEXTEN      0x00000400  /* Enable extended input processing */

// Control characters
#define VEOF        0  /* End of file */
#define VEOL        1  /* End of line */
#define VERASE      2  /* Erase character */
#define VKILL       3  /* Kill line */
#define VINTR       4  /* Interrupt character */
#define VQUIT       5  /* Quit character */
#define VSUSP       10 /* Suspend character */
#define VSTART      12 /* Start character */
#define VSTOP       13 /* Stop character */
#define VMIN        16 /* Minimum number of characters to read */
#define VTIME       17 /* Time to wait for input (tenths of a second) */
#define VSWTC       7  /* Switch terminal characters */
#define VREPRINT    8  /* Reprint unread characters */
#define VDISCARD    9  /* Discard pending output */
#define VWERASE     11 /* Word erase character */
#define VLNEXT      14 /* Literal next character */
#define VEOF2       18 /* Alternate end-of-file character */

// Initialize TTY devices
int tty_devices_init(void);
void tty_devices_cleanup(void);

// Handle keyboard events for TTY
void tty_handle_keyboard_event(uint8_t keycode, bool pressed);

#endif // TTY_H