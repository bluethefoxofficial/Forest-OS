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

static void format_uint_padded(char** buf, size_t* remaining, unsigned long value,
                               unsigned int base, bool uppercase, int width, char pad_char) {
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int i = 0;

    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0 && i < (int)sizeof(tmp)) {
            unsigned int digit = (unsigned int)(value % base);
            tmp[i++] = digits[digit];
            value /= base;
        }
    }

    // Add padding if width is specified
    while (i < width && i < (int)sizeof(tmp)) {
        tmp[i++] = pad_char;
    }

    // Output in reverse order
    while (i > 0) {
        buffer_append(buf, remaining, tmp[--i]);
    }
}

static void format_string_padded(char** buf, size_t* remaining, const char* str,
                                  int width, bool left_justify) {
    if (!str) {
        str = "(null)";
    }

    int len = 0;
    const char* p = str;
    while (*p++) len++;

    // Calculate padding needed
    int pad = (width > len) ? (width - len) : 0;

    // Right padding (left justify): output string first, then spaces
    // Left padding (right justify): output spaces first, then string
    if (!left_justify) {
        while (pad-- > 0) {
            buffer_append(buf, remaining, ' ');
        }
    }

    // Output the string
    while (*str) {
        buffer_append(buf, remaining, *str++);
    }

    if (left_justify) {
        while (pad-- > 0) {
            buffer_append(buf, remaining, ' ');
        }
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

        // Parse flags
        bool zero_pad = false;
        bool left_justify = false;

        while (*format == '-' || *format == '0' || *format == '+' || *format == ' ') {
            if (*format == '-') left_justify = true;
            else if (*format == '0') zero_pad = true;
            format++;
        }

        // Parse width
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        // Parse length modifier
        bool long_flag = false;
        if (*format == 'l') {
            long_flag = true;
            format++;
        }

        char pad_char = zero_pad ? '0' : ' ';

        switch (*format) {
            case 's': {
                const char* str = va_arg(args, const char*);
                if (width > 0) {
                    format_string_padded(&out, &remaining, str, width, left_justify);
                } else {
                    format_string(&out, &remaining, str);
                }
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
                    if (width > 0) width--;
                }
                format_uint_padded(&out, &remaining, (unsigned long)val, 10, false, width, pad_char);
                break;
            }
            case 'u': {
                unsigned long val = long_flag ? va_arg(args, unsigned long)
                                              : va_arg(args, unsigned int);
                format_uint_padded(&out, &remaining, val, 10, false, width, pad_char);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long val = long_flag ? va_arg(args, unsigned long)
                                              : va_arg(args, unsigned int);
                format_uint_padded(&out, &remaining, val, 16, *format == 'X', width, pad_char);
                break;
            }
            case 'p': {
                // Pointer format - always use full width for the architecture
                void* ptr = va_arg(args, void*);
                buffer_append(&out, &remaining, '0');
                buffer_append(&out, &remaining, 'x');
                format_uint_padded(&out, &remaining, (unsigned long)ptr, 16, false, sizeof(void*) * 2, '0');
                break;
            }
            case '%':
                buffer_append(&out, &remaining, '%');
                break;
            case '\0':
                // Premature end of format string
                goto done;
            default:
                buffer_append(&out, &remaining, '%');
                buffer_append(&out, &remaining, *format);
                break;
        }
        format++;
    }

done:
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
