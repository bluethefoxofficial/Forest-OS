#ifndef LOCAL_APIC_H
#define LOCAL_APIC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Local APIC (Advanced Programmable Interrupt Controller)
 * Handles per-CPU interrupt processing and IPI delivery
 */

// Local APIC register offsets
#define LAPIC_REG_ID                0x020
#define LAPIC_REG_VERSION           0x030
#define LAPIC_REG_TPR               0x080  // Task Priority Register
#define LAPIC_REG_APR               0x090  // Arbitration Priority Register
#define LAPIC_REG_PPR               0x0A0  // Processor Priority Register
#define LAPIC_REG_EOI               0x0B0  // End of Interrupt
#define LAPIC_REG_RRD               0x0C0  // Remote Read Register
#define LAPIC_REG_LDR               0x0D0  // Logical Destination Register
#define LAPIC_REG_DFR               0x0E0  // Destination Format Register
#define LAPIC_REG_SPURIOUS          0x0F0  // Spurious Interrupt Vector
#define LAPIC_REG_ISR_BASE          0x100  // In-Service Register (8 registers)
#define LAPIC_REG_TMR_BASE          0x180  // Trigger Mode Register (8 registers)
#define LAPIC_REG_IRR_BASE          0x200  // Interrupt Request Register (8 registers)
#define LAPIC_REG_ESR               0x280  // Error Status Register
#define LAPIC_REG_ICR_LOW           0x300  // Interrupt Command Register Low
#define LAPIC_REG_ICR_HIGH          0x310  // Interrupt Command Register High
#define LAPIC_REG_LVT_TIMER         0x320  // LVT Timer Register
#define LAPIC_REG_LVT_THERMAL       0x330  // LVT Thermal Monitor
#define LAPIC_REG_LVT_PERF          0x340  // LVT Performance Counter
#define LAPIC_REG_LVT_LINT0         0x350  // LVT LINT0 Register
#define LAPIC_REG_LVT_LINT1         0x360  // LVT LINT1 Register
#define LAPIC_REG_LVT_ERROR         0x370  // LVT Error Register
#define LAPIC_REG_TIMER_INIT        0x380  // Timer Initial Count
#define LAPIC_REG_TIMER_CURRENT     0x390  // Timer Current Count
#define LAPIC_REG_TIMER_DIVIDE      0x3E0  // Timer Divide Configuration

// MSR addresses
#define MSR_APIC_BASE               0x1B
#define MSR_X2APIC_ENABLE           0x400

// APIC base register bits
#define APIC_BASE_ENABLE            (1 << 11)
#define APIC_BASE_X2APIC            (1 << 10)
#define APIC_BASE_BSP               (1 << 8)

// Spurious vector register bits
#define LAPIC_SPURIOUS_ENABLE       (1 << 8)
#define LAPIC_SPURIOUS_FOCUS_DISABLE (1 << 9)

// LVT register bits
#define LAPIC_LVT_MASK              (1 << 16)
#define LAPIC_LVT_TRIGGER_LEVEL     (1 << 15)
#define LAPIC_LVT_TRIGGER_EDGE      (0 << 15)
#define LAPIC_LVT_REMOTE_IRR        (1 << 14)
#define LAPIC_LVT_POLARITY_LOW      (1 << 13)
#define LAPIC_LVT_POLARITY_HIGH     (0 << 13)
#define LAPIC_LVT_DELIVERY_PENDING  (1 << 12)

// LVT delivery modes
#define LAPIC_LVT_DELIVERY_FIXED    0x0
#define LAPIC_LVT_DELIVERY_SMI      0x2
#define LAPIC_LVT_DELIVERY_NMI      0x4
#define LAPIC_LVT_DELIVERY_EXTINT   0x7

// ICR delivery modes
#define LAPIC_ICR_DELIVERY_FIXED    0x0
#define LAPIC_ICR_DELIVERY_LOWPRI   0x1
#define LAPIC_ICR_DELIVERY_SMI      0x2
#define LAPIC_ICR_DELIVERY_NMI      0x4
#define LAPIC_ICR_DELIVERY_INIT     0x5
#define LAPIC_ICR_DELIVERY_STARTUP  0x6

// ICR destination modes
#define LAPIC_ICR_DEST_PHYSICAL     0x0
#define LAPIC_ICR_DEST_LOGICAL      0x1

// ICR destination shortcuts
#define LAPIC_ICR_DEST_SELF         0x1
#define LAPIC_ICR_DEST_ALL          0x2
#define LAPIC_ICR_DEST_ALL_BUT_SELF 0x3

// Timer modes
#define LAPIC_TIMER_ONESHOT         0x0
#define LAPIC_TIMER_PERIODIC        0x1
#define LAPIC_TIMER_TSC_DEADLINE    0x2

// Timer divide values
#define LAPIC_TIMER_DIV_2           0x0
#define LAPIC_TIMER_DIV_4           0x1
#define LAPIC_TIMER_DIV_8           0x2
#define LAPIC_TIMER_DIV_16          0x3
#define LAPIC_TIMER_DIV_32          0x8
#define LAPIC_TIMER_DIV_64          0x9
#define LAPIC_TIMER_DIV_128         0xA
#define LAPIC_TIMER_DIV_1           0xB

typedef struct {
    uintptr_t base_address;
    uint32_t id;
    uint32_t version;
    bool x2apic_mode;
    bool initialized;
    uint32_t timer_frequency;
} local_apic_t;

// Local APIC initialization
int local_apic_init(void);
int local_apic_init_cpu(void);
void local_apic_shutdown(void);

// APIC detection and setup
bool local_apic_is_available(void);
bool local_apic_is_x2apic_available(void);
int local_apic_enable(void);
int local_apic_enable_x2apic(void);
void local_apic_disable(void);

// Register access
uint32_t local_apic_read(uint32_t reg);
void local_apic_write(uint32_t reg, uint32_t value);

// Basic APIC operations
uint32_t local_apic_get_id(void);
void local_apic_send_eoi(void);
int local_apic_set_spurious_vector(uint8_t vector);

// Timer functions
int local_apic_timer_init(uint32_t frequency_hz);
int local_apic_timer_start(uint32_t initial_count);
void local_apic_timer_stop(void);
int local_apic_timer_set_mode(uint8_t mode);
int local_apic_timer_set_divide(uint8_t divide);
uint32_t local_apic_timer_get_current(void);
int local_apic_timer_calibrate(void);

// LVT (Local Vector Table) management
int local_apic_set_lvt_timer(uint8_t vector, bool masked);
int local_apic_set_lvt_lint0(uint8_t vector, uint8_t delivery_mode, bool masked);
int local_apic_set_lvt_lint1(uint8_t vector, uint8_t delivery_mode, bool masked);
int local_apic_set_lvt_error(uint8_t vector, bool masked);
int local_apic_set_lvt_thermal(uint8_t vector, bool masked);
int local_apic_set_lvt_performance(uint8_t vector, bool masked);

// IPI (Inter-Processor Interrupt) functions
int local_apic_send_ipi(uint32_t dest_id, uint8_t vector);
int local_apic_send_ipi_all(uint8_t vector);
int local_apic_send_ipi_all_but_self(uint8_t vector);
int local_apic_send_ipi_self(uint8_t vector);
int local_apic_send_init_ipi(uint32_t dest_id);
int local_apic_send_startup_ipi(uint32_t dest_id, uint8_t vector);
int local_apic_send_nmi(uint32_t dest_id);
int local_apic_send_nmi_all(void);
int local_apic_send_nmi_all_but_self(void);

// Priority and masking
int local_apic_set_task_priority(uint8_t priority);
uint8_t local_apic_get_task_priority(void);
uint8_t local_apic_get_processor_priority(void);

// Status and error handling
uint32_t local_apic_get_error_status(void);
void local_apic_clear_error_status(void);
bool local_apic_is_delivery_pending(void);

// Information functions
const local_apic_t *local_apic_get_info(void);
bool local_apic_is_initialized(void);
uint32_t local_apic_get_version(void);
uint32_t local_apic_get_max_lvt(void);

// Debugging and diagnostics
void local_apic_dump_registers(void);
void local_apic_dump_lvt(void);
void local_apic_dump_isr(void);
void local_apic_dump_irr(void);

#endif // LOCAL_APIC_H