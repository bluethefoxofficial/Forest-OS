#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "debuglog.h"

// Debug levels
typedef enum {
    DEBUG_LEVEL_NONE = 0,
    DEBUG_LEVEL_ERROR,
    DEBUG_LEVEL_WARN,
    DEBUG_LEVEL_INFO,
    DEBUG_LEVEL_DEBUG,
    DEBUG_LEVEL_VERBOSE
} debug_level_t;

// Debug subsystem initialization
int debug_init(void);
void debug_set_level(debug_level_t level);
debug_level_t debug_get_level(void);

// Debug printing functions
void debug_print(const char *format, ...);
void debug_error(const char *format, ...);
void debug_warn(const char *format, ...);
void debug_info(const char *format, ...);
void debug_verbose(const char *format, ...);

// Utility debug print functions
void printhex(unsigned int value);
void printdec(unsigned int value);
void debug_print_hex(unsigned int value);
void debug_print_dec(unsigned int value);

// Conditional debug macros
#ifdef DEBUG_BUILD
    #define DEBUG_PRINT(fmt, ...) debug_print(fmt, ##__VA_ARGS__)
    #define DEBUG_ERROR(fmt, ...) debug_error(fmt, ##__VA_ARGS__)
    #define DEBUG_WARN(fmt, ...) debug_warn(fmt, ##__VA_ARGS__)
    #define DEBUG_INFO(fmt, ...) debug_info(fmt, ##__VA_ARGS__)
    #define DEBUG_VERBOSE(fmt, ...) debug_verbose(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...) do { } while(0)
    #define DEBUG_ERROR(fmt, ...) do { } while(0)
    #define DEBUG_WARN(fmt, ...) do { } while(0)
    #define DEBUG_INFO(fmt, ...) do { } while(0)
    #define DEBUG_VERBOSE(fmt, ...) do { } while(0)
#endif

// Debug assertions
#ifdef DEBUG_BUILD
    #define DEBUG_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                debug_error("Assertion failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
                while(1) {} \
            } \
        } while(0)
#else
    #define DEBUG_ASSERT(expr) do { } while(0)
#endif

#endif // DEBUG_H
