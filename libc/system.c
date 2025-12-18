#include "include/system.h"
#include "include/string.h"
#include "include/memory_safe.h"

static size_t guarded_copy_span(const void* dest, const void* src, size_t length) {
    size_t dest_span = memory_is_user_pointer(dest) ? memory_probe_user_buffer(dest, length)
                                                    : memory_probe_buffer(dest, length);
    size_t src_span = memory_is_user_pointer(src) ? memory_probe_user_buffer(src, length)
                                                  : memory_probe_buffer(src, length);
    size_t span = (dest_span < src_span) ? dest_span : src_span;
    return (span < length) ? span : length;
}

static inline bool io_port_in_range(uint16 port) {
    return port < 0x10000;
}

static inline size_t guarded_mmio_span(const void* address, size_t length) {
    if (!address || length == 0) {
        return 0;
    }

    return memory_is_user_pointer(address)
        ? memory_probe_user_buffer(address, length)
        : memory_probe_buffer(address, length);
}

uint8 inportb (uint16 _port)
{
        uint8 rv = 0;
        if (!io_port_in_range(_port)) {
            return rv;
        }
        __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
        return rv;
}

uint16 inportw(uint16 _port)
{
        uint16 rv = 0;
        if (!io_port_in_range(_port)) {
            return rv;
        }
        __asm__ __volatile__ ("inw %1, %0" : "=a" (rv) : "dN" (_port));
        return rv;
}

uint32 inportd(uint16 _port)
{
        uint32 rv = 0;
        if (!io_port_in_range(_port)) {
            return rv;
        }
        __asm__ __volatile__ ("inl %1, %0" : "=a" (rv) : "dN" (_port));
        return rv;
}

void outportb (uint16 _port, uint8 _data)
{
        if (!io_port_in_range(_port)) {
            return;
        }
        __asm__ __volatile__ ("outb %1, %0" : : "dN" (_port), "a" (_data));
}

void outportw(uint16 _port, uint16 _data)
{
        if (!io_port_in_range(_port)) {
            return;
        }
        __asm__ __volatile__ ("outw %1, %0" : : "dN" (_port), "a" (_data));
}

void outportd(uint16 _port, uint32 _data)
{
        if (!io_port_in_range(_port)) {
            return;
        }
        __asm__ __volatile__ ("outl %1, %0" : : "dN" (_port), "a" (_data));
}

void io_wait(void) {
    outportb(0x80, 0);
}

void *memset(void *s, int c, size_t n) {
    unsigned char* p = s;
    size_t span = memory_is_user_pointer(s) ? memory_probe_user_buffer(s, n)
                                            : memory_probe_buffer(s, n);
    for (size_t i = 0; i < span; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *dp = dest;
    const char *sp = src;
    size_t span = guarded_copy_span(dest, src, n);
    for (size_t i = 0; i < span; i++) {
        dp[i] = sp[i];
    }
    return dest;
}

uint8 mmio_read8(const volatile void* address) {
    size_t span = guarded_mmio_span(address, sizeof(uint8));
    if (span < sizeof(uint8)) {
        return 0;
    }
    return *(volatile const uint8*)address;
}

uint16 mmio_read16(const volatile void* address) {
    size_t span = guarded_mmio_span(address, sizeof(uint16));
    if (span < sizeof(uint16)) {
        return 0;
    }
    return *(volatile const uint16*)address;
}

uint32 mmio_read32(const volatile void* address) {
    size_t span = guarded_mmio_span(address, sizeof(uint32));
    if (span < sizeof(uint32)) {
        return 0;
    }
    return *(volatile const uint32*)address;
}

void mmio_write8(volatile void* address, uint8 value) {
    size_t span = guarded_mmio_span(address, sizeof(uint8));
    if (span < sizeof(uint8)) {
        return;
    }
    *(volatile uint8*)address = value;
}

void mmio_write16(volatile void* address, uint16 value) {
    size_t span = guarded_mmio_span(address, sizeof(uint16));
    if (span < sizeof(uint16)) {
        return;
    }
    *(volatile uint16*)address = value;
}

void mmio_write32(volatile void* address, uint32 value) {
    size_t span = guarded_mmio_span(address, sizeof(uint32));
    if (span < sizeof(uint32)) {
        return;
    }
    *(volatile uint32*)address = value;
}
uint32 cpu_get_cr0(void) {
    uint32 value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
}

void cpu_set_cr0(uint32 value) {
    __asm__ __volatile__("mov %0, %%cr0" : : "r"(value));
}

void cpu_set_cr3(uint32 value) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(value));
}

uint64 cpu_read_tsc(void) {
    uint32 low, high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64)high << 32) | low;
}

static inline uint8 rtc_read_register(uint8 reg) {
    outportb(0x70, reg);
    io_wait();
    return inportb(0x71);
}

static inline uint8 rtc_bcd_to_binary(uint8 value) {
    return (value & 0x0F) + ((value / 16) * 10);
}

bool rtc_read_time(rtc_time_t* out) {
    if (!out) {
        return false;
    }

    uint8 status_b = rtc_read_register(0x0B);
    bool binary_mode = status_b & 0x04;
    bool hour_24 = status_b & 0x02;

    uint8 seconds = rtc_read_register(0x00);
    uint8 minutes = rtc_read_register(0x02);
    uint8 hours = rtc_read_register(0x04);
    uint8 day = rtc_read_register(0x07);
    uint8 month = rtc_read_register(0x08);
    uint8 year = rtc_read_register(0x09);

    if (!binary_mode) {
        seconds = rtc_bcd_to_binary(seconds);
        minutes = rtc_bcd_to_binary(minutes);
        hours = rtc_bcd_to_binary(hours);
        day = rtc_bcd_to_binary(day);
        month = rtc_bcd_to_binary(month);
        year = rtc_bcd_to_binary(year);
    }

    if (!hour_24) {
        bool pm = hours & 0x80;
        hours = hours & 0x7F;
        if (pm && hours < 12) {
            hours = (hours + 12) % 24;
        }
    }

    out->seconds = seconds;
    out->minutes = minutes;
    out->hours = hours;
    out->day_of_month = day;
    out->month = month;
    out->year = 2000 + year;
    return true;
}

void timer_sleep_ms(uint32 milliseconds) {
    uint64 start = cpu_read_tsc();
    // Fallback to a simple port-delay loop when TSC frequency is unknown.
    const uint64 fallback_iterations = (uint64)milliseconds * 1000ULL;
    if (start == 0) {
        for (uint64 i = 0; i < fallback_iterations; i++) {
            io_wait();
        }
        return;
    }

    // Assume a conservative 1GHz clock if no calibration is available.
    const uint64 assumed_hz = 1000000000ULL;
    uint64 target_ticks = (assumed_hz / 1000ULL) * milliseconds;
    while (cpu_read_tsc() - start < target_ticks) {
        __asm__ __volatile__("pause");
    }
}
