#ifndef SYSTEM_H
#define SYSTEM_H
#include "types.h"
#include "util.h"

uint8 inportb (uint16 _port);
void outportb (uint16 _port, uint8 _data);
uint32 cpu_get_cr0(void);
void cpu_set_cr0(uint32 value);
void cpu_set_cr3(uint32 value);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int strcmp(const char *s1, const char *s2);
char *strncpy(char *dest, const char *src, size_t n);

#endif
