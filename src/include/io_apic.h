#ifndef IO_APIC_H
#define IO_APIC_H

#include <stdint.h>
#include <stdbool.h>
#include "interrupt.h"

/*
 * I/O APIC (Advanced Programmable Interrupt Controller)
 * Handles external interrupt routing and distribution
 */

#define IO_APIC_MAX_REDIRECTIONS 24
#define IO_APIC_REG_SELECT      0x00
#define IO_APIC_REG_WINDOW      0x10

// I/O APIC register offsets
#define IO_APIC_REG_ID          0x00
#define IO_APIC_REG_VERSION     0x01
#define IO_APIC_REG_ARBITRATION 0x02
#define IO_APIC_REG_REDIRECTION_BASE 0x10

// Redirection table entry bits
#define IO_APIC_REDIR_MASK      (1 << 16)
#define IO_APIC_REDIR_TRIGGER_LEVEL (1 << 15)
#define IO_APIC_REDIR_TRIGGER_EDGE  (0 << 15)
#define IO_APIC_REDIR_POLARITY_LOW  (1 << 13)
#define IO_APIC_REDIR_POLARITY_HIGH (0 << 13)
#define IO_APIC_REDIR_DEST_LOGICAL  (1 << 11)
#define IO_APIC_REDIR_DEST_PHYSICAL (0 << 11)
#define IO_APIC_REDIR_PENDING   (1 << 12)

// Delivery modes
#define IO_APIC_DELIVERY_FIXED      0x0
#define IO_APIC_DELIVERY_LOWPRI     0x1
#define IO_APIC_DELIVERY_SMI        0x2
#define IO_APIC_DELIVERY_NMI        0x4
#define IO_APIC_DELIVERY_INIT       0x5
#define IO_APIC_DELIVERY_EXTINT     0x7

typedef struct {
    uint32_t vector:8;
    uint32_t delivery_mode:3;
    uint32_t dest_mode:1;
    uint32_t delivery_status:1;
    uint32_t polarity:1;
    uint32_t remote_irr:1;
    uint32_t trigger:1;
    uint32_t mask:1;
    uint32_t reserved:15;
    uint32_t destination:8;
    uint32_t reserved2:24;
} __attribute__((packed)) io_apic_redir_entry_t;

typedef struct {
    uintptr_t base_address;
    uint32_t id;
    uint32_t version;
    uint32_t max_redirections;
    bool initialized;
} io_apic_t;

// I/O APIC initialization and management
int io_apic_init(void);
int io_apic_discover(void);
void io_apic_shutdown(void);

// Register access
uint32_t io_apic_read_register(uint32_t reg);
void io_apic_write_register(uint32_t reg, uint32_t value);

// Redirection table management
int io_apic_set_redirection(uint8_t irq, uint8_t vector, uint8_t dest_cpu);
int io_apic_mask_irq(uint8_t irq);
int io_apic_unmask_irq(uint8_t irq);
int io_apic_set_trigger_mode(uint8_t irq, bool level_triggered);
int io_apic_set_polarity(uint8_t irq, bool active_low);

// IRQ routing
int io_apic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_cpu);
int io_apic_disable_legacy_pic(void);

// Information functions
uint32_t io_apic_get_max_redirections(void);
bool io_apic_is_initialized(void);
uint32_t io_apic_get_id(void);

// ISA IRQ mapping
int io_apic_map_isa_irq(uint8_t isa_irq, uint8_t gsi);

#endif // IO_APIC_H