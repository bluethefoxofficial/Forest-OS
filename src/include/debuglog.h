#ifndef DEBUGLOG_H
#define DEBUGLOG_H

#include "types.h"
#include <stdbool.h>

void debuglog_init(void);
bool debuglog_is_ready(void);
void debuglog_write_char(char c);
void debuglog_write(const char* text);
void debuglog_write_hex(uint32 value);
void debuglog_write_dec(uint32 value);

typedef enum {
    DEBUG_INFO = 0,
    DEBUG_WARN,
    DEBUG_ERROR,
    DEBUG_FATAL
} debug_log_level_t;

void debuglog(debug_log_level_t level, const char* format, ...) __attribute__((format(printf, 2, 3)));

#endif
