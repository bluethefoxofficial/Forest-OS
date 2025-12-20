#include "include/pci.h"
#include "include/acpi.h"
#include "include/io_ports.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

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

static uint32 pci_build_address(uint8 bus, uint8 device, uint8 function, uint8 offset) {
    return (uint32)(0x80000000UL |
                    ((uint32)bus << 16) |
                    ((uint32)device << 11) |
                    ((uint32)function << 8) |
                    (offset & 0xFC));
}

static volatile uint32* pci_ecam_ptr(const pci_segment_info_t* segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    if (!segment) {
        return 0;
    }
    uint64 bus_part = ((uint64)bus - segment->start_bus) << 20;
    uint64 device_part = (uint64)device << 15;
    uint64 function_part = (uint64)function << 12;
    uint64 addr = segment->base_address + bus_part + device_part + function_part + (offset & ~0x3u);
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

bool pci_init(void) {
    if (g_pci_initialized) {
        return true;
    }
    g_pci_initialized = true;

    const acpi_mcfg_table_t* mcfg = acpi_get_mcfg();
    if (mcfg) {
        uint32 entry_bytes = mcfg->header.length - sizeof(acpi_mcfg_table_t);
        uint32 entry_count = entry_bytes / sizeof(acpi_mcfg_entry_t);
        if (entry_count > PCI_MAX_SEGMENTS) {
            entry_count = PCI_MAX_SEGMENTS;
        }
        const acpi_mcfg_entry_t* entry = (const acpi_mcfg_entry_t*)((const uint8*)mcfg + sizeof(acpi_mcfg_table_t));
        for (uint32 i = 0; i < entry_count; i++, entry++) {
            g_segments[g_segment_count].present = true;
            g_segments[g_segment_count].segment = entry->segment_group;
            g_segments[g_segment_count].start_bus = entry->start_bus;
            g_segments[g_segment_count].end_bus = entry->end_bus;
            g_segments[g_segment_count].base_address = entry->base_address;
            g_segment_count++;
        }
        if (g_segment_count > 0) {
            g_access_mode = PCI_ACCESS_ECAM;
        }
    }

    return true;
}

static uint32 pci_read_type1(uint8 bus, uint8 device, uint8 function, uint16 offset) {
    outportd(PCI_CONFIG_ADDRESS, pci_build_address(bus, device, function, (uint8)offset));
    return inportd(PCI_CONFIG_DATA);
}

uint32 pci_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset) {
    if (g_access_mode == PCI_ACCESS_ECAM) {
        const pci_segment_info_t* info = pci_find_segment(segment, bus);
        volatile uint32* addr = pci_ecam_ptr(info, bus, device, function, offset);
        if (addr) {
            return *addr;
        }
    }
    (void)segment;
    return pci_read_type1(bus, device, function, offset);
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
    if (g_access_mode == PCI_ACCESS_ECAM) {
        const pci_segment_info_t* info = pci_find_segment(segment, bus);
        volatile uint32* addr = pci_ecam_ptr(info, bus, device, function, offset);
        if (addr) {
            *addr = value;
            return;
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

    for (uint8 i = 0; i < PCI_BAR_COUNT; i++) {
        out_device->bar[i] = pci_config_read32(segment, bus, device, function, 0x10 + (i * 4));
    }
}

static bool pci_enumerate_function(uint16 segment, uint8 bus, uint8 device, uint8 function, pci_enum_callback_t callback, void* context) {
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
    if (g_access_mode == PCI_ACCESS_ECAM && g_segment_count > 0) {
        for (uint32 i = 0; i < g_segment_count; i++) {
            const pci_segment_info_t* segment = &g_segments[i];
            if (!segment->present) {
                continue;
            }
            pci_scan_bus_range(segment->segment, segment->start_bus, segment->end_bus, callback, context);
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

static bool pci_find_class_cb(const pci_device_t* device, void* context) {
    pci_find_class_ctx* ctx = (pci_find_class_ctx*)context;
    if (device->class_code == ctx->target_class && device->subclass == ctx->target_subclass) {
        if (ctx->out_device) {
            *ctx->out_device = *device;
        }
        ctx->found = true;
        return false;
    }
    return true;
}

bool pci_find_by_class(uint8 class_code, uint8 subclass, pci_device_t* out_device) {
    pci_find_class_ctx ctx;
    ctx.target_class = class_code;
    ctx.target_subclass = subclass;
    ctx.out_device = out_device;
    ctx.found = false;
    pci_enumerate(pci_find_class_cb, &ctx);
    return ctx.found;
}

typedef struct {
    uint16 vendor;
    uint16 device;
    pci_device_t* out;
    bool found;
} pci_find_vendor_ctx;

static bool pci_find_vendor_cb(const pci_device_t* device, void* context) {
    pci_find_vendor_ctx* ctx = (pci_find_vendor_ctx*)context;
    if (device->vendor_id == ctx->vendor && device->device_id == ctx->device) {
        if (ctx->out) {
            *ctx->out = *device;
        }
        ctx->found = true;
        return false;
    }
    return true;
}

bool pci_find_by_vendor_device(uint16 vendor_id, uint16 device_id, pci_device_t* out_device) {
    pci_find_vendor_ctx ctx;
    ctx.vendor = vendor_id;
    ctx.device = device_id;
    ctx.out = out_device;
    ctx.found = false;
    pci_enumerate(pci_find_vendor_cb, &ctx);
    return ctx.found;
}
