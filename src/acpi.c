#include "include/acpi.h"
#include "include/string.h"
#include "include/io.h"
#include "include/interrupt.h"
#include "include/debuglog.h"
#include "include/memory.h"
#include <uacpi/uacpi.h>
#include <uacpi/sleep.h>
#include <uacpi/event.h>
#include "include/panic.h"


#define EBDA_SEG_PTR 0x040E
#define BIOS_AREA_START 0xE0000
#define BIOS_AREA_END   0x100000
#define RSDP_SIGNATURE "RSD PTR "

static const acpi_rsdp_t* g_rsdp = 0;
static const acpi_sdt_header_t* g_rsdt = 0;
static const acpi_sdt_header_t* g_xsdt = 0;
static const acpi_mcfg_table_t* g_mcfg = 0;
static const acpi_fadt_t* g_fadt = 0;
static bool g_acpi_initialized = false;
static bool g_acpi_enabled = false;
static bool g_uacpi_ready = false;
static const char* g_acpi_last_error = "ACPI not initialized";

static void acpi_set_last_error(const char* reason) {
    if (reason && reason[0]) {
        g_acpi_last_error = reason;
    } else {
        g_acpi_last_error = "Unknown ACPI error";
    }
}

static bool acpi_bytes_equal(const void* lhs, const void* rhs, uint32 length) {
    const uint8* a = (const uint8*)lhs;
    const uint8* b = (const uint8*)rhs;
    for (uint32 i = 0; i < length; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static void acpi_copy_signature(char* out, const char* in, uint32 length) {
    if (!out || !in || length == 0) {
        return;
    }
    for (uint32 i = 0; i < length; i++) {
        char c = in[i];
        out[i] = (c >= 32 && c <= 126) ? c : '.';
    }
    out[length] = '\0';
}

static bool acpi_map_physical_range(uint32 phys_addr, uint32 length) {
    if (length == 0) {
        return false;
    }

    uint64_t end = (uint64_t)phys_addr + length;
    if (end > 0xFFFFFFFFULL) {
        return false;
    }

    uint32 start_aligned = memory_align_down(phys_addr, MEMORY_PAGE_SIZE);
    uint32 end_aligned = memory_align_up((uint32)end, MEMORY_PAGE_SIZE);

    if (end_aligned <= start_aligned) {
        return false;
    }

    page_directory_t* dir = vmm_get_current_page_directory();
    if (!dir) {
        return false;
    }

    memory_result_t res = vmm_identity_map_range(
        dir, start_aligned, end_aligned, PAGE_PRESENT | PAGE_WRITABLE);

    return res == MEMORY_OK || res == MEMORY_ERROR_ALREADY_MAPPED;
}

const acpi_sdt_header_t* acpi_map_table(uint64 phys_addr) {
    if (phys_addr == 0 || phys_addr > 0xFFFFFFFFULL) {
        debuglog_printf("[ACPI] acpi_map_table: invalid address hi=0x%08x lo=0x%08x\n",
                       (uint32)(phys_addr >> 32), (uint32)phys_addr);
        return NULL;
    }

    uint32 phys32 = (uint32)phys_addr;
    
    page_directory_t* dir = vmm_get_current_page_directory();
    if (!dir) {
        debuglog_printf("[ACPI] acpi_map_table: no page directory\n");
        return NULL;
    }
    
    if (!acpi_map_physical_range(phys32, sizeof(acpi_sdt_header_t))) {
        debuglog_printf("[ACPI] acpi_map_table: failed to map header at 0x%08x\n", phys32);
        return NULL;
    }

    const acpi_sdt_header_t* hdr = (const acpi_sdt_header_t*)phys32;
    uint32 length = hdr->length;

    if (length < sizeof(acpi_sdt_header_t) || length > (1 * 1024 * 1024)) {
        debuglog_printf("[ACPI] acpi_map_table: invalid length %u at 0x%08x\n", length, phys32);
        return NULL;
    }

    if (!acpi_map_physical_range(phys32, length)) {
        debuglog_printf("[ACPI] acpi_map_table: failed to map full table (%u bytes) at 0x%08x\n", length, phys32);
        return NULL;
    }

    return hdr;
}

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
        if (!acpi_map_physical_range(addr, sizeof(acpi_rsdp_t))) {
            continue;
        }
        
        const acpi_rsdp_t* rsdp = (const acpi_rsdp_t*)addr;
        
        if (!acpi_bytes_equal(rsdp->v1.signature, RSDP_SIGNATURE, 8)) {
            continue;
        }

        if (acpi_checksum(rsdp, sizeof(acpi_rsdp_v1_t)) != 0) {
            continue;
        }

        if (rsdp->v1.revision >= 2) {
            if (rsdp->length < sizeof(acpi_rsdp_t) || rsdp->length > 4096) {
                continue;
            }

            if (!acpi_map_physical_range(addr, rsdp->length)) {
                continue;
            }

            if (acpi_checksum(rsdp, rsdp->length) != 0) {
                continue;
            }
        }

        char signature[9];
        acpi_copy_signature(signature, rsdp->v1.signature, 8);
        debuglog_printf("[ACPI] RSDP: sig=%s, rev=%u, rsdt=0x%08x, xsdt=0x%08x\n",
                       signature, rsdp->v1.revision, rsdp->v1.rsdt_address,
                       (uint32)(rsdp->xsdt_address & 0xFFFFFFFFu));
        return rsdp;
    }
    return 0;
}

const acpi_rsdp_t* acpi_find_rsdp(void) {
    uint32 ebda_address = 0;
    
    uint16* ebda_ptr = (uint16*)EBDA_SEG_PTR;
    if (ebda_ptr) {
        ebda_address = (uint32)(*ebda_ptr) << 4;
        debuglog_printf("[ACPI] EBDA segment: 0x%04x -> physical: 0x%08x\n", *ebda_ptr, ebda_address);
    }
    
    if (ebda_address >= 0x80000 && ebda_address < 0x100000) {
        debuglog_printf("[ACPI] Searching EBDA at 0x%08x (1024 bytes)\n", ebda_address);
        const acpi_rsdp_t* rsdp = acpi_scan_region(ebda_address, 1024);
        if (rsdp) {
            return rsdp;
        }
    } else {
        debuglog_printf("[ACPI] EBDA address 0x%08x invalid, skipping\n", ebda_address);
    }
    
    debuglog_printf("[ACPI] Searching BIOS area 0xE0000-0x100000 (131072 bytes)\n");
    const acpi_rsdp_t* rsdp = acpi_scan_region(BIOS_AREA_START, BIOS_AREA_END - BIOS_AREA_START);
    if (rsdp) {
        return rsdp;
    }
    
    debuglog_printf("[ACPI] RSDP not found in any location\n");
    return 0;
}

static bool acpi_validate_table(const acpi_sdt_header_t* hdr) {
    if (!hdr) {
        return false;
    }
    return acpi_checksum(hdr, hdr->length) == 0;
}

static const acpi_sdt_header_t* acpi_get_root_table(const acpi_rsdp_t* rsdp) {
    if (!rsdp) {
        acpi_set_last_error("RSDP pointer is null");
        return 0;
    }

    debuglog_printf("[ACPI] Using RSDT at 0x%08x\n", rsdp->v1.rsdt_address);

    uint32 rsdt_addr = rsdp->v1.rsdt_address;
    if (rsdt_addr == 0) {
        if (rsdp->v1.revision >= 2 && rsdp->xsdt_address) {
            debuglog_printf("[ACPI] RSDT address is zero; will rely on XSDT\n");
        } else {
            debuglog_printf("[ACPI] RSDT address is zero and XSDT is unavailable\n");
            acpi_set_last_error("RSDT address is zero in RSDP");
        }
        return NULL;
    }

    const acpi_sdt_header_t* hdr = acpi_map_table(rsdt_addr);
    if (!hdr) {
        acpi_set_last_error("Failed to map RSDT physical address");
        return NULL;
    }

    if (!acpi_bytes_equal(hdr->signature, "RSDT", 4)) {
        char signature[5];
        acpi_copy_signature(signature, hdr->signature, 4);
        debuglog_printf("[ACPI] Root table signature '%s' is not RSDT\n", signature);
        acpi_set_last_error("Root table signature mismatch (expected RSDT)");
        return NULL;
    }

    uint32 length = hdr->length;
    debuglog_printf("[ACPI] RSDT header: sig=RSDT, len=%u, rev=%u\n",
                   length, hdr->revision);

    if (length < sizeof(acpi_sdt_header_t) || length > (256 * 1024)) {
        debuglog_printf("[ACPI] RSDT length invalid: %u\n", length);
        acpi_set_last_error("RSDT length is invalid");
        return NULL;
    }

    return hdr;
}

static const acpi_sdt_header_t* acpi_find_table(const char* signature) {
    if (g_xsdt) {
        uint32 entries = (g_xsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint64);
        const uint64* entry = (const uint64*)((const uint8*)g_xsdt + sizeof(acpi_sdt_header_t));
        for (uint32 i = 0; i < entries; i++, entry++) {
            const acpi_sdt_header_t* table = acpi_map_table(*entry & 0xFFFFFFFFu);
            if (!table) {
                continue;
            }
            if (acpi_bytes_equal(table->signature, signature, 4) && acpi_validate_table(table)) {
                return table;
            }
        }
    }

    if (g_rsdt) {
        uint32 entries = (g_rsdt->length - sizeof(acpi_sdt_header_t)) / sizeof(uint32);
        const uint32* entry = (const uint32*)((const uint8*)g_rsdt + sizeof(acpi_sdt_header_t));
        for (uint32 i = 0; i < entries; i++, entry++) {
            const acpi_sdt_header_t* table = acpi_map_table(*entry);
            if (!table) {
                continue;
            }
            if (acpi_bytes_equal(table->signature, signature, 4) && acpi_validate_table(table)) {
                return table;
            }
        }
    }
    return 0;
}

static bool acpi_enable(void) {
    if (g_acpi_enabled) {
        return true;
    }
    
    const acpi_fadt_t* fadt = g_fadt;
    if (!fadt) {
        debuglog_printf("[ACPI] Cannot enable - FADT not available\n");
        return false;
    }
    
    uint32 pm1a_cnt = 0;
    uint32 sci_enabled = 0;
    
    if (fadt->header.revision >= 2 && fadt->x_pm1a_control_block.address) {
        pm1a_cnt = (uint32)fadt->x_pm1a_control_block.address;
    } else if (fadt->pm1a_control_block) {
        pm1a_cnt = fadt->pm1a_control_block;
    }
    
    if (pm1a_cnt) {
        uint16 pm1a_status = inw(pm1a_cnt);
        sci_enabled = pm1a_status & 0x0400;
    }
    
    if (sci_enabled) {
        debuglog_printf("[ACPI] ACPI already enabled (SCI detected)\n");
        g_acpi_enabled = true;
        return true;
    }
    
    if (fadt->smi_command && fadt->acpi_enable) {
        debuglog_printf("[ACPI] Enabling ACPI via SMI command port 0x%02x, value 0x%02x\n",
                       fadt->smi_command, fadt->acpi_enable);
        
        outb(fadt->smi_command, fadt->acpi_enable);
        
        for (int i = 0; i < 300; i++) {
            if (pm1a_cnt) {
                uint16 pm1a_status = inw(pm1a_cnt);
                if (pm1a_status & 0x0400) {
                    debuglog_printf("[ACPI] ACPI enabled successfully after %d ms\n", i * 10);
                    g_acpi_enabled = true;
                    return true;
                }
            }
            for (volatile int j = 0; j < 100000; j++);
        }
        
        debuglog_printf("[ACPI] ACPI enable timed out\n");
        return false;
    }
    
    if (!fadt->smi_command && !fadt->acpi_enable) {
        debuglog_printf("[ACPI] No ACPI enable method available (SMI not required)\n");
        g_acpi_enabled = true;
        return true;
    }
    
    debuglog_printf("[ACPI] Cannot enable ACPI - no enable method found\n");
    return false;
}

bool acpi_init(void) {
    if (g_acpi_initialized) {
        if (g_rsdp == 0 && (g_acpi_last_error == 0 || g_acpi_last_error[0] == '\0')) {
            acpi_set_last_error("ACPI initialization previously failed");
        }
        return (g_rsdp != 0);
    }
    g_acpi_initialized = true;
    acpi_set_last_error("No ACPI error");

    debuglog_printf("[ACPI] Starting ACPI initialization\n");
    
    g_rsdp = acpi_find_rsdp();
    if (!g_rsdp) {
        acpi_set_last_error("RSDP not found in EBDA or BIOS search range");
        debuglog_printf("[ACPI] RSDP not found - ACPI not available\n");
        return false;
    }
    debuglog_printf("[ACPI] RSDP found at 0x%08x\n", (uint32)g_rsdp);

    g_rsdt = acpi_get_root_table(g_rsdp);
    if (!g_rsdt) {
        debuglog_printf("[ACPI] RSDT unavailable: %s\n", g_acpi_last_error);
    } else if (!acpi_validate_table(g_rsdt)) {
        debuglog_printf("[ACPI] RSDT invalid: addr=0x%08x, len=%u, checksum failed\n", 
                       (uint32)g_rsdt, g_rsdt->length);
        acpi_set_last_error("RSDT checksum validation failed");
        g_rsdt = NULL;
    } else {
        debuglog_printf("[ACPI] RSDT valid: addr=0x%08x, len=%u\n", 
                       (uint32)g_rsdt, g_rsdt->length);
    }
    
    if (g_rsdp->v1.revision >= 2 && g_rsdp->xsdt_address) {
        debuglog_printf("[ACPI] Trying XSDT at 0x%08x\n",
                       (uint32)(g_rsdp->xsdt_address & 0xFFFFFFFFu));
        g_xsdt = acpi_map_table(g_rsdp->xsdt_address);
        if (!g_xsdt) {
            debuglog_printf("[ACPI] XSDT mapping failed\n");
            if (!g_rsdt) {
                acpi_set_last_error("Failed to map XSDT and no valid RSDT");
            }
        } else if (!acpi_bytes_equal(g_xsdt->signature, "XSDT", 4)) {
            char signature[5];
            acpi_copy_signature(signature, g_xsdt->signature, 4);
            debuglog_printf("[ACPI] XSDT signature mismatch: '%s'\n", signature);
            if (!g_rsdt) {
                acpi_set_last_error("XSDT signature mismatch and no valid RSDT");
            }
            g_xsdt = NULL;
        } else if (!acpi_validate_table(g_xsdt)) {
            debuglog_printf("[ACPI] XSDT checksum invalid, using RSDT only\n");
            if (!g_rsdt) {
                acpi_set_last_error("XSDT checksum invalid and no valid RSDT");
            }
            g_xsdt = NULL;
        } else {
            debuglog_printf("[ACPI] XSDT valid: addr=0x%08x, len=%u\n",
                           (uint32)g_xsdt, g_xsdt->length);
        }
    }
    
    if (!g_rsdt && !g_xsdt) {
        if (g_acpi_last_error == 0 || g_acpi_last_error[0] == '\0' ||
            strcmp(g_acpi_last_error, "No ACPI error") == 0) {
            acpi_set_last_error("No valid ACPI root table (RSDT/XSDT)");
        }
        debuglog_printf("[ACPI] No valid root table found: %s\n", g_acpi_last_error);
        return false;
    }
    
    debuglog_printf("[ACPI] ACPI revision: %u\n", g_rsdp->v1.revision);
    
    const acpi_sdt_header_t* fadt_header = acpi_find_table("FACP");
    if (fadt_header) {
        g_fadt = (const acpi_fadt_t*)fadt_header;
        debuglog_printf("[ACPI] FADT found, PM1a control at 0x%08x\n", 
                       g_fadt->pm1a_control_block);
    }
    
    acpi_enable();
    
    const acpi_sdt_header_t* mcfg = acpi_find_table(ACPI_MCFG_SIGNATURE);
    if (mcfg) {
        g_mcfg = (const acpi_mcfg_table_t*)mcfg;
        debuglog_printf("[ACPI] MCFG table found at 0x%08x\n", (uint32)mcfg);
    } else {
        debuglog_printf("[ACPI] MCFG not found (PCIe ECAM not available)\n");
    }
    
    const acpi_sdt_header_t* madt = acpi_find_table("APIC");
    if (madt) {
        debuglog_printf("[ACPI] MADT found at 0x%08x\n", (uint32)madt);
    }
    
    debuglog_printf("[ACPI] Initialization complete\n");
    acpi_set_last_error("No ACPI error");
    return true;
}

const char* acpi_get_last_error(void) {
    return g_acpi_last_error;
}

const acpi_rsdp_t* acpi_get_rsdp(void) {
    return g_rsdp;
}

const acpi_mcfg_table_t* acpi_get_mcfg(void) {
    return g_mcfg;
}

const acpi_madt_header_t* acpi_get_madt(void) {
    return (const acpi_madt_header_t*)acpi_find_table("APIC");
}

const acpi_fadt_t* acpi_get_fadt(void) {
    return g_fadt;
}

#define ACPI_LOG(msg) debuglog_write(msg)

bool uacpi_init(void) {
    if (g_uacpi_ready) {
        acpi_set_last_error("No ACPI error");
        return true;
    }

    uacpi_status st = uacpi_initialize(UACPI_FLAG_NO_ACPI_MODE);
    if (uacpi_unlikely_error(st)) {
        acpi_set_last_error("uACPI initialize failed");
        ACPI_LOG("[ACPI] uacpi_initialize failed\n");
        return false;
    }

    st = uacpi_namespace_load();
    if (uacpi_unlikely_error(st)) {
        acpi_set_last_error("uACPI namespace load failed");
        ACPI_LOG("[ACPI] uacpi_namespace_load failed\n");
        return false;
    }

    st = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(st)) {
        acpi_set_last_error("uACPI namespace initialization failed");
        ACPI_LOG("[ACPI] uacpi_namespace_initialize failed\n");
        return false;
    }

    st = uacpi_enter_acpi_mode();
    if (uacpi_unlikely_error(st)) {
        acpi_set_last_error("uACPI failed to enter ACPI mode");
        ACPI_LOG("[ACPI] uacpi_enter_acpi_mode failed\n");
        return false;
    }

    st = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(st)) {
        acpi_set_last_error("uACPI GPE initialization failed");
        ACPI_LOG("[ACPI] uacpi_finalize_gpe_initialization failed\n");
        return false;
    }

    g_uacpi_ready = true;
    acpi_set_last_error("No ACPI error");
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
