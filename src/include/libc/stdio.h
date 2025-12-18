#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#define __STDC_VERSION_STDIO_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"
#include <stddef.h>
#include <stdarg.h>

// Standard I/O functions
int printf(const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int vsprintf(char *buffer, const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...);
int puts(const char *str);
int putchar(int c);
int getchar(void);
char *gets(char *str);

// File operations (stubs for now)
typedef struct {
    int fd;
    int flags;
    char *buffer;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *filename, const char *mode);
int fclose(FILE *file);
int fgetc(FILE *file);
int fputc(int c, FILE *file);
char *fgets(char *str, int n, FILE *file);
int fputs(const char *str, FILE *file);

#ifdef __cplusplus
}
#endif

#endif