#include "include/debuglog.h"
#include "include/io_ports.h"
#include "include/types.h"
#include "include/util.h"

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
