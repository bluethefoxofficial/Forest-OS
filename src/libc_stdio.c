#include <stdarg.h>
#include <stdbool.h>
#include "include/libc/stdio.h"
#include "include/libc/string.h"

static void buffer_append(char** buf, size_t* remaining, char c) {
    if (*remaining == 0) {
        return;
    }
    **buf = c;
    (*buf)++;
    (*remaining)--;
}

static void format_string(char** buf, size_t* remaining, const char* str) {
    if (!str) {
        str = "(null)";
    }
    while (*str) {
        buffer_append(buf, remaining, *str++);
    }
}

static void format_uint(char** buf, size_t* remaining, unsigned long value,
                        unsigned int base, bool uppercase) {
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    unsigned int i = 0;

    if (value == 0) {
        buffer_append(buf, remaining, '0');
        return;
    }

    while (value > 0 && i < sizeof(tmp)) {
        unsigned int digit = (unsigned int)(value % base);
        tmp[i++] = digits[digit];
        value /= base;
    }

    while (i > 0) {
        buffer_append(buf, remaining, tmp[--i]);
    }
}

static int vsnprintf_simple(char* buffer, size_t size, const char* format, va_list args) {
    if (!buffer || !format) {
        return 0;
    }

    char* out = buffer;
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
                const char* str = va_arg(args, const char*);
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
                long val = long_flag ? va_arg(args, long) : va_arg(args, int);
                if (val < 0) {
                    buffer_append(&out, &remaining, '-');
                    val = -val;
                }
                format_uint(&out, &remaining, (unsigned long)val, 10, false);
                break;
            }
            case 'u': {
                unsigned long val = long_flag ? va_arg(args, unsigned long)
                                              : va_arg(args, unsigned int);
                format_uint(&out, &remaining, val, 10, false);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long val = long_flag ? va_arg(args, unsigned long)
                                              : va_arg(args, unsigned int);
                format_uint(&out, &remaining, val, 16, *format == 'X');
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

int vsnprintf(char* buffer, size_t size, const char* format, va_list args) {
    return vsnprintf_simple(buffer, size, format, args);
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf_simple(buffer, size, format, args);
    va_end(args);
    return written;
}

int vsprintf(char* buffer, const char* format, va_list args) {
    return vsnprintf(buffer, (size_t)-1, format, args);
}

int sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf_simple(buffer, (size_t)-1, format, args);
    va_end(args);
    return written;
}

int printf(const char* format, ...) {
    (void)format;
    return 0;
}

int puts(const char* str) {
    (void)str;
    return 0;
}

int putchar(int c) {
    return c;
}

int getchar(void) {
    return -1;
}

char* gets(char* str) {
    if (str) {
        *str = '\0';
    }
    return str;
}

FILE* stdin = NULL;
FILE* stdout = NULL;
FILE* stderr = NULL;

FILE* fopen(const char* filename, const char* mode) {
    (void)filename;
    (void)mode;
    return NULL;
}

int fclose(FILE* file) {
    (void)file;
    return -1;
}

int fgetc(FILE* file) {
    (void)file;
    return -1;
}

int fputc(int c, FILE* file) {
    (void)file;
    return c;
}

char* fgets(char* str, int n, FILE* file) {
    (void)str;
    (void)n;
    (void)file;
    return NULL;
}

int fputs(const char* str, FILE* file) {
    (void)str;
    (void)file;
    return -1;
}
