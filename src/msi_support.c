/*
 * msi_support.c - Message Signaled Interrupts (MSI/MSI-X) Support for Forest OS
 * 
 * This module provides:
 * - MSI and MSI-X capability detection and configuration
 * - MSI vector allocation and management
 * - MSI address and data programming
 * - MSI interrupt handling and dispatching
 * - Integration with PCI subsystem and interrupt management
 * - MSI-X extended features and per-vector configuration
 * 
 * Message Signaled Interrupts provide a modern alternative to traditional
 * line-based interrupts, offering better scalability, performance, and
 * elimination of interrupt sharing issues.
 * 
 * Copyright (c) 2024 Forest OS Project
 */

#include "interrupt.h"
#include "memory.h"
#include "smp.h"
#include "debug.h"
#include "time.h"
#include <string.h>

/* MSI Configuration Registers (from PCI specification) */
#define MSI_CAP_ID              0x05    /* MSI Capability ID */
#define MSIX_CAP_ID             0x11    /* MSI-X Capability ID */

/* MSI Capability Structure Offsets */
#define MSI_CAP_CONTROL         0x02    /* Message Control */
#define MSI_CAP_ADDRESS_LO      0x04    /* Message Address (lower 32 bits) */
#define MSI_CAP_ADDRESS_HI      0x08    /* Message Address (upper 32 bits) */
#define MSI_CAP_DATA            0x0C    /* Message Data (16 bits) */
#define MSI_CAP_MASK            0x10    /* Mask Bits (optional) */
#define MSI_CAP_PENDING         0x14    /* Pending Bits (optional) */

/* MSI Control Register Bits */
#define MSI_CTRL_ENABLE         (1 << 0)   /* MSI Enable */
#define MSI_CTRL_MULTI_CAP      (7 << 1)   /* Multiple Message Capable */
#define MSI_CTRL_MULTI_EN       (7 << 4)   /* Multiple Message Enable */
#define MSI_CTRL_64BIT          (1 << 7)   /* 64-bit Address Capable */
#define MSI_CTRL_PVM            (1 << 8)   /* Per-Vector Masking Capable */

/* MSI-X Capability Structure Offsets */
#define MSIX_CAP_CONTROL        0x02    /* Message Control */
#define MSIX_CAP_TABLE          0x04    /* Table Offset/BIR */
#define MSIX_CAP_PBA            0x08    /* Pending Bit Array Offset/BIR */

/* MSI-X Control Register Bits */
#define MSIX_CTRL_ENABLE        (1 << 15)  /* MSI-X Enable */
#define MSIX_CTRL_FMASK         (1 << 14)  /* Function Mask */
#define MSIX_CTRL_TABLE_SIZE    (0x7FF)    /* Table Size Mask */

/* MSI-X Table Entry Structure */
struct msix_table_entry {
    uint32_t msg_addr_lo;       /* Message Address (lower 32 bits) */
    uint32_t msg_addr_hi;       /* Message Address (upper 32 bits) */
    uint32_t msg_data;          /* Message Data */
    uint32_t vector_control;    /* Vector Control (bit 0 = mask) */
} __attribute__((packed));

/* PCI Device MSI Information */
struct pci_msi_device {
    /* Device identification */
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    
    /* MSI capability information */
    bool msi_capable;
    bool msix_capable;
    uint8_t msi_cap_offset;
    uint8_t msix_cap_offset;
    
    /* MSI configuration */
    struct {
        bool enabled;
        bool addr_64bit;
        bool per_vector_masking;
        uint8_t multi_message_capable;
        uint8_t multi_message_enable;
        uint32_t base_vector;
        uint32_t num_vectors;
        uint64_t address;
        uint16_t data;
    } msi;
    
    /* MSI-X configuration */
    struct {
        bool enabled;
        uint16_t table_size;
        uint32_t table_offset;
        uint32_t pba_offset;
        uint8_t table_bir;
        uint8_t pba_bir;
        struct msix_table_entry *table;
        uint32_t *pba;
        uint32_t num_vectors_allocated;
    } msix;
    
    /* Interrupt handling */
    interrupt_handler_t *handlers;
    void **handler_data;
    uint64_t *interrupt_counts;
    
    /* Linked list for device tracking */
    struct pci_msi_device *next;
};

/* MSI Vector Management */
struct msi_vector_info {
    bool allocated;
    bool enabled;
    struct pci_msi_device *device;
    uint32_t device_vector;     /* Vector index within device */
    interrupt_handler_t handler;
    void *handler_data;
    uint64_t interrupt_count;
    uint64_t last_interrupt_time;
    const char *name;
};

/* MSI Management Structure */
struct msi_manager {
    /* Vector allocation */
    uint32_t base_vector;           /* Base MSI vector number */
    uint32_t num_vectors;           /* Total MSI vectors available */
    struct msi_vector_info *vectors; /* Vector information array */
    spinlock_t vector_lock;         /* Protects vector allocation */
    
    /* Device management */
    struct pci_msi_device *devices;
    uint32_t num_devices;
    spinlock_t device_lock;
    
    /* MSI address configuration */
    uint64_t msi_address_base;      /* Base address for MSI writes */
    uint32_t msi_data_base;         /* Base data pattern for MSI */
    
    /* Statistics */
    struct {
        uint64_t msi_interrupts;
        uint64_t msix_interrupts;
        uint64_t allocation_requests;
        uint64_t allocation_failures;
        uint32_t peak_allocated_vectors;
        uint32_t current_allocated_vectors;
    } stats;
    
    /* Configuration */
    bool initialized;
    bool msi_supported;
    bool msix_supported;
    bool debug_enabled;
};

static struct msi_manager msi_mgr = {0};

/* MSI Address and Data patterns for x86 */
#define MSI_ADDRESS_BASE        0xFEE00000ULL   /* Local APIC base */
#define MSI_ADDRESS_CPU_MASK    0xFF000         /* CPU targeting mask */
#define MSI_DATA_TRIGGER_EDGE   0x0000          /* Edge triggered */
#define MSI_DATA_LEVEL_ASSERT   0x0000          /* Assert level */
#define MSI_DATA_DELIVERY_FIXED 0x0000          /* Fixed delivery mode */

/* Forward declarations */
static int msi_detect_capability(uint8_t bus, uint8_t device, uint8_t function);
static int msi_allocate_vectors(struct pci_msi_device *dev, uint32_t count);
static void msi_free_vectors(struct pci_msi_device *dev);
static int msi_configure_device(struct pci_msi_device *dev);
static int msix_configure_device(struct pci_msi_device *dev);
static irq_return_t msi_interrupt_handler(int vector, struct interrupt_context *ctx);
static uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
static uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
static void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);

/* ===========================
 * INITIALIZATION
 * =========================== */

/**
 * Initialize MSI/MSI-X support
 */
int msi_support_init(void)
{
    if (msi_mgr.initialized) {
        return 0;
    }
    
    memset(&msi_mgr, 0, sizeof(msi_mgr));
    spinlock_init(&msi_mgr.vector_lock, "msi_vector");
    spinlock_init(&msi_mgr.device_lock, "msi_device");
    
    /* Configure MSI vector range */
    msi_mgr.base_vector = 32;  /* Start after legacy IRQs */
    msi_mgr.num_vectors = 128; /* Support up to 128 MSI vectors */
    
    /* Allocate vector information array */
    msi_mgr.vectors = (struct msi_vector_info *)kmalloc(
        msi_mgr.num_vectors * sizeof(struct msi_vector_info), GFP_KERNEL);
    if (!msi_mgr.vectors) {
        debug_printf("Failed to allocate MSI vector array\n");
        return -ENOMEM;
    }
    
    memset(msi_mgr.vectors, 0, msi_mgr.num_vectors * sizeof(struct msi_vector_info));
    
    /* Set up MSI address and data patterns */
    msi_mgr.msi_address_base = MSI_ADDRESS_BASE;
    msi_mgr.msi_data_base = MSI_DATA_TRIGGER_EDGE | MSI_DATA_LEVEL_ASSERT | MSI_DATA_DELIVERY_FIXED;
    
    /* Check if MSI/MSI-X are supported by the system */
    msi_mgr.msi_supported = true;   /* Assume supported on modern x86 systems */
    msi_mgr.msix_supported = true;
    
    msi_mgr.initialized = true;
    
    debug_printf("MSI/MSI-X support initialized\n");
    debug_printf("Vector range: %u-%u (%u vectors)\n", 
                msi_mgr.base_vector, 
                msi_mgr.base_vector + msi_mgr.num_vectors - 1,
                msi_mgr.num_vectors);
    debug_printf("MSI address base: 0x%llx\n", msi_mgr.msi_address_base);
    
    return 0;
}

/**
 * Cleanup MSI/MSI-X support
 */
void msi_support_cleanup(void)
{
    struct pci_msi_device *dev, *next;
    unsigned long flags;
    
    if (!msi_mgr.initialized) {
        return;
    }
    
    spin_lock_irqsave(&msi_mgr.device_lock, flags);
    
    /* Free all registered devices */
    dev = msi_mgr.devices;
    while (dev) {
        next = dev->next;
        msi_free_vectors(dev);
        kfree(dev);
        dev = next;
    }
    
    msi_mgr.devices = NULL;
    msi_mgr.num_devices = 0;
    
    spin_unlock_irqrestore(&msi_mgr.device_lock, flags);
    
    /* Free vector array */
    if (msi_mgr.vectors) {
        kfree(msi_mgr.vectors);
        msi_mgr.vectors = NULL;
    }
    
    msi_mgr.initialized = false;
    debug_printf("MSI/MSI-X support cleaned up\n");
}

/* ===========================
 * DEVICE DETECTION AND REGISTRATION
 * =========================== */

/**
 * Detect MSI/MSI-X capabilities for a PCI device
 */
static int msi_detect_capability(uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t status_command;
    uint8_t cap_ptr, cap_id;
    bool msi_found = false, msix_found = false;
    uint8_t msi_offset = 0, msix_offset = 0;
    
    /* Check if device supports capabilities */
    status_command = pci_config_read32(bus, device, function, 0x04);
    if (!(status_command & (1 << 20))) {  /* Capabilities bit */
        return -ENODEV;
    }
    
    /* Walk capabilities list */
    cap_ptr = pci_config_read16(bus, device, function, 0x34) & 0xFC;
    
    while (cap_ptr != 0) {
        cap_id = pci_config_read16(bus, device, function, cap_ptr) & 0xFF;
        
        switch (cap_id) {
            case MSI_CAP_ID:
                msi_found = true;
                msi_offset = cap_ptr;
                break;
                
            case MSIX_CAP_ID:
                msix_found = true;
                msix_offset = cap_ptr;
                break;
        }
        
        /* Get next capability */
        cap_ptr = (pci_config_read16(bus, device, function, cap_ptr + 1) >> 8) & 0xFC;
    }
    
    if (!msi_found && !msix_found) {
        return -ENODEV;
    }
    
    if (msi_mgr.debug_enabled) {
        debug_printf("PCI %02x:%02x.%x: MSI=%s MSI-X=%s\n", 
                    bus, device, function,
                    msi_found ? "yes" : "no",
                    msix_found ? "yes" : "no");
    }
    
    return 0; /* Found at least one MSI capability */
}

/**
 * Register a PCI device for MSI/MSI-X support
 */
int msi_register_device(uint8_t bus, uint8_t device, uint8_t function)
{
    struct pci_msi_device *dev;
    unsigned long flags;
    uint16_t vendor_id, device_id;
    uint16_t msi_control, msix_control;
    int ret;
    
    if (!msi_mgr.initialized) {
        return -ENODEV;
    }
    
    /* Check if device supports MSI/MSI-X */
    ret = msi_detect_capability(bus, device, function);
    if (ret < 0) {
        return ret;
    }
    
    /* Get device identification */
    vendor_id = pci_config_read16(bus, device, function, 0x00);
    device_id = pci_config_read16(bus, device, function, 0x02);
    
    /* Allocate device structure */
    dev = (struct pci_msi_device *)kmalloc(sizeof(*dev));
    if (!dev) {
        return -ENOMEM;
    }
    
    memset(dev, 0, sizeof(*dev));
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    
    /* Detect MSI capabilities */
    uint8_t cap_ptr = pci_config_read16(bus, device, function, 0x34) & 0xFC;
    while (cap_ptr != 0) {
        uint8_t cap_id = pci_config_read16(bus, device, function, cap_ptr) & 0xFF;
        
        if (cap_id == MSI_CAP_ID) {
            dev->msi_capable = true;
            dev->msi_cap_offset = cap_ptr;
            
            /* Read MSI control register */
            msi_control = pci_config_read16(bus, device, function, cap_ptr + MSI_CAP_CONTROL);
            dev->msi.addr_64bit = (msi_control & MSI_CTRL_64BIT) != 0;
            dev->msi.per_vector_masking = (msi_control & MSI_CTRL_PVM) != 0;
            dev->msi.multi_message_capable = (msi_control & MSI_CTRL_MULTI_CAP) >> 1;
            
        } else if (cap_id == MSIX_CAP_ID) {
            dev->msix_capable = true;
            dev->msix_cap_offset = cap_ptr;
            
            /* Read MSI-X control register */
            msix_control = pci_config_read16(bus, device, function, cap_ptr + MSIX_CAP_CONTROL);
            dev->msix.table_size = (msix_control & MSIX_CTRL_TABLE_SIZE) + 1;
        }
        
        cap_ptr = (pci_config_read16(bus, device, function, cap_ptr + 1) >> 8) & 0xFC;
    }
    
    /* Add to device list */
    spin_lock_irqsave(&msi_mgr.device_lock, flags);
    dev->next = msi_mgr.devices;
    msi_mgr.devices = dev;
    msi_mgr.num_devices++;
    spin_unlock_irqrestore(&msi_mgr.device_lock, flags);
    
    debug_printf("Registered MSI device: %04x:%04x at %02x:%02x.%x\n",
                vendor_id, device_id, bus, device, function);
    debug_printf("  MSI: %s (multi=%d, 64bit=%s, masking=%s)\n",
                dev->msi_capable ? "yes" : "no",
                1 << dev->msi.multi_message_capable,
                dev->msi.addr_64bit ? "yes" : "no",
                dev->msi.per_vector_masking ? "yes" : "no");
    debug_printf("  MSI-X: %s (vectors=%d)\n",
                dev->msix_capable ? "yes" : "no",
                dev->msix.table_size);
    
    return 0;
}

/* ===========================
 * VECTOR ALLOCATION AND MANAGEMENT
 * =========================== */

/**
 * Allocate MSI vectors for a device
 */
static int msi_allocate_vectors(struct pci_msi_device *dev, uint32_t count)
{
    unsigned long flags;
    uint32_t start_vector = 0;
    uint32_t i, j;
    bool found = false;
    
    if (!dev || count == 0 || count > msi_mgr.num_vectors) {
        return -EINVAL;
    }
    
    spin_lock_irqsave(&msi_mgr.vector_lock, flags);
    
    /* Find contiguous block of free vectors */
    for (i = 0; i <= msi_mgr.num_vectors - count; i++) {
        bool block_free = true;
        
        for (j = 0; j < count; j++) {
            if (msi_mgr.vectors[i + j].allocated) {
                block_free = false;
                break;
            }
        }
        
        if (block_free) {
            start_vector = i;
            found = true;
            break;
        }
    }
    
    if (!found) {
        spin_unlock_irqrestore(&msi_mgr.vector_lock, flags);
        msi_mgr.stats.allocation_failures++;
        return -ENOSPC;
    }
    
    /* Allocate the vectors */
    for (i = 0; i < count; i++) {
        struct msi_vector_info *vec = &msi_mgr.vectors[start_vector + i];
        vec->allocated = true;
        vec->device = dev;
        vec->device_vector = i;
        vec->interrupt_count = 0;
        vec->last_interrupt_time = 0;
    }
    
    msi_mgr.stats.current_allocated_vectors += count;
    if (msi_mgr.stats.current_allocated_vectors > msi_mgr.stats.peak_allocated_vectors) {
        msi_mgr.stats.peak_allocated_vectors = msi_mgr.stats.current_allocated_vectors;
    }
    
    spin_unlock_irqrestore(&msi_mgr.vector_lock, flags);
    
    dev->msi.base_vector = msi_mgr.base_vector + start_vector;
    dev->msi.num_vectors = count;
    msi_mgr.stats.allocation_requests++;
    
    debug_printf("Allocated %u MSI vectors starting at %u for device %04x:%04x\n",
                count, dev->msi.base_vector, dev->vendor_id, dev->device_id);
    
    return 0;
}

/**
 * Free MSI vectors for a device
 */
static void msi_free_vectors(struct pci_msi_device *dev)
{
    unsigned long flags;
    uint32_t start_idx, i;
    
    if (!dev || dev->msi.num_vectors == 0) {
        return;
    }
    
    start_idx = dev->msi.base_vector - msi_mgr.base_vector;
    
    spin_lock_irqsave(&msi_mgr.vector_lock, flags);
    
    /* Free the vectors */
    for (i = 0; i < dev->msi.num_vectors; i++) {
        struct msi_vector_info *vec = &msi_mgr.vectors[start_idx + i];
        
        if (vec->allocated && vec->device == dev) {
            vec->allocated = false;
            vec->enabled = false;
            vec->device = NULL;
            vec->handler = NULL;
            vec->handler_data = NULL;
            vec->name = NULL;
            
            msi_mgr.stats.current_allocated_vectors--;
        }
    }
    
    spin_unlock_irqrestore(&msi_mgr.vector_lock, flags);
    
    dev->msi.base_vector = 0;
    dev->msi.num_vectors = 0;
    
    debug_printf("Freed MSI vectors for device %04x:%04x\n",
                dev->vendor_id, dev->device_id);
}

/* ===========================
 * MSI CONFIGURATION
 * =========================== */

/**
 * Configure MSI for a device
 */
static int msi_configure_device(struct pci_msi_device *dev)
{
    uint16_t msi_control;
    uint64_t msi_address;
    uint16_t msi_data;
    uint8_t offset;
    
    if (!dev || !dev->msi_capable || dev->msi.num_vectors == 0) {
        return -EINVAL;
    }
    
    offset = dev->msi_cap_offset;
    
    /* Calculate MSI address and data */
    msi_address = msi_mgr.msi_address_base;
    msi_data = msi_mgr.msi_data_base | dev->msi.base_vector;
    
    /* Disable MSI before configuration */
    msi_control = pci_config_read16(dev->bus, dev->device, dev->function, offset + MSI_CAP_CONTROL);
    msi_control &= ~MSI_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->device, dev->function, offset + MSI_CAP_CONTROL, msi_control);
    
    /* Configure address */
    pci_config_write32(dev->bus, dev->device, dev->function, offset + MSI_CAP_ADDRESS_LO, 
                      (uint32_t)(msi_address & 0xFFFFFFFF));
    
    if (dev->msi.addr_64bit) {
        pci_config_write32(dev->bus, dev->device, dev->function, offset + MSI_CAP_ADDRESS_HI,
                          (uint32_t)(msi_address >> 32));
        /* Data offset is different for 64-bit addressing */
        pci_config_write16(dev->bus, dev->device, dev->function, offset + MSI_CAP_DATA + 4, msi_data);
    } else {
        pci_config_write16(dev->bus, dev->device, dev->function, offset + MSI_CAP_DATA, msi_data);
    }
    
    /* Configure multiple message enable */
    msi_control &= ~MSI_CTRL_MULTI_EN;
    if (dev->msi.num_vectors > 1) {
        uint8_t multi_enable = 0;
        uint32_t count = dev->msi.num_vectors;
        while (count > 1) {
            multi_enable++;
            count >>= 1;
        }
        msi_control |= (multi_enable << 4);
    }
    
    /* Enable MSI */
    msi_control |= MSI_CTRL_ENABLE;
    pci_config_write16(dev->bus, dev->device, dev->function, offset + MSI_CAP_CONTROL, msi_control);
    
    dev->msi.enabled = true;
    dev->msi.address = msi_address;
    dev->msi.data = msi_data;
    
    debug_printf("Configured MSI for device %04x:%04x: addr=0x%llx, data=0x%x, vectors=%u\n",
                dev->vendor_id, dev->device_id, msi_address, msi_data, dev->msi.num_vectors);
    
    return 0;
}

/**
 * Configure MSI-X for a device
 */
static int msix_configure_device(struct pci_msi_device *dev)
{
    uint16_t msix_control;
    uint8_t offset;
    
    if (!dev || !dev->msix_capable) {
        return -EINVAL;
    }
    
    offset = dev->msix_cap_offset;
    
    /* Read table and PBA information */
    uint32_t table_reg = pci_config_read32(dev->bus, dev->device, dev->function, offset + MSIX_CAP_TABLE);
    uint32_t pba_reg = pci_config_read32(dev->bus, dev->device, dev->function, offset + MSIX_CAP_PBA);
    
    dev->msix.table_offset = table_reg & ~0x7;
    dev->msix.table_bir = table_reg & 0x7;
    dev->msix.pba_offset = pba_reg & ~0x7;
    dev->msix.pba_bir = pba_reg & 0x7;
    
    /* For now, we'll assume the table and PBA are memory-mapped */
    /* In a real implementation, we would map the BARs */
    
    /* Enable MSI-X but keep function masked initially */
    msix_control = pci_config_read16(dev->bus, dev->device, dev->function, offset + MSIX_CAP_CONTROL);
    msix_control |= MSIX_CTRL_ENABLE | MSIX_CTRL_FMASK;
    pci_config_write16(dev->bus, dev->device, dev->function, offset + MSIX_CAP_CONTROL, msix_control);
    
    dev->msix.enabled = true;
    
    debug_printf("Configured MSI-X for device %04x:%04x: table_size=%u, table_offset=0x%x\n",
                dev->vendor_id, dev->device_id, dev->msix.table_size, dev->msix.table_offset);
    
    return 0;
}

/* ===========================
 * INTERRUPT HANDLING
 * =========================== */

/**
 * MSI interrupt handler
 */
static irq_return_t msi_interrupt_handler(int vector, struct interrupt_context *ctx)
{
    uint32_t vector_idx;
    struct msi_vector_info *vec_info;
    irq_return_t result = IRQ_NONE;
    
    if (vector < msi_mgr.base_vector || 
        vector >= msi_mgr.base_vector + msi_mgr.num_vectors) {
        return IRQ_NONE;
    }
    
    vector_idx = vector - msi_mgr.base_vector;
    vec_info = &msi_mgr.vectors[vector_idx];
    
    if (!vec_info->allocated || !vec_info->enabled) {
        return IRQ_NONE;
    }
    
    /* Update statistics */
    vec_info->interrupt_count++;
    vec_info->last_interrupt_time = get_system_time_ns();
    
    if (vec_info->device->msix_capable && vec_info->device->msix.enabled) {
        msi_mgr.stats.msix_interrupts++;
    } else {
        msi_mgr.stats.msi_interrupts++;
    }
    
    /* Call device-specific handler */
    if (vec_info->handler) {
        result = vec_info->handler(vector, ctx);
    }
    
    return result;
}

/* ===========================
 * PUBLIC API
 * =========================== */

/**
 * Request MSI vectors for a device
 */
int msi_request_vectors(uint8_t bus, uint8_t device, uint8_t function, 
                       uint32_t min_vectors, uint32_t max_vectors,
                       interrupt_handler_t handler, void *data, const char *name)
{
    struct pci_msi_device *dev = NULL;
    unsigned long flags;
    uint32_t allocated_vectors;
    int ret;
    uint32_t i;
    
    if (!msi_mgr.initialized || !handler) {
        return -EINVAL;
    }
    
    /* Find the device */
    spin_lock_irqsave(&msi_mgr.device_lock, flags);
    dev = msi_mgr.devices;
    while (dev) {
        if (dev->bus == bus && dev->device == device && dev->function == function) {
            break;
        }
        dev = dev->next;
    }
    spin_unlock_irqrestore(&msi_mgr.device_lock, flags);
    
    if (!dev) {
        debug_printf("Device %02x:%02x.%x not registered for MSI\n", bus, device, function);
        return -ENODEV;
    }
    
    if (!dev->msi_capable && !dev->msix_capable) {
        return -ENOTSUP;
    }
    
    /* Try to allocate maximum vectors first, then fall back */
    allocated_vectors = max_vectors;
    ret = msi_allocate_vectors(dev, allocated_vectors);
    
    while (ret < 0 && allocated_vectors >= min_vectors) {
        allocated_vectors /= 2;
        if (allocated_vectors < min_vectors) {
            allocated_vectors = min_vectors;
        }
        ret = msi_allocate_vectors(dev, allocated_vectors);
    }
    
    if (ret < 0) {
        debug_printf("Failed to allocate MSI vectors for device %02x:%02x.%x\n", 
                    bus, device, function);
        return ret;
    }
    
    /* Set up interrupt handlers */
    for (i = 0; i < allocated_vectors; i++) {
        uint32_t vector_idx = (dev->msi.base_vector - msi_mgr.base_vector) + i;
        struct msi_vector_info *vec_info = &msi_mgr.vectors[vector_idx];
        
        vec_info->handler = handler;
        vec_info->handler_data = data;
        vec_info->name = name;
        vec_info->enabled = true;
        
        /* Register with interrupt system */
        ret = request_irq(dev->msi.base_vector + i, msi_interrupt_handler,
                         IRQF_SHARED, name, vec_info);
        if (ret < 0) {
            debug_printf("Failed to register MSI interrupt %u\n", dev->msi.base_vector + i);
            /* Clean up partial registration */
            for (uint32_t j = 0; j < i; j++) {
                free_irq_extended(dev->msi.base_vector + j, &msi_mgr.vectors[(dev->msi.base_vector - msi_mgr.base_vector) + j]);
            }
            msi_free_vectors(dev);
            return ret;
        }
    }
    
    /* Configure the device */
    if (dev->msix_capable && allocated_vectors > 1) {
        ret = msix_configure_device(dev);
    } else if (dev->msi_capable) {
        ret = msi_configure_device(dev);
    } else {
        ret = -ENOTSUP;
    }
    
    if (ret < 0) {
        debug_printf("Failed to configure MSI for device %02x:%02x.%x\n", 
                    bus, device, function);
        /* Clean up */
        for (i = 0; i < allocated_vectors; i++) {
            free_irq_extended(dev->msi.base_vector + i, &msi_mgr.vectors[(dev->msi.base_vector - msi_mgr.base_vector) + i]);
        }
        msi_free_vectors(dev);
        return ret;
    }
    
    debug_printf("Successfully configured %u MSI vectors for device %04x:%04x\n",
                allocated_vectors, dev->vendor_id, dev->device_id);
    
    return allocated_vectors;
}

/**
 * Free MSI vectors for a device
 */
void msi_free_device_vectors(uint8_t bus, uint8_t device, uint8_t function)
{
    struct pci_msi_device *dev = NULL;
    unsigned long flags;
    uint32_t i;
    
    if (!msi_mgr.initialized) {
        return;
    }
    
    /* Find the device */
    spin_lock_irqsave(&msi_mgr.device_lock, flags);
    dev = msi_mgr.devices;
    while (dev) {
        if (dev->bus == bus && dev->device == device && dev->function == function) {
            break;
        }
        dev = dev->next;
    }
    spin_unlock_irqrestore(&msi_mgr.device_lock, flags);
    
    if (!dev || dev->msi.num_vectors == 0) {
        return;
    }
    
    /* Disable MSI/MSI-X */
    if (dev->msi.enabled) {
        uint16_t msi_control = pci_config_read16(dev->bus, dev->device, dev->function, 
                                               dev->msi_cap_offset + MSI_CAP_CONTROL);
        msi_control &= ~MSI_CTRL_ENABLE;
        pci_config_write16(dev->bus, dev->device, dev->function, 
                          dev->msi_cap_offset + MSI_CAP_CONTROL, msi_control);
        dev->msi.enabled = false;
    }
    
    if (dev->msix.enabled) {
        uint16_t msix_control = pci_config_read16(dev->bus, dev->device, dev->function,
                                                dev->msix_cap_offset + MSIX_CAP_CONTROL);
        msix_control &= ~MSIX_CTRL_ENABLE;
        pci_config_write16(dev->bus, dev->device, dev->function,
                          dev->msix_cap_offset + MSIX_CAP_CONTROL, msix_control);
        dev->msix.enabled = false;
    }
    
    /* Unregister interrupt handlers */
    for (i = 0; i < dev->msi.num_vectors; i++) {
        uint32_t vector_idx = (dev->msi.base_vector - msi_mgr.base_vector) + i;
        free_irq_extended(dev->msi.base_vector + i, &msi_mgr.vectors[vector_idx]);
    }
    
    /* Free vectors */
    msi_free_vectors(dev);
    
    debug_printf("Freed MSI vectors for device %04x:%04x\n",
                dev->vendor_id, dev->device_id);
}

/* ===========================
 * STATISTICS AND DEBUGGING
 * =========================== */

/**
 * Get MSI statistics
 */
void msi_get_statistics(struct msi_manager *stats)
{
    unsigned long flags;
    
    if (!msi_mgr.initialized || !stats) {
        return;
    }
    
    spin_lock_irqsave(&msi_mgr.vector_lock, flags);
    memcpy(stats, &msi_mgr, sizeof(*stats));
    spin_unlock_irqrestore(&msi_mgr.vector_lock, flags);
}

/**
 * Dump MSI state for debugging
 */
void msi_dump_state(void)
{
    struct pci_msi_device *dev;
    unsigned long flags;
    uint32_t allocated_count = 0;
    
    if (!msi_mgr.initialized) {
        debug_printf("MSI support not initialized\n");
        return;
    }
    
    debug_printf("=== MSI/MSI-X State ===\n");
    debug_printf("Vector range: %u-%u (%u total)\n", 
                msi_mgr.base_vector, msi_mgr.base_vector + msi_mgr.num_vectors - 1,
                msi_mgr.num_vectors);
    debug_printf("MSI address base: 0x%llx\n", msi_mgr.msi_address_base);
    debug_printf("MSI data base: 0x%x\n", msi_mgr.msi_data_base);
    
    debug_printf("\nStatistics:\n");
    debug_printf("  MSI interrupts: %llu\n", msi_mgr.stats.msi_interrupts);
    debug_printf("  MSI-X interrupts: %llu\n", msi_mgr.stats.msix_interrupts);
    debug_printf("  Allocation requests: %llu\n", msi_mgr.stats.allocation_requests);
    debug_printf("  Allocation failures: %llu\n", msi_mgr.stats.allocation_failures);
    debug_printf("  Current allocated: %u\n", msi_mgr.stats.current_allocated_vectors);
    debug_printf("  Peak allocated: %u\n", msi_mgr.stats.peak_allocated_vectors);
    
    /* Count actually allocated vectors */
    for (uint32_t i = 0; i < msi_mgr.num_vectors; i++) {
        if (msi_mgr.vectors[i].allocated) {
            allocated_count++;
        }
    }
    debug_printf("  Vectors in use: %u\n", allocated_count);
    
    debug_printf("\nRegistered Devices:\n");
    spin_lock_irqsave(&msi_mgr.device_lock, flags);
    dev = msi_mgr.devices;
    while (dev) {
        debug_printf("  %04x:%04x at %02x:%02x.%x:\n", 
                    dev->vendor_id, dev->device_id, dev->bus, dev->device, dev->function);
        debug_printf("    MSI: %s", dev->msi_capable ? "capable" : "not capable");
        if (dev->msi.enabled) {
            debug_printf(" (enabled, vectors %u-%u)",
                        dev->msi.base_vector, dev->msi.base_vector + dev->msi.num_vectors - 1);
        }
        debug_printf("\n");
        debug_printf("    MSI-X: %s", dev->msix_capable ? "capable" : "not capable");
        if (dev->msix.enabled) {
            debug_printf(" (enabled, table size %u)", dev->msix.table_size);
        }
        debug_printf("\n");
        
        dev = dev->next;
    }
    spin_unlock_irqrestore(&msi_mgr.device_lock, flags);
}

/**
 * Enable or disable MSI debugging
 */
void msi_debug_enable(bool enable)
{
    msi_mgr.debug_enabled = enable;
    debug_printf("MSI debugging %s\n", enable ? "enabled" : "disabled");
}

/* ===========================
 * PCI CONFIGURATION HELPER FUNCTIONS
 * =========================== */

/**
 * Read 32-bit value from PCI configuration space
 */
static uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    /* This would implement actual PCI configuration space access */
    /* For now, return dummy values */
    (void)bus; (void)device; (void)function; (void)offset;
    return 0;
}

/**
 * Write 32-bit value to PCI configuration space
 */
static void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    /* This would implement actual PCI configuration space access */
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}

/**
 * Read 16-bit value from PCI configuration space
 */
static uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    /* This would implement actual PCI configuration space access */
    (void)bus; (void)device; (void)function; (void)offset;
    return 0;
}

/**
 * Write 16-bit value to PCI configuration space
 */
static void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    /* This would implement actual PCI configuration space access */
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}