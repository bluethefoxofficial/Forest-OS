#include "debug.h"
#include "debuglog.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

static debug_level_t current_level = DEBUG_LEVEL_INFO;
static bool debug_initialized = false;

int debug_init(void) {
    debug_initialized = true;
    return 0;
}

void debug_set_level(debug_level_t level) {
    current_level = level;
}

debug_level_t debug_get_level(void) {
    return current_level;
}

void debug_print(const char *format, ...) {
    if (!debug_initialized || current_level < DEBUG_LEVEL_DEBUG) return;
    
    va_list args;
    va_start(args, format);
    debuglog(DEBUG_INFO, format, args);
    va_end(args);
}

void debug_error(const char *format, ...) {
    if (!debug_initialized || current_level < DEBUG_LEVEL_ERROR) return;
    
    va_list args;
    va_start(args, format);
    debuglog(DEBUG_ERROR, format, args);
    va_end(args);
}

void debug_warn(const char *format, ...) {
    if (!debug_initialized || current_level < DEBUG_LEVEL_WARN) return;
    
    va_list args;
    va_start(args, format);
    debuglog(DEBUG_WARN, format, args);
    va_end(args);
}

void debug_info(const char *format, ...) {
    if (!debug_initialized || current_level < DEBUG_LEVEL_INFO) return;
    
    va_list args;
    va_start(args, format);
    debuglog(DEBUG_INFO, format, args);
    va_end(args);
}

void debug_verbose(const char *format, ...) {
    if (!debug_initialized || current_level < DEBUG_LEVEL_VERBOSE) return;

    va_list args;
    va_start(args, format);
    debuglog(DEBUG_DETAIL, format, args);
    va_end(args);
}

void printhex(unsigned int value) {
    debug_print("0x%x", value);
}

void printdec(unsigned int value) {
    debug_print("%u", value);
}

void debug_print_hex(unsigned int value) {
    debug_print("0x%x", value);
}

void debug_print_dec(unsigned int value) {
    debug_print("%u", value);
}
