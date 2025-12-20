#include "include/debuglog.h"
#include "include/io_ports.h"
#include "include/types.h"
#include "include/util.h"
#include "include/string.h"
#include <stdarg.h>

#define DEBUGLOG_SERIAL_PORT 0x3F8
#define DEBUGLOG_DEBUGCON_PORT 0xE9
#define DEBUGLOG_SERIAL_TIMEOUT 0xFFFF

static bool debuglog_initialized = false;

static inline void debuglog_serial_wait_tx(void) {
    uint32 timeout = DEBUGLOG_SERIAL_TIMEOUT;
    while (timeout--) {
        if (inportb(DEBUGLOG_SERIAL_PORT + 5) & 0x20) {
            break;
        }
    }
}

static void debuglog_serial_write_char(char c) {
    debuglog_serial_wait_tx();
    outportb(DEBUGLOG_SERIAL_PORT, (uint8)c);
}

static void debuglog_serial_init(void) {
    outportb(DEBUGLOG_SERIAL_PORT + 1, 0x00);      // Disable interrupts
    outportb(DEBUGLOG_SERIAL_PORT + 3, 0x80);      // Enable DLAB
    outportb(DEBUGLOG_SERIAL_PORT + 0, 0x03);      // 38400 baud (divisor = 3)
    outportb(DEBUGLOG_SERIAL_PORT + 1, 0x00);
    outportb(DEBUGLOG_SERIAL_PORT + 3, 0x03);      // 8 bits, no parity, one stop bit
    outportb(DEBUGLOG_SERIAL_PORT + 2, 0xC7);      // Enable FIFO, clear them
    outportb(DEBUGLOG_SERIAL_PORT + 4, 0x0B);      // IRQs enabled, RTS/DSR set
}

void debuglog_init(void) {
    if (debuglog_initialized) {
        return;
    }

    debuglog_serial_init();
    debuglog_initialized = true;
}

bool debuglog_is_ready(void) {
    return debuglog_initialized;
}

void debuglog_write_char(char c) {
    if (!debuglog_initialized) {
        return;
    }

    if (c == '\n') {
        debuglog_write_char('\r');
    }

    debuglog_serial_write_char(c);
    outportb(DEBUGLOG_DEBUGCON_PORT, (uint8)c);
}

void debuglog_write(const char* text) {
    if (!debuglog_initialized || !text) {
        return;
    }

    while (*text) {
        debuglog_write_char(*text++);
    }
}

void debuglog_write_hex(uint32 value) {
    char buffer[11];
    char* ptr = buffer;
    static const char hex_chars[] = "0123456789ABCDEF";

    *ptr++ = '0';
    *ptr++ = 'x';
    for (int i = 28; i >= 0; i -= 4) {
        *ptr++ = hex_chars[(value >> i) & 0xF];
    }
    *ptr = '\0';
    debuglog_write(buffer);
}

void debuglog_write_dec(uint32 value) {
    char buffer[16];
    int pos = 0;

    if (value == 0) {
        debuglog_write("0");
        return;
    }

    while (value > 0 && pos < (int)(sizeof(buffer) - 1)) {
        buffer[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (pos > 0) {
        debuglog_write_char(buffer[--pos]);
    }
}

static void debuglog_write_uint_base(uint32 value, uint32 base, bool uppercase, bool prefix) {
    char buffer[34];
    static const char* hex_lower = "0123456789abcdef";
    static const char* hex_upper = "0123456789ABCDEF";
    const char* digits = uppercase ? hex_upper : hex_lower;

    int pos = sizeof(buffer) - 1;
    buffer[pos--] = '\0';
    if (value == 0) {
        buffer[pos--] = '0';
    } else {
        while (value > 0 && pos >= 0) {
            buffer[pos--] = digits[value % base];
            value /= base;
        }
    }

    if (prefix && base == 16) {
        buffer[pos--] = 'x';
        buffer[pos--] = '0';
    }

    debuglog_write(&buffer[pos + 1]);
}

static void debuglog_vformat(const char* format, va_list args) {
    while (format && *format) {
        if (*format != '%') {
            debuglog_write_char(*format++);
            continue;
        }

        format++;
        switch (*format) {
            case '%':
                debuglog_write_char('%');
                break;
            case 's': {
                const char* str = va_arg(args, const char*);
                debuglog_write(str ? str : "(null)");
                break;
            }
            case 'c': {
                int ch = va_arg(args, int);
                debuglog_write_char((char)ch);
                break;
            }
            case 'd':
            case 'i': {
                int32 value = va_arg(args, int32);
                if (value < 0) {
                    debuglog_write_char('-');
                    value = -value;
                }
                debuglog_write_dec((uint32)value);
                break;
            }
            case 'u': {
                uint32 value = va_arg(args, uint32);
                debuglog_write_dec(value);
                break;
            }
            case 'x':
            case 'X': {
                uint32 value = va_arg(args, uint32);
                bool uppercase = (*format == 'X');
                debuglog_write_uint_base(value, 16, uppercase, false);
                break;
            }
            case 'p': {
                uintptr_t value = (uintptr_t)va_arg(args, void*);
                debuglog_write_uint_base((uint32)value, 16, false, true);
                break;
            }
            default:
                debuglog_write_char('%');
                debuglog_write_char(*format);
                break;
        }
        format++;
    }
}

void debuglog(debug_log_level_t level, const char* format, ...) {
    if (!debuglog_initialized || !format) {
        return;
    }

    static const char* level_prefix[] = {
        "[INFO] ",
        "[WARN] ",
        "[ERROR]",
        "[FATAL]"
    };

    int index = (int)level;
    if (index < 0 || index >= (int)(sizeof(level_prefix) / sizeof(level_prefix[0]))) {
        index = 0;
    }

    debuglog_write(level_prefix[index]);
    va_list args;
    va_start(args, format);
    debuglog_vformat(format, args);
    va_end(args);
}
