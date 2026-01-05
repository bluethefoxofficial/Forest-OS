/*
 * I/O APIC (Input/Output Advanced Programmable Interrupt Controller) Driver
 * Provides multiprocessor interrupt routing and distribution
 * Integrates with ACPI MADT and Forest OS interrupt management system
 */

#include "interrupt.h"
#include "smp.h"
#include "acpi.h"
#include "cpu_ops.h"
#include "debuglog.h"
#include "panic.h"
#include "mm.h"
#include "memory.h"
#include "atomic.h"
#include "spinlock.h"

/* Page flags if not already defined */
#ifndef PAGE_PRESENT
#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_CACHE_DISABLE  0x010
#endif

/* IRQ base offset */
#ifndef IRQ_BASE
#define IRQ_BASE            0x20
#endif

/* I/O APIC Register Offsets */
#define IOAPIC_REG_SELECT           0x00    /* Register Select */
#define IOAPIC_REG_WINDOW           0x10    /* Register Window */

/* I/O APIC Registers (accessed through window) */
#define IOAPIC_REGSEL_ID            0x00    /* I/O APIC ID */
#define IOAPIC_REGSEL_VERSION       0x01    /* I/O APIC Version */
#define IOAPIC_REGSEL_ARB           0x02    /* I/O APIC Arbitration */
#define IOAPIC_REGSEL_REDTBL_BASE   0x10    /* Redirection Table Base */

/* Redirection Table Entry Flags */
#define IOAPIC_DEST_PHYSICAL        0x00000
#define IOAPIC_DEST_LOGICAL         0x08000
#define IOAPIC_INTPOL_ACTIVE_HIGH   0x00000
#define IOAPIC_INTPOL_ACTIVE_LOW    0x02000
#define IOAPIC_TRIGGER_EDGE         0x00000
#define IOAPIC_TRIGGER_LEVEL        0x08000
#define IOAPIC_MASK                 0x10000

/* Delivery Modes */
#define IOAPIC_DELMODE_FIXED        0x00000
#define IOAPIC_DELMODE_LOWPRI       0x00100
#define IOAPIC_DELMODE_SMI          0x00200
#define IOAPIC_DELMODE_NMI          0x00400
#define IOAPIC_DELMODE_INIT         0x00500
#define IOAPIC_DELMODE_EXTINT       0x00700

/* ACPI MADT Entry Types (if not defined in acpi.h) */
#ifndef MADT_ENTRY_TYPE_IOAPIC
#define MADT_ENTRY_TYPE_IOAPIC      1
#define MADT_ENTRY_TYPE_OVERRIDE    2
#define MADT_ENTRY_TYPE_NMI         3
#endif

/* ACPI MADT I/O APIC entry (type 1) - if not defined in acpi.h */
#ifndef ACPI_MADT_IOAPIC_DEFINED
#define ACPI_MADT_IOAPIC_DEFINED
typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t global_irq_base;
} __attribute__((packed)) acpi_madt_ioapic_t;
#endif

/* ACPI MADT Interrupt Override entry (type 2) - if not defined in acpi.h */
#ifndef ACPI_MADT_OVERRIDE_DEFINED
#define ACPI_MADT_OVERRIDE_DEFINED
typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t bus;            /* 0 = ISA */
    uint8_t source_irq;     /* IRQ on the bus */
    uint32_t global_irq;    /* Global system interrupt */
    uint16_t flags;         /* Polarity and trigger mode */
} __attribute__((packed)) acpi_madt_override_t;
#endif

typedef struct {
    uint32_t low;
    uint32_t high;
} ioapic_redtbl_entry_t;

/* I/O APIC device structure */
struct ioapic_device {
    bool present;
    uint8_t id;
    uint32_t *base_addr;
    uint32_t physical_addr;
    uint32_t global_irq_base;
    uint32_t max_redirections;
    uint32_t version;
    atomic64_t interrupt_count[24];  /* Maximum 24 IRQ lines per I/O APIC */
    spinlock_t lock;
};

/* IRQ override structure */
struct irq_override {
    uint8_t source_irq;
    uint32_t global_irq;
    uint16_t flags;
    bool active;
};

/* I/O APIC subsystem state */
struct ioapic_state {
    bool initialized;
    int num_ioapics;
    struct ioapic_device ioapics[8];  /* Support up to 8 I/O APICs */
    int num_overrides;
    struct irq_override overrides[32]; /* Support up to 32 IRQ overrides */
    uint32_t total_irqs;
    atomic64_t total_interrupts;
    spinlock_t global_lock;
};

static struct ioapic_state ioapic_state = {
    .initialized = false,
    .num_ioapics = 0,
    .num_overrides = 0,
    .total_irqs = 0,
    .global_lock = SPINLOCK_INIT("ioapic_global")
};

/* Function prototypes */
static int ioapic_discover_devices(void);
static int ioapic_init_device(struct ioapic_device *ioapic);
static uint32_t ioapic_read_register(struct ioapic_device *ioapic, uint8_t reg);
static void ioapic_write_register(struct ioapic_device *ioapic, uint8_t reg, uint32_t value);
static ioapic_redtbl_entry_t ioapic_read_redtbl(struct ioapic_device *ioapic, uint8_t irq);
static void ioapic_write_redtbl(struct ioapic_device *ioapic, uint8_t irq, ioapic_redtbl_entry_t entry);
static struct ioapic_device *ioapic_find_for_irq(uint32_t global_irq);
static uint32_t ioapic_apply_overrides(uint8_t irq);
static int ioapic_setup_legacy_irqs(void);

/*
 * Read I/O APIC register
 */
static uint32_t ioapic_read_register(struct ioapic_device *ioapic, uint8_t reg)
{
    ioapic->base_addr[IOAPIC_REG_SELECT] = reg;
    return ioapic->base_addr[IOAPIC_REG_WINDOW / 4];
}

/*
 * Write I/O APIC register
 */
static void ioapic_write_register(struct ioapic_device *ioapic, uint8_t reg, uint32_t value)
{
    ioapic->base_addr[IOAPIC_REG_SELECT] = reg;
    ioapic->base_addr[IOAPIC_REG_WINDOW / 4] = value;
}

/*
 * Read redirection table entry
 */
static ioapic_redtbl_entry_t ioapic_read_redtbl(struct ioapic_device *ioapic, uint8_t irq)
{
    ioapic_redtbl_entry_t entry;
    
    entry.low = ioapic_read_register(ioapic, IOAPIC_REGSEL_REDTBL_BASE + (irq * 2));
    entry.high = ioapic_read_register(ioapic, IOAPIC_REGSEL_REDTBL_BASE + (irq * 2) + 1);
    
    return entry;
}

/*
 * Write redirection table entry
 */
static void ioapic_write_redtbl(struct ioapic_device *ioapic, uint8_t irq, ioapic_redtbl_entry_t entry)
{
    ioapic_write_register(ioapic, IOAPIC_REGSEL_REDTBL_BASE + (irq * 2), entry.low);
    ioapic_write_register(ioapic, IOAPIC_REGSEL_REDTBL_BASE + (irq * 2) + 1, entry.high);
}

/*
 * Initialize individual I/O APIC device
 */
static int ioapic_init_device(struct ioapic_device *ioapic)
{
    uint32_t version_reg;
    unsigned long flags;
    
    if (!ioapic || ioapic->present) {
        return -1;
    }
    
    /* Map I/O APIC registers */
    ioapic->base_addr = (uint32_t *)mm_map_physical_page(ioapic->physical_addr,
                                                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    if (!ioapic->base_addr) {
        debuglog_printf("I/O APIC: Failed to map registers at 0x%08x\n", ioapic->physical_addr);
        return -1;
    }
    
    spin_lock_irqsave(&ioapic->lock, flags);
    
    /* Read and verify I/O APIC version */
    version_reg = ioapic_read_register(ioapic, IOAPIC_REGSEL_VERSION);
    ioapic->version = version_reg & 0xFF;
    ioapic->max_redirections = ((version_reg >> 16) & 0xFF) + 1;
    
    if (ioapic->version == 0xFF || ioapic->max_redirections == 0) {
        spin_unlock_irqrestore(&ioapic->lock, flags);
        debuglog_printf("I/O APIC: Invalid version or redirection count\n");
        return -1;
    }
    
    /* Set I/O APIC ID */
    uint32_t id_reg = (ioapic->id << 24);
    ioapic_write_register(ioapic, IOAPIC_REGSEL_ID, id_reg);
    
    /* Mask all interrupts initially */
    for (uint32_t i = 0; i < ioapic->max_redirections; i++) {
        ioapic_redtbl_entry_t entry = {0};
        entry.low = IOAPIC_MASK;  /* Masked */
        ioapic_write_redtbl(ioapic, i, entry);
    }
    
    /* Clear interrupt counters */
    for (uint32_t i = 0; i < 24; i++) {
        atomic64_set(&ioapic->interrupt_count[i], 0);
    }
    
    ioapic->present = true;
    
    spin_unlock_irqrestore(&ioapic->lock, flags);
    
    debuglog_printf("I/O APIC: Initialized ID=%d, version=0x%02x, IRQs=%d, base=0x%08x\n",
                ioapic->id, ioapic->version, ioapic->max_redirections, ioapic->physical_addr);
    
    return 0;
}

/*
 * Discover I/O APIC devices from ACPI MADT
 */
static int ioapic_discover_devices(void)
{
    const void *madt_ptr;
    const uint8_t *ptr, *end;
    
    debuglog_printf("I/O APIC: Discovering devices from ACPI MADT\n");
    
    /* Get MADT from ACPI - this needs to be implemented */
    /* For now, we'll use a placeholder that would call the ACPI subsystem */
    /* madt_ptr = acpi_get_madt(); */
    madt_ptr = NULL;  /* Placeholder until ACPI is fully implemented */
    
    if (!madt_ptr) {
        debuglog_printf("I/O APIC: MADT not available, using default configuration\n");
        
        /* Set up a default I/O APIC configuration for systems without ACPI */
        struct ioapic_device *ioapic = &ioapic_state.ioapics[0];
        ioapic->id = 0;
        ioapic->physical_addr = 0xFEC00000;  /* Standard I/O APIC address */
        ioapic->global_irq_base = 0;
        ioapic->present = false;
        spinlock_init(&ioapic->lock, "ioapic");
        
        if (ioapic_init_device(ioapic) == 0) {
            ioapic_state.num_ioapics = 1;
            ioapic_state.total_irqs += ioapic->max_redirections;
            return 0;
        }
        
        return -1;
    }
    
    /* Parse MADT entries */
    /* This code would parse the actual MADT structure */
    /* Note: When ACPI is fully implemented, use actual MADT structure size */
    ptr = (const uint8_t *)madt_ptr + 44;  /* sizeof(acpi_madt_t) header */
    end = (const uint8_t *)madt_ptr + 44;  /* Would be madt->header.length */
    
    while (ptr + sizeof(acpi_madt_entry_header_t) <= end) {
        const acpi_madt_entry_header_t *hdr = (const acpi_madt_entry_header_t *)ptr;
        
        if (hdr->length < sizeof(acpi_madt_entry_header_t)) {
            break;
        }
        
        switch (hdr->type) {
            case MADT_ENTRY_TYPE_IOAPIC: {
                if (hdr->length >= sizeof(acpi_madt_ioapic_t) && 
                    ioapic_state.num_ioapics < 8) {
                    
                    const acpi_madt_ioapic_t *ioapic_entry = (const acpi_madt_ioapic_t *)ptr;
                    struct ioapic_device *ioapic = &ioapic_state.ioapics[ioapic_state.num_ioapics];
                    
                    ioapic->id = ioapic_entry->ioapic_id;
                    ioapic->physical_addr = ioapic_entry->ioapic_address;
                    ioapic->global_irq_base = ioapic_entry->global_irq_base;
                    ioapic->present = false;
                    spinlock_init(&ioapic->lock, "ioapic");
                    
                    if (ioapic_init_device(ioapic) == 0) {
                        ioapic_state.num_ioapics++;
                        ioapic_state.total_irqs += ioapic->max_redirections;
                    }
                }
                break;
            }
            
            case MADT_ENTRY_TYPE_OVERRIDE: {
                if (hdr->length >= sizeof(acpi_madt_override_t) && 
                    ioapic_state.num_overrides < 32) {
                    
                    const acpi_madt_override_t *override = (const acpi_madt_override_t *)ptr;
                    struct irq_override *ovr = &ioapic_state.overrides[ioapic_state.num_overrides];
                    
                    ovr->source_irq = override->source_irq;
                    ovr->global_irq = override->global_irq;
                    ovr->flags = override->flags;
                    ovr->active = true;
                    
                    ioapic_state.num_overrides++;
                    
                    debuglog_printf("I/O APIC: IRQ override %d -> %d (flags=0x%04x)\n",
                               override->source_irq, override->global_irq, override->flags);
                }
                break;
            }
            
            default:
                break;
        }
        
        ptr += hdr->length;
    }
    
    return ioapic_state.num_ioapics > 0 ? 0 : -1;
}

/*
 * Find I/O APIC responsible for a global IRQ
 */
static struct ioapic_device *ioapic_find_for_irq(uint32_t global_irq)
{
    for (int i = 0; i < ioapic_state.num_ioapics; i++) {
        struct ioapic_device *ioapic = &ioapic_state.ioapics[i];
        
        if (ioapic->present && 
            global_irq >= ioapic->global_irq_base &&
            global_irq < (ioapic->global_irq_base + ioapic->max_redirections)) {
            return ioapic;
        }
    }
    
    return NULL;
}

/*
 * Apply IRQ overrides to convert ISA IRQ to global IRQ
 */
static uint32_t ioapic_apply_overrides(uint8_t irq)
{
    for (int i = 0; i < ioapic_state.num_overrides; i++) {
        struct irq_override *ovr = &ioapic_state.overrides[i];
        
        if (ovr->active && ovr->source_irq == irq) {
            return ovr->global_irq;
        }
    }
    
    /* No override found, use direct mapping */
    return irq;
}

/*
 * Set up legacy ISA IRQ mappings
 */
static int ioapic_setup_legacy_irqs(void)
{
    debuglog_printf("I/O APIC: Setting up legacy IRQ mappings\n");
    
    /* Map legacy ISA IRQs (0-15) to appropriate vectors */
    for (uint8_t irq = 0; irq < 16; irq++) {
        uint32_t global_irq = ioapic_apply_overrides(irq);
        struct ioapic_device *ioapic = ioapic_find_for_irq(global_irq);
        
        if (ioapic) {
            ioapic_redtbl_entry_t entry = {0};
            uint32_t local_irq = global_irq - ioapic->global_irq_base;
            
            /* Configure redirection entry */
            entry.low = (IRQ_BASE + irq) |           /* Vector */
                       IOAPIC_DELMODE_FIXED |        /* Fixed delivery */
                       IOAPIC_DEST_PHYSICAL |        /* Physical destination */
                       IOAPIC_INTPOL_ACTIVE_HIGH |   /* Active high (default for ISA) */
                       IOAPIC_TRIGGER_EDGE;          /* Edge triggered (default for ISA) */
            
            /* Apply polarity and trigger overrides */
            for (int i = 0; i < ioapic_state.num_overrides; i++) {
                struct irq_override *ovr = &ioapic_state.overrides[i];
                if (ovr->active && ovr->source_irq == irq) {
                    if (ovr->flags & 0x02) {  /* Active low */
                        entry.low &= ~IOAPIC_INTPOL_ACTIVE_HIGH;
                        entry.low |= IOAPIC_INTPOL_ACTIVE_LOW;
                    }
                    if (ovr->flags & 0x08) {  /* Level triggered */
                        entry.low &= ~IOAPIC_TRIGGER_EDGE;
                        entry.low |= IOAPIC_TRIGGER_LEVEL;
                    }
                    break;
                }
            }
            
            /* Set destination to BSP initially */
            uint32_t bsp_apic_id = apic_get_id();
            entry.high = (bsp_apic_id << 24);
            
            /* Mask the interrupt initially */
            entry.low |= IOAPIC_MASK;
            
            ioapic_write_redtbl(ioapic, local_irq, entry);
            
            debuglog_printf("I/O APIC: Mapped IRQ %d -> global %d (I/O APIC %d:%d)\n",
                       irq, global_irq, ioapic->id, local_irq);
        }
    }
    
    return 0;
}

/*
 * Initialize I/O APIC subsystem
 */
int ioapic_init_advanced(void)
{
    int ret;
    
    debuglog_printf("I/O APIC: Initializing I/O APIC subsystem\n");
    
    if (ioapic_state.initialized) {
        debuglog_printf("I/O APIC: Already initialized\n");
        return 0;
    }
    
    if (!apic_is_available()) {
        debuglog_printf("I/O APIC: Local APIC not available\n");
        return -1;
    }
    
    /* Clear state */
    memset(&ioapic_state, 0, sizeof(ioapic_state));
    spinlock_init(&ioapic_state.global_lock, "ioapic_global");
    
    /* Discover I/O APIC devices */
    ret = ioapic_discover_devices();
    if (ret != 0) {
        debuglog_printf("I/O APIC: No I/O APIC devices found\n");
        return ret;
    }
    
    /* Set up legacy IRQ mappings */
    ret = ioapic_setup_legacy_irqs();
    if (ret != 0) {
        debuglog_printf("I/O APIC: Failed to set up legacy IRQs\n");
        return ret;
    }
    
    ioapic_state.initialized = true;
    
    debuglog_printf("I/O APIC: Initialization complete (%d I/O APICs, %d total IRQs)\n",
                ioapic_state.num_ioapics, ioapic_state.total_irqs);
    
    return 0;
}

/*
 * Enable IRQ on I/O APIC
 */
int ioapic_enable_irq(uint8_t irq)
{
    uint32_t global_irq = ioapic_apply_overrides(irq);
    struct ioapic_device *ioapic = ioapic_find_for_irq(global_irq);
    
    if (!ioapic) {
        debuglog_printf("I/O APIC: No I/O APIC found for IRQ %d\n", irq);
        return -1;
    }
    
    uint32_t local_irq = global_irq - ioapic->global_irq_base;
    unsigned long flags;
    
    spin_lock_irqsave(&ioapic->lock, flags);
    
    /* Read current redirection entry */
    ioapic_redtbl_entry_t entry = ioapic_read_redtbl(ioapic, local_irq);
    
    /* Unmask the interrupt */
    entry.low &= ~IOAPIC_MASK;
    
    /* Write back the entry */
    ioapic_write_redtbl(ioapic, local_irq, entry);
    
    spin_unlock_irqrestore(&ioapic->lock, flags);
    
    debuglog_printf("I/O APIC: Enabled IRQ %d (global %d, I/O APIC %d:%d)\n",
                irq, global_irq, ioapic->id, local_irq);
    
    return 0;
}

/*
 * Disable IRQ on I/O APIC
 */
int ioapic_disable_irq(uint8_t irq)
{
    uint32_t global_irq = ioapic_apply_overrides(irq);
    struct ioapic_device *ioapic = ioapic_find_for_irq(global_irq);
    
    if (!ioapic) {
        return -1;
    }
    
    uint32_t local_irq = global_irq - ioapic->global_irq_base;
    unsigned long flags;
    
    spin_lock_irqsave(&ioapic->lock, flags);
    
    /* Read current redirection entry */
    ioapic_redtbl_entry_t entry = ioapic_read_redtbl(ioapic, local_irq);
    
    /* Mask the interrupt */
    entry.low |= IOAPIC_MASK;
    
    /* Write back the entry */
    ioapic_write_redtbl(ioapic, local_irq, entry);
    
    spin_unlock_irqrestore(&ioapic->lock, flags);
    
    debuglog_printf("I/O APIC: Disabled IRQ %d\n", irq);
    
    return 0;
}

/*
 * Set IRQ affinity (target CPU)
 */
int ioapic_set_affinity(uint8_t irq, uint32_t target_apic_id)
{
    uint32_t global_irq = ioapic_apply_overrides(irq);
    struct ioapic_device *ioapic = ioapic_find_for_irq(global_irq);
    
    if (!ioapic) {
        return -1;
    }
    
    uint32_t local_irq = global_irq - ioapic->global_irq_base;
    unsigned long flags;
    
    spin_lock_irqsave(&ioapic->lock, flags);
    
    /* Read current redirection entry */
    ioapic_redtbl_entry_t entry = ioapic_read_redtbl(ioapic, local_irq);
    
    /* Set new destination */
    entry.high = (target_apic_id << 24);
    
    /* Write back the entry */
    ioapic_write_redtbl(ioapic, local_irq, entry);
    
    spin_unlock_irqrestore(&ioapic->lock, flags);
    
    debuglog_printf("I/O APIC: Set IRQ %d affinity to APIC ID %d\n", irq, target_apic_id);
    
    return 0;
}

/*
 * Get I/O APIC statistics
 */
void ioapic_get_stats(struct ioapic_stats *stats)
{
    if (!stats || !ioapic_state.initialized) {
        return;
    }
    
    stats->initialized = ioapic_state.initialized;
    stats->num_ioapics = ioapic_state.num_ioapics;
    stats->total_irqs = ioapic_state.total_irqs;
    stats->num_overrides = ioapic_state.num_overrides;
    stats->total_interrupts = atomic64_read(&ioapic_state.total_interrupts);
    
    /* Copy device information */
    for (int i = 0; i < ioapic_state.num_ioapics && i < 8; i++) {
        struct ioapic_device *ioapic = &ioapic_state.ioapics[i];
        
        stats->devices[i].present = ioapic->present;
        stats->devices[i].id = ioapic->id;
        stats->devices[i].physical_addr = ioapic->physical_addr;
        stats->devices[i].global_irq_base = ioapic->global_irq_base;
        stats->devices[i].max_redirections = ioapic->max_redirections;
        stats->devices[i].version = ioapic->version;
    }
}

/*
 * Check if I/O APIC is available
 */
bool ioapic_is_available(void)
{
    return ioapic_state.initialized && ioapic_state.num_ioapics > 0;
}

/*
 * Dump I/O APIC debug information
 */
void ioapic_debug_dump(void)
{
    if (!ioapic_state.initialized) {
        debuglog_printf("I/O APIC: Not initialized\n");
        return;
    }
    
    debuglog_printf("\n=== I/O APIC DEBUG INFORMATION ===\n");
    debuglog_printf("I/O APICs: %d, Total IRQs: %d, Overrides: %d\n",
                ioapic_state.num_ioapics, ioapic_state.total_irqs, ioapic_state.num_overrides);
    
    for (int i = 0; i < ioapic_state.num_ioapics; i++) {
        struct ioapic_device *ioapic = &ioapic_state.ioapics[i];
        
        debuglog_printf("I/O APIC %d: ID=%d, addr=0x%08x, IRQ base=%d, max=%d, ver=0x%02x\n",
                   i, ioapic->id, ioapic->physical_addr, ioapic->global_irq_base,
                   ioapic->max_redirections, ioapic->version);
    }
    
    if (ioapic_state.num_overrides > 0) {
        debuglog_printf("IRQ Overrides:\n");
        for (int i = 0; i < ioapic_state.num_overrides; i++) {
            struct irq_override *ovr = &ioapic_state.overrides[i];
            debuglog_printf("  %d -> %d (flags=0x%04x)\n", 
                       ovr->source_irq, ovr->global_irq, ovr->flags);
        }
    }
    
    debuglog_printf("=== END I/O APIC DEBUG ===\n\n");
}