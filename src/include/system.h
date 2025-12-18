#ifndef SYSTEM_H
#define SYSTEM_H
#include "types.h"
#include "util.h"

uint8 inportb (uint16 _port);
uint16 inportw(uint16 _port);
uint32 inportd(uint16 _port);
void outportb (uint16 _port, uint8 _data);
void outportw(uint16 _port, uint16 _data);
void outportd(uint16 _port, uint32 _data);
void io_wait(void);

uint8  mmio_read8 (const volatile void* address);
uint16 mmio_read16(const volatile void* address);
uint32 mmio_read32(const volatile void* address);
void   mmio_write8 (volatile void* address, uint8 value);
void   mmio_write16(volatile void* address, uint16 value);
void   mmio_write32(volatile void* address, uint32 value);

uint64 cpu_read_tsc(void);

typedef struct {
    uint8 seconds;
    uint8 minutes;
    uint8 hours;
    uint8 day_of_month;
    uint8 month;
    uint16 year;
} rtc_time_t;

bool rtc_read_time(rtc_time_t* out);
void timer_sleep_ms(uint32 milliseconds);
uint32 cpu_get_cr0(void);
void cpu_set_cr0(uint32 value);
void cpu_set_cr3(uint32 value);

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int strcmp(const char *s1, const char *s2);
char *strncpy(char *dest, const char *src, size_t n);

#endif
