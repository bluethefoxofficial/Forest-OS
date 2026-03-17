#include <stdarg.h>
#include <stdbool.h>
#include "../../src/include/libc/stdio.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/string.h"
#include "../../src/include/libc/errno.h"
#include "../../src/include/libc/stdlib.h"

// File opening modes (basic support)
#define O_RDONLY    0x00
#define O_WRONLY    0x01
#define O_RDWR      0x02
#define O_CREAT     0x40
#define O_TRUNC     0x200
#define O_APPEND    0x400

// File buffering modes
#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

// Seek constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Extended stdio functions for better POSIX compatibility (simplified)

FILE *fopen(const char *pathname, const char *mode) {
    if (!pathname || !mode) {
        errno = EINVAL;
        return NULL;
    }
    
    int flags = 0;
    // Parse mode string to get flags
    if (mode[0] == 'r') {
        flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        errno = EINVAL;
        return NULL;
    }
    
    int fd = open(pathname, flags);
    if (fd < 0) {
        return NULL;
    }
    
    // Create FILE structure (simplified)
    FILE *fp = malloc(sizeof(FILE));
    if (!fp) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    
    fp->fd = fd;
    fp->flags = 0;
    fp->buffer = NULL;
    
    return fp;
}

int fclose(FILE *fp) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    
    int result = 0;
    if (fp->fd >= 0) {
        if (close(fp->fd) < 0) {
            result = -1;
        }
    }
    
    if (fp->buffer) {
        free(fp->buffer);
    }
    free(fp);
    return result;
}

int fgetc(FILE *fp) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    
    unsigned char c;
    ssize_t result = read(fp->fd, &c, 1);
    if (result <= 0) {
        return -1;
    }
    return c;
}

int fputc(int c, FILE *fp) {
    if (!fp) {
        errno = EINVAL;
        return -1;
    }
    
    unsigned char ch = (unsigned char)c;
    ssize_t result = write(fp->fd, &ch, 1);
    if (result <= 0) {
        return -1;
    }
    return c;
}

char *fgets(char *str, int n, FILE *fp) {
    if (!str || !fp || n <= 0) {
        errno = EINVAL;
        return NULL;
    }
    
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(fp);
        if (c < 0) {
            if (i == 0) return NULL;
            break;
        }
        
        str[i++] = c;
        if (c == '\n') break;
    }
    
    str[i] = '\0';
    return str;
}

int fputs(const char *s, FILE *fp) {
    if (!s || !fp) {
        errno = EINVAL;
        return -1;
    }
    
    size_t len = strlen(s);
    ssize_t result = write(fp->fd, s, len);
    if (result < 0) {
        return -1;
    }
    return 0;
}

static void buffer_append(char **buf, size_t *remaining, char c) {
    if (*remaining == 0) {
        return;
    }
    **buf = c;
    (*buf)++;
    (*remaining)--;
}

static void format_string(char **buf, size_t *remaining, const char *str) {
    if (!str) {
        str = "(null)";
    }
    while (*str) {
        buffer_append(buf, remaining, *str++);
    }
}

static void format_uint_padded(char **buf, size_t *remaining, unsigned long value,
                               unsigned int base, bool uppercase, int width, char pad_char) {
    char digits[] = "0123456789abcdef";
    char digits_upper[] = "0123456789ABCDEF";
    char tmp[32];
    int i = 0;

    if (value == 0) {
        tmp[i++] = '0';
    } else {
        while (value > 0 && i < (int)sizeof(tmp)) {
            unsigned int digit = value % base;
            tmp[i++] = uppercase ? digits_upper[digit] : digits[digit];
            value /= base;
        }
    }

    while (i < width && i < (int)sizeof(tmp)) {
        tmp[i++] = pad_char;
    }

    while (i > 0) {
        buffer_append(buf, remaining, tmp[--i]);
    }
}

static void format_string_padded(char **buf, size_t *remaining, const char *str,
                                 int width, bool left_justify) {
    if (!str) {
        str = "(null)";
    }

    int len = 0;
    const char *p = str;
    while (*p++) {
        len++;
    }
    int pad = (width > len) ? (width - len) : 0;

    if (!left_justify) {
        while (pad-- > 0) {
            buffer_append(buf, remaining, ' ');
        }
    }

    format_string(buf, remaining, str);

    if (left_justify) {
        while (pad-- > 0) {
            buffer_append(buf, remaining, ' ');
        }
    }
}

static int vsnprintf_simple(char *buffer, size_t size, const char *format, va_list args) {
    if (!buffer || !format) {
        return 0;
    }

    char *out = buffer;
    size_t remaining = size ? size - 1 : 0;

    while (*format) {
        if (*format != '%') {
            buffer_append(&out, &remaining, *format++);
            continue;
        }

        format++;

        bool zero_pad = false;
        bool left_justify = false;

        while (*format == '-' || *format == '0' || *format == '+' || *format == ' ') {
            if (*format == '-') {
                left_justify = true;
            } else if (*format == '0') {
                zero_pad = true;
            }
            format++;
        }

        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        bool long_flag = false;
        if (*format == 'l') {
            long_flag = true;
            format++;
        }
        char pad_char = zero_pad ? '0' : ' ';

        switch (*format) {
            case 's': {
                const char *str = va_arg(args, const char*);
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
                    if (width > 0) {
                        width--;
                    }
                }
                format_uint_padded(&out, &remaining, (unsigned long)val, 10, false, width, pad_char);
                break;
            }
            case 'u': {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint_padded(&out, &remaining, val, 10, false, width, pad_char);
                break;
            }
            case 'x': {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint_padded(&out, &remaining, val, 16, false, width, pad_char);
                break;
            }
            case 'X': {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
                format_uint_padded(&out, &remaining, val, 16, true, width, pad_char);
                break;
            }
            case 'p': {
                void *ptr = va_arg(args, void *);
                buffer_append(&out, &remaining, '0');
                buffer_append(&out, &remaining, 'x');
                format_uint_padded(&out, &remaining, (unsigned long)ptr, 16, false,
                                   (int)(sizeof(void*) * 2), '0');
                break;
            }
            case '%':
                buffer_append(&out, &remaining, '%');
                break;
            case '\0':
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

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }

    size_t total_bytes = size * nmemb;
    ssize_t result = read(stream->fd, ptr, total_bytes);
    if (result < 0) {
        return 0;
    }
    return (size_t)result / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }

    size_t total_bytes = size * nmemb;
    ssize_t result = write(stream->fd, ptr, total_bytes);
    if (result < 0) {
        return 0;
    }
    return (size_t)result / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    int result = lseek(stream->fd, offset, whence);
    if (result < 0) {
        return -1;
    }
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    // Get current position by seeking 0 from current position
    return (long)lseek(stream->fd, 0, SEEK_CUR);
}

int sscanf(const char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    int parsed = 0;
    const char *p = str;
    const char *f = format;
    
    while (*f && *p) {
        if (*f == '%') {
            f++;
            if (*f == 'd') {
                int *val = va_arg(args, int*);
                int num = 0;
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                while (*p >= '0' && *p <= '9') {
                    num = num * 10 + (*p - '0');
                    p++;
                }
                *val = neg ? -num : num;
                parsed++;
            } else if (*f == 'u') {
                unsigned int *val = va_arg(args, unsigned int*);
                unsigned int num = 0;
                while (*p >= '0' && *p <= '9') {
                    num = num * 10 + (*p - '0');
                    p++;
                }
                *val = num;
                parsed++;
            } else if (*f == 's') {
                char *val = va_arg(args, char*);
                while (*p && *p != ' ' && *p != '\n' && *p != '\t') {
                    *val++ = *p++;
                }
                *val = '\0';
                parsed++;
            } else if (*f == 'c') {
                char *val = va_arg(args, char*);
                *val = *p++;
                parsed++;
            } else if (*f == 'x') {
                unsigned int *val = va_arg(args, unsigned int*);
                unsigned int num = 0;
                while (1) {
                    if (*p >= '0' && *p <= '9') {
                        num = num * 16 + (*p - '0');
                    } else if (*p >= 'a' && *p <= 'f') {
                        num = num * 16 + (*p - 'a' + 10);
                    } else if (*p >= 'A' && *p <= 'F') {
                        num = num * 16 + (*p - 'A' + 10);
                    } else {
                        break;
                    }
                    p++;
                }
                *val = num;
                parsed++;
            } else if (*f == 'l' && *(f+1) == 'd') {
                long *val = va_arg(args, long*);
                long num = 0;
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                while (*p >= '0' && *p <= '9') {
                    num = num * 10 + (*p - '0');
                    p++;
                }
                *val = neg ? -num : num;
                parsed++;
                f++;
            }
        } else if (*f == *p) {
            f++;
            p++;
        } else if (*p == ' ' || *p == '\n' || *p == '\t') {
            p++;
        } else {
            break;
        }
    }
    
    va_end(args);
    return parsed;
}
