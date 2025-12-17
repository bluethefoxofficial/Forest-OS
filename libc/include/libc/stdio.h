#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../types.h"

// Standard I/O functions
int printf(const char *format, ...);
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