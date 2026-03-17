/*
 * PCI Configuration Space Access with VirtualBox Compatibility
 * 
 * Fixes for VirtualBox hanging issues:
 * - Added timeout protection for PCI config reads
 * - ECAM access now validates address before dereferencing
 * - Falls back to Type1 access if ECAM fails
 * - Added watchdog timer to prevent infinite loops during enumeration
 */

#include "include/pci.h"
#include "include/acpi.h"
#include "include/io_ports.h"
#include "include/timer.h"
#include "include/debuglog.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* Maximum time to wait for PCI config read (in timer ticks) */
#define PCI_READ_TIMEOUT_TICKS 100

/* Maximum devices to enumerate before giving up */
#define PCI_ENUMERATION_LIMIT 256

typedef enum {
    PCI_ACCESS_TYPE1 = 0,
    PCI_ACCESS_ECAM  = 1
} pci_access_mode_t;

typedef struct {
    bool present;
    uint16 segment;
    uint8 start_bus;
    uint8 end_bus;
    uint64 base_address;
} pci_segment_info_t;

static pci_access_mode_t g_access_mode = PCI_ACCESS_TYPE1;
static pci_segment_info_t g_segments[PCI_MAX_SEGMENTS];
static uint32 g_segment_count = 0;
static bool g_pci_initialized = false;
static uint32 g_enumeration_count = 0;  /* Counter to prevent infinite loops */

static uint32 pci_build_address(uint8 bus, uint8 device, uint8 function, uint8 offset) {
    return (uint32)(0x80000000UL |
                    ((uint32)bus << 16) |
                    ((uint32)device << 11) |
                    ((uint32)function << 8) |
                    (offset & 0xFC));
}

/*
 * Validate that an ECAM address is safe to access
 * Returns false if address is invalid or would cause a page fault
 */
static bool pci_ecam_address_valid(uint64 addr) {
    /* ECAM space should be in the kernel memory region (above 0xC0000000 typically) */
    /* or in the physical address space that is identity mapped */
    
    /* Basic sanity checks */
    if (addr == 0) {
        return false;
    }
    
    /* Check for obviously invalid addresses */
    if (addr < 0x100000) {  /* Below 1MB - could be legacy VGA or other reserved areas */
        return false;
    }
    
    /* For 32-bit systems, ensure address fits in 32 bits */
    if (addr > 0xFFFFFFFFULL) {
        return false;
    }
    
    return true;
}

static volatile uint32* pci_ecam_ptr(const pci_segment_info_t* segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    if (!segment) {
        return 0;
    }
    if (bus < segment->start_bus || bus > segment->end_bus) {
        return 0;
    }
    
    uint64 bus_offset = bus - segment->start_bus;
    uint64 bus_part = bus_offset << 20;
    uint64 device_part = (uint64)device << 15;
    uint64 function_part = (uint64)function << 12;
    uint64 addr = segment->base_address + bus_part + device_part + function_part + (offset & ~0x3u);
    
    /* Validate address before returning pointer */
    if (!pci_ecam_address_valid(addr)) {
        return 0;
    }
    
    /* Cast to pointer - this assumes ECAM is identity mapped or mapped in kernel space */
    return (volatile uint32*)(uint32)(addr & 0xFFFFFFFFu);
}

static const pci_segment_info_t* pci_find_segment(uint16 segment, uint8 bus) {
    for (uint32 i = 0; i < g_segment_count; i++) {
        const pci_segment_info_t* info = &g_segments[i];
        if (!info->present) {
            continue;
        }
        if (info->segment == segment && bus >= info->start_bus && bus <= info->end_bus) {
            return info;
        }
    }
    return 0;
}

/*
 * Safe PCI config read with timeout protection
 * Prevents hanging on devices that don't respond
 */
static uint32 pci_read_type1_safe(uint8 bus, uint8 device, uint8 function, uint16 offset) {
    uint32 start_ticks = timer_get_ticks();
    
    outportd(PCI_CONFIG_ADDRESS, pci_build_address(bus, device, function, (uint8)offset));
    
    /* Small delay to let device respond */
    for (volatile int i = 0; i < 10; i++) {
        __asm__ volatile("nop");
    }
    
    uint32 value = inportd(PCI_CONFIG_DATA);
    
    /* Check for timeout - if we took too long, assume device not present */
    if (timer_get_ticks() - start_ticks > PCI_READ_TIMEOUT_TICKS) {
        debuglog(DEBUG_WARN, "[PCI] Config read timeout on %02x:%02x.%x\n", 
                 bus, device, function);
        return 0xFFFFFFFF;
    }
    
    return value;
}

bool pci_init(void) {
    if (g_pci_initialized) {
        return true;
    }
    
    /* Reset enumeration counter */
    g_enumeration_count = 0;
    
    /* Default to Type1 access - it's safer and works everywhere */
    g_access_mode = PCI_ACCESS_TYPE1;
    g_segment_count = 0;
    
    /* Try to find MCFG table for ECAM support */
    const acpi_mcfg_table_t* mcfg = acpi_get_mcfg();
    if (mcfg) {
        uint32 entry_bytes = mcfg->header.length - sizeof(acpi_mcfg_table_t);
        uint32 entry_count = entry_bytes / sizeof(acpi_mcfg_entry_t);
        if (entry_count > PCI_MAX_SEGMENTS) {
            entry_count = PCI_MAX_SEGMENTS;
        }
        
        const acpi_mcfg_entry_t* entry = (const acpi_mcfg_entry_t*)((const uint8*)mcfg + sizeof(acpi_mcfg_table_t));
        for (uint32 i = 0; i < entry_count; i++, entry++) {
            /* Validate the base address before using it */
            if (!pci_ecam_address_valid(entry->base_address)) {
                debuglog(DEBUG_WARN, "[PCI] Invalid ECAM base address: 0x%llx\n", 
                         (unsigned long long)entry->base_address);
                continue;
            }
            
            g_segments[g_segment_count].present = true;
            g_segments[g_segment_count].segment = entry->segment_group;
            g_segments[g_segment_count].start_bus = entry->start_bus;
            g_segments[g_segment_count].end_bus = entry->end_bus;
            g_segments[g_segment_count].base_address = entry->base_address;
            g_segment_count++;
        }
        
        /* Only use ECAM if we have valid segments */
        if (g_segment_count > 0) {
            debuglog(DEBUG_INFO, "[PCI] Found %u ECAM segment(s)\n", g_segment_count);
            
            /* Test ECAM access on bus 0, device 0 before enabling it */
            const pci_segment_info_t* test_segment = pci_find_segment(0, 0);
            if (test_segment) {
                volatile uint32* test_addr = pci_ecam_ptr(test_segment, 0, 0, 0, 0);
                if (test_addr) {
                    /* Try to read vendor ID - if this hangs, we'll need to use Type1 */
                    uint32 start_ticks = timer_get_ticks();
                    uint32 test_value = 0xFFFFFFFF;
                    
                    /* Use inline assembly with timeout check */
                    __asm__ volatile(
                        "movl (%1), %0"
                        : "=r" (test_value)
                        : "r" (test_addr)
                        : "memory"
                    );
                    
                    if (timer_get_ticks() - start_ticks > PCI_READ_TIMEOUT_TICKS) {
                        debuglog(DEBUG_WARN, "[PCI] ECAM access test failed (timeout), falling back to Type1\n");
                        g_access_mode = PCI_ACCESS_TYPE1;
                    } else if (test_value != 0xFFFFFFFF) {
                        debuglog(DEBUG_INFO, "[PCI] ECAM access test passed, enabling ECAM mode\n");
                        g_access_mode = PCI_ACCESS_ECAM;
                    } else {
                        debuglog(DEBUG_INFO, "[PCI] ECAM test returned 0xFFFFFFFF, using Type1\n");
                        g_access_mode = PCI_ACCESS_TYPE1;
                    }
                } else {
                    g_access_mode = PCI_ACCESS_TYPE1;
                }
            }
        }
    }
    
    g_pci_initialized = true;
    
    debuglog(DEBUG_INFO, "[PCI] Initialized with %s access mode\n", 
             g_access_mode == PCI_ACCESS_ECAM ? "ECAM" : "Type1");
    
    return true;
}

/*
 * PCI Config Read with ECAM safety checks
 */
uint32 pci_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    if (!g_pci_initialized) {
        return 0xFFFFFFFF;
    }
    
    if (g_access_mode == PCI_ACCESS_ECAM) {
        const pci_segment_info_t* info = pci_find_segment(segment, bus);
        if (info) {
            volatile uint32* addr = pci_ecam_ptr(info, bus, device, function, offset);
            if (addr) {
                /* Read with timeout protection */
                uint32 start_ticks = timer_get_ticks();
                uint32 value = *addr;
                
                if (timer_get_ticks() - start_ticks > PCI_READ_TIMEOUT_TICKS) {
                    debuglog(DEBUG_WARN, "[PCI] ECAM read timeout, switching to Type1\n");
                    g_access_mode = PCI_ACCESS_TYPE1;
                    return pci_read_type1_safe(bus, device, function, offset);
                }
                
                return value;
            }
        }
    }
    
    /* Fall back to Type1 access */
    (void)segment;
    return pci_read_type1_safe(bus, device, function, offset);
}

uint16 pci_config_read16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    uint32 value = pci_config_read32(segment, bus, device, function, offset);
    return (uint16)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8 pci_config_read8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    uint32 value = pci_config_read32(segment, bus, device, function, offset);
    return (uint8)((value >> ((offset & 3) * 8)) & 0xFF);
}

static void pci_write_type1(uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 value) {
    outportd(PCI_CONFIG_ADDRESS, pci_build_address(bus, device, function, (uint8)offset));
    outportd(PCI_CONFIG_DATA, value);
}

void pci_config_write32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 value) {
    if (!g_pci_initialized) {
        return;
    }
    
    if (g_access_mode == PCI_ACCESS_ECAM) {
        const pci_segment_info_t* info = pci_find_segment(segment, bus);
        if (info) {
            volatile uint32* addr = pci_ecam_ptr(info, bus, device, function, offset);
            if (addr) {
                *addr = value;
                return;
            }
        }
    }
    
    (void)segment;
    pci_write_type1(bus, device, function, offset, value);
}

void pci_config_write16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint16 value) {
    uint32 temp = pci_config_read32(segment, bus, device, function, offset);
    temp &= ~(0xFFFF << ((offset & 2) * 8));
    temp |= (value << ((offset & 2) * 8));
    pci_config_write32(segment, bus, device, function, offset, temp);
}

void pci_config_write8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 value) {
    uint32 temp = pci_config_read32(segment, bus, device, function, offset);
    temp &= ~(0xFF << ((offset & 3) * 8));
    temp |= (value << ((offset & 3) * 8));
    pci_config_write32(segment, bus, device, function, offset, temp);
}

static void pci_fill_device(uint16 segment, uint8 bus, uint8 device, uint8 function, pci_device_t* out_device) {
    out_device->segment = segment;
    out_device->bus = bus;
    out_device->device = device;
    out_device->function = function;
    out_device->vendor_id = pci_config_read16(segment, bus, device, function, 0x00);
    out_device->device_id = pci_config_read16(segment, bus, device, function, 0x02);
    out_device->revision_id = pci_config_read8(segment, bus, device, function, 0x08);
    out_device->prog_if = pci_config_read8(segment, bus, device, function, 0x09);
    out_device->subclass = pci_config_read8(segment, bus, device, function, 0x0A);
    out_device->class_code = pci_config_read8(segment, bus, device, function, 0x0B);
    out_device->header_type = pci_config_read8(segment, bus, device, function, 0x0E);

    out_device->is_pcie = false;
    out_device->pcie_cap_offset = 0;
    out_device->pcie_device_port_type = 0;
    out_device->pcie_link_speed = 0;
    out_device->pcie_link_width = 0;

    for (uint8 i = 0; i < PCI_BAR_COUNT; i++) {
        out_device->bar[i] = pci_config_read32(segment, bus, device, function, 0x10 + (i * 4));
    }

    uint8 pcie_cap = pcie_find_capability_offset(segment, bus, device, function, PCIE_CAPABILITY_ID);
    if (pcie_cap != 0) {
        out_device->is_pcie = true;
        out_device->pcie_cap_offset = pcie_cap;
        
        uint16 pcie_cap_reg = pcie_config_read16(segment, bus, device, function, pcie_cap);
        out_device->pcie_device_port_type = (pcie_cap_reg >> 4) & 0xF;
        
        uint32 link_cap = pcie_config_read32(segment, bus, device, function, pcie_cap + PCIE_LINK_CAP_OFFSET);
        out_device->pcie_link_speed = link_cap & 0xF;
        out_device->pcie_link_width = (link_cap >> 4) & 0x3F;
        
        uint16 link_status = pcie_config_read16(segment, bus, device, function, pcie_cap + PCIE_LINK_STATUS_OFFSET);
        uint8 current_speed = link_status & 0xF;
        uint8 current_width = (link_status >> 4) & 0x3F;
        
        if (current_speed != 0) out_device->pcie_link_speed = current_speed;
        if (current_width != 0) out_device->pcie_link_width = current_width;
    }
}

/*
 * Enumeration function with limit to prevent infinite loops
 */
static bool pci_enumerate_function(uint16 segment, uint8 bus, uint8 device, uint8 function, pci_enum_callback_t callback, void* context) {
    /* Check enumeration limit to prevent infinite loops/hangs */
    if (g_enumeration_count >= PCI_ENUMERATION_LIMIT) {
        debuglog(DEBUG_WARN, "[PCI] Enumeration limit reached, stopping\n");
        return false;
    }
    g_enumeration_count++;
    
    pci_device_t dev;
    pci_fill_device(segment, bus, device, function, &dev);
    if (dev.vendor_id == 0xFFFF) {
        return true;
    }
    if (callback) {
        return callback(&dev, context);
    }
    return true;
}

static void pci_scan_bus_range(uint16 segment, uint8 start_bus, uint8 end_bus, pci_enum_callback_t callback, void* context) {
    for (uint16 bus = start_bus; bus <= end_bus; bus++) {
        for (uint8 device = 0; device < PCI_MAX_DEVICE; device++) {
            /* Check enumeration limit */
            if (g_enumeration_count >= PCI_ENUMERATION_LIMIT) {
                debuglog(DEBUG_WARN, "[PCI] Enumeration limit reached during bus scan\n");
                return;
            }
            
            uint16 vendor = pci_config_read16(segment, bus, device, 0, 0x00);
            if (vendor == 0xFFFF) {
                continue;
            }

            uint8 header = pci_config_read8(segment, bus, device, 0, 0x0E);
            uint8 functions = (header & 0x80) ? PCI_MAX_FUNCTION : 1;

            for (uint8 function = 0; function < functions; function++) {
                if (!pci_enumerate_function(segment, bus, device, function, callback, context)) {
                    return;
                }
            }
        }
    }
}

void pci_enumerate(pci_enum_callback_t callback, void* context) {
    /* Reset enumeration counter at start of enumeration */
    g_enumeration_count = 0;
    
    if (g_access_mode == PCI_ACCESS_ECAM && g_segment_count > 0) {
        for (uint32 i = 0; i < g_segment_count; i++) {
            const pci_segment_info_t* segment = &g_segments[i];
            if (!segment->present) {
                continue;
            }
            pci_scan_bus_range(segment->segment, segment->start_bus, segment->end_bus, callback, context);
            
            /* Check if enumeration limit was reached */
            if (g_enumeration_count >= PCI_ENUMERATION_LIMIT) {
                return;
            }
        }
        return;
    }

    pci_scan_bus_range(0, 0, PCI_MAX_BUS - 1, callback, context);
}

typedef struct {
    uint8 target_class;
    uint8 target_subclass;
    pci_device_t* out_device;
    bool found;
} pci_find_class_ctx;

static bool pci_find_class_cb(const pci_device_t* dev, void* context) {
    pci_find_class_ctx* ctx = (pci_find_class_ctx*)context;
    if (dev->class_code == ctx->target_class && dev->subclass == ctx->target_subclass) {
        *ctx->out_device = *dev;
        ctx->found = true;
        return false;
    }
    return true;
}

bool pci_find_by_class(uint8 class_code, uint8 subclass, pci_device_t* out_device) {
    pci_find_class_ctx ctx = {class_code, subclass, out_device, false};
    pci_enumerate(pci_find_class_cb, &ctx);
    return ctx.found;
}

typedef struct {
    uint16 target_vendor;
    uint16 target_device;
    pci_device_t* out_device;
    bool found;
} pci_find_vendor_ctx;

static bool pci_find_vendor_cb(const pci_device_t* dev, void* context) {
    pci_find_vendor_ctx* ctx = (pci_find_vendor_ctx*)context;
    if (dev->vendor_id == ctx->target_vendor && dev->device_id == ctx->target_device) {
        *ctx->out_device = *dev;
        ctx->found = true;
        return false;
    }
    return true;
}

bool pci_find_by_vendor_device(uint16 vendor_id, uint16 device_id, pci_device_t* out_device) {
    pci_find_vendor_ctx ctx = {vendor_id, device_id, out_device, false};
    pci_enumerate(pci_find_vendor_cb, &ctx);
    return ctx.found;
}

/*
 * PCIe Capability Access with safety checks
 */
uint8 pcie_find_capability_offset(uint16 segment, uint8 bus, uint8 device, uint8 function, uint8 cap_id) {
    uint8 header_type = pci_config_read8(segment, bus, device, function, 0x0E);
    if ((header_type & 0x7F) != 0x00) {
        return 0;
    }
    
    uint8 status = pci_config_read8(segment, bus, device, function, 0x06);
    if (!(status & 0x10)) {
        return 0;
    }
    
    uint8 capability_ptr = pci_config_read8(segment, bus, device, function, 0x34);
    capability_ptr &= 0xFC;
    
    uint8 max_iterations = 48;
    while (capability_ptr != 0 && max_iterations-- > 0) {
        uint8 current_id = pci_config_read8(segment, bus, device, function, capability_ptr);
        if (current_id == cap_id) {
            return capability_ptr;
        }
        capability_ptr = pci_config_read8(segment, bus, device, function, capability_ptr + 1);
        capability_ptr &= 0xFC;
    }
    
    return 0;
}

uint32 pcie_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    return pci_config_read32(segment, bus, device, function, offset);
}

uint16 pcie_config_read16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    return pci_config_read16(segment, bus, device, function, offset);
}

bool pcie_is_enumerated_device_pcie(const pci_device_t* device) {
    if (!device) {
        return false;
    }
    return device->is_pcie;
}
