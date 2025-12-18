#include <stdarg.h>
#include <stdbool.h>
#include "../../src/include/libc/stdio.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/string.h"

static void buffer_append(char **buf, size_t *remaining, char c) {
    if (*remaining == 0) {
        return;
    }
    **buf = c;
    (*buf)++;
    (*remaining)--;
}

static void format_string(char **buf, size_t *remaining, const char *str) {
    while (*str) {
        buffer_append(buf, remaining, *str++);
    }
}

static void format_uint(char **buf, size_t *remaining, unsigned int value, unsigned int base, bool uppercase) {
    char digits[] = "0123456789abcdef";
    char digits_upper[] = "0123456789ABCDEF";
    char tmp[32];
    unsigned int i = 0;

    if (value == 0) {
        buffer_append(buf, remaining, '0');
        return;
    }

    while (value > 0 && i < sizeof(tmp)) {
        unsigned int digit = value % base;
        tmp[i++] = uppercase ? digits_upper[digit] : digits[digit];
        value /= base;
    }

    while (i > 0) {
        buffer_append(buf, remaining, tmp[--i]);
    }
}

static int vsnprintf_simple(char *buffer, size_t size, const char *format, va_list args) {
    char *out = buffer;
    size_t remaining = size ? size - 1 : 0;

    while (*format) {
        if (*format != '%') {
            buffer_append(&out, &remaining, *format++);
            continue;
        }

        format++;
        bool long_flag = false;
        if (*format == 'l') {
            long_flag = true;
            format++;
        }

        switch (*format) {
            case 's': {
                const char *str = va_arg(args, const char*);
                if (!str) {
                    str = "(null)";
                }
                format_string(&out, &remaining, str);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buffer_append(&out, &remaining, c);
                break;
            }
            case 'd':
            case 'i': {
                int val = long_flag ? va_arg(args, long) : va_arg(args, int);
                if (val < 0) {
                    buffer_append(&out, &remaining, '-');
                    val = -val;
                }
                format_uint(&out, &remaining, (unsigned int)val, 10, false);
                break;
            }
            case 'u': {
                unsigned int val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint(&out, &remaining, val, 10, false);
                break;
            }
            case 'x': {
                unsigned int val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint(&out, &remaining, val, 16, false);
                break;
            }
            case 'X': {
                unsigned int val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint(&out, &remaining, val, 16, true);
                break;
            }
            case '%':
                buffer_append(&out, &remaining, '%');
                break;
            default:
                buffer_append(&out, &remaining, '%');
                buffer_append(&out, &remaining, *format);
                break;
        }
        format++;
    }

    if (size) {
        *out = '\0';
    }

    return (int)(out - buffer);
}

int vsnprintf(char *buffer, size_t size, const char *format, va_list args);

int vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    return vsnprintf_simple(buffer, size, format, args);
}

int vsprintf(char *buffer, const char *format, va_list args) {
    return vsnprintf(buffer, (size_t)-1, format, args);
}

int sprintf(char *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf_simple(buffer, (size_t)-1, format, args);
    va_end(args);
    return written;
}

int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf_simple(buffer, size, format, args);
    va_end(args);
    return written;
}

int printf(const char *format, ...) {
    char temp[512];
    va_list args;
    va_start(args, format);
    int written = vsnprintf_simple(temp, sizeof(temp), format, args);
    va_end(args);
    write(1, temp, (size_t)written);
    return written;
}

int puts(const char *str) {
    size_t len = strlen(str);
    write(1, str, len);
    write(1, "\n", 1);
    return (int)(len + 1);
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int getchar(void) {
    char ch = 0;
    if (read(0, &ch, 1) <= 0) {
        return -1;
    }
    return ch;
}

char *gets(char *str) {
    if (!str) {
        return NULL;
    }
    size_t i = 0;
    while (1) {
        int c = getchar();
        if (c <= 0 || c == '\n' || c == '\r') {
            str[i] = '\0';
            return str;
        }
        str[i++] = (char)c;
    }
}
