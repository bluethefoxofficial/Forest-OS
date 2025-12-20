#include "include/acpi.h"
#include "include/string.h"
#include "include/interrupt.h"
#include "include/debuglog.h"
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
#include <uacpi/event.h>
#include "include/panic.h"


#define EBDA_SEG_PTR 0x040E
#define BIOS_AREA_START 0xE0000
#define BIOS_AREA_END   0x100000

static const acpi_rsdp_t* g_rsdp = 0;
static const acpi_sdt_header_t* g_rsdt = 0;
static const acpi_sdt_header_t* g_xsdt = 0;
static const acpi_mcfg_table_t* g_mcfg = 0;
static bool g_acpi_initialized = false;
static bool g_uacpi_ready = false;

static uint8 acpi_checksum(const void* ptr, uint32 length) {
    const uint8* bytes = (const uint8*)ptr;
    uint8 sum = 0;
    for (uint32 i = 0; i < length; i++) {
        sum = (uint8)(sum + bytes[i]);
    }
    return sum;
}

static const acpi_rsdp_t* acpi_scan_region(uint32 base, uint32 length) {
    for (uint32 addr = base; addr < base + length; addr += 16) {
        const acpi_rsdp_t* rsdp = (const acpi_rsdp_t*)addr;
        if (memcmp(rsdp->v1.signature, ACPI_RSDP_SIGNATURE, 8) != 0) {
            continue;
        }

        uint32 check_length = (rsdp->v1.revision >= 2) ? rsdp->length : sizeof(acpi_rsdp_v1_t);
        if (acpi_checksum(rsdp, check_length) != 0) {
            continue;
        }
        if (rsdp->v1.revision >= 2 && acpi_checksum(rsdp, rsdp->length) != 0) {
            continue;
        }
        return rsdp;
    }
    return 0;
}

const acpi_rsdp_t* acpi_find_rsdp(void) {
    uint16* ebda_ptr = (uint16*)EBDA_SEG_PTR;
    uint32 ebda_address = (uint32)(*ebda_ptr) << 4;
    if (ebda_address) {
        const acpi_rsdp_t* rsdp = acpi_scan_region(ebda_address, 1024);
        if (rsdp) {
            return rsdp;
        }
    }
    return acpi_scan_region(BIOS_AREA_START, BIOS_AREA_END - BIOS_AREA_START);
}

static bool acpi_validate_table(const acpi_sdt_header_t* hdr) {
    if (!hdr) {
        return false;
    }
    return acpi_checksum(hdr, hdr->length) == 0;
}

static const acpi_sdt_header_t* acpi_get_root_table(const acpi_rsdp_t* rsdp) {
    if (!rsdp) {
        return 0;
    }
    if (rsdp->v1.revision >= 2 && rsdp->xsdt_address) {
        return (const acpi_sdt_header_t*)(uint32)(rsdp->xsdt_address & 0xFFFFFFFFu);
    }
    return (const acpi_sdt_header_t*)(uint32)rsdp->v1.rsdt_address;
}

static const acpi_sdt_header_t* acpi_find_table(const char* signature) {
    if (g_xsdt) {
        uint32 entries = (g_xsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint64);
        const uint64* entry = (const uint64*)((const uint8*)g_xsdt + sizeof(acpi_sdt_header_t));
        for (uint32 i = 0; i < entries; i++, entry++) {
            const acpi_sdt_header_t* table = (const acpi_sdt_header_t*)(uint32)(*entry & 0xFFFFFFFFu);
            if (!table) {
                continue;
            }
            if (memcmp(table->signature, signature, 4) == 0 && acpi_validate_table(table)) {
                return table;
            }
        }
    }

    if (g_rsdt) {
        uint32 entries = (g_rsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint32);
        const uint32* entry = (const uint32*)((const uint8*)g_rsdt + sizeof(acpi_sdt_header_t));
        for (uint32 i = 0; i < entries; i++, entry++) {
            const acpi_sdt_header_t* table = (const acpi_sdt_header_t*)(uint32)(*entry);
            if (!table) {
                continue;
            }
            if (memcmp(table->signature, signature, 4) == 0 && acpi_validate_table(table)) {
                return table;
            }
        }
    }
    return 0;
}

bool acpi_init(void) {
    if (g_acpi_initialized) {
        return (g_rsdp != 0);
    }
    g_acpi_initialized = true;

    g_rsdp = acpi_find_rsdp();
    if (!g_rsdp) {
        return false;
    }

    const acpi_sdt_header_t* root = acpi_get_root_table(g_rsdp);
    if (!acpi_validate_table(root)) {
        return false;
    }

    if (g_rsdp->v1.revision >= 2 && g_rsdp->xsdt_address) {
        g_xsdt = (const acpi_sdt_header_t*)(uint32)(g_rsdp->xsdt_address & 0xFFFFFFFFu);
        if (!acpi_validate_table(g_xsdt)) {
            g_xsdt = 0;
        }
    }

    g_rsdt = (const acpi_sdt_header_t*)(uint32)g_rsdp->v1.rsdt_address;
    if (!acpi_validate_table(g_rsdt)) {
        g_rsdt = 0;
    }

    const acpi_sdt_header_t* mcfg = acpi_find_table(ACPI_MCFG_SIGNATURE);
    if (mcfg) {
        g_mcfg = (const acpi_mcfg_table_t*)mcfg;
    }
    return true;
}

const acpi_rsdp_t* acpi_get_rsdp(void) {
    return g_rsdp;
}

const acpi_mcfg_table_t* acpi_get_mcfg(void) {
    return g_mcfg;
}

#define ACPI_LOG(msg) debuglog_write(msg)

bool uacpi_init(void) {
    if (g_uacpi_ready) {
        return true;
    }

    uacpi_status st = uacpi_initialize(UACPI_FLAG_NO_ACPI_MODE);
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] uacpi_initialize failed\n");
        return false;
    }

    st = uacpi_namespace_load();
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] uacpi_namespace_load failed\n");
        return false;
    }

    st = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] uacpi_namespace_initialize failed\n");
        return false;
    }

    st = uacpi_enter_acpi_mode();
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] uacpi_enter_acpi_mode failed\n");
        return false;
    }

    st = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] uacpi_finalize_gpe_initialization failed\n");
        return false;
    }

    g_uacpi_ready = true;
    return true;
}

bool acpi_shutdown(void) {
    if (!g_uacpi_ready && !uacpi_init()) {
        return false;
    }

    uacpi_status st = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] Failed to prepare for S5\n");
        return false;
    }

    irq_disable_safe();
    st = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] Failed to enter S5\n");
        return false;
    }
    return true;
}

bool acpi_reboot(void) {
    if (!g_uacpi_ready && !uacpi_init()) {
        return false;
    }

    uacpi_status st = uacpi_reboot();
    if (uacpi_unlikely_error(st)) {
        ACPI_LOG("[ACPI] ACPI reset register unavailable\n");
        return false;
    }
    return true;
}
