#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

// APIC register offsets
#define LAPIC_ID            0x020
#define LAPIC_VERSION       0x030
#define LAPIC_TPR           0x080
#define LAPIC_APR           0x090
#define LAPIC_PPR           0x0A0
#define LAPIC_EOI           0x0B0
#define LAPIC_RRD           0x0C0
#define LAPIC_LDR           0x0D0
#define LAPIC_DFR           0x0E0
#define LAPIC_SVR           0x0F0
#define LAPIC_ISR           0x100
#define LAPIC_TMR           0x180
#define LAPIC_IRR           0x200
#define LAPIC_ESR           0x280
#define LAPIC_CMCI          0x2F0
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310
#define LAPIC_TIMER         0x320
#define LAPIC_THERMAL       0x330
#define LAPIC_PERF          0x340
#define LAPIC_LINT0         0x350
#define LAPIC_LINT1         0x360
#define LAPIC_ERROR         0x370
#define LAPIC_TIMER_ICR     0x380
#define LAPIC_TIMER_CCR     0x390
#define LAPIC_TIMER_DCR     0x3E0

// APIC constants
#define APIC_SPURIOUS_VECTOR    0xFF
#define APIC_SOFTWARE_ENABLE    0x100
#define APIC_TIMER_PERIODIC     0x20000
#define APIC_TIMER_ONE_SHOT     0x00000

// MSR definitions
#define MSR_APIC_BASE           0x1B
#define MSR_X2APIC_ID           0x802

// Function declarations  
int apic_init(void);
bool apic_is_available(void);
int apic_enable_local_apic(void);
void apic_send_eoi(void);
uint32_t apic_read_register(uint32_t reg);
void apic_write_register(uint32_t reg, uint32_t value);
int apic_send_ipi(uint32_t dest_apic_id, uint32_t vector, uint32_t delivery_mode);
void apic_send_init_ipi(uint32_t dest_apic_id);
void apic_send_startup_ipi(uint32_t dest_apic_id, uint8_t vector);
int apic_calibrate_timer(void);
void apic_start_timer(uint32_t ticks, bool periodic);
void apic_stop_timer(void);

// IRQ definitions that were missing
#define IRQ_APIC_ERROR          0xF0
#define IRQ_APIC_TIMER          0xF1

// Utility functions
uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);
uint64_t read_tsc(void);
uint32_t get_kernel_cs(void);

#endif // APIC_H