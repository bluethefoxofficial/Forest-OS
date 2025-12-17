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

#endif
