#include "include/memory.h"
#include "include/memory_safe.h"
#include "include/screen.h"
#include "include/panic.h"

// =============================================================================
// INTELLIGENT MEMORY REGION MANAGER
// =============================================================================
// Automatically detects and protects critical system memory regions including:
// - Kernel code and data
// - Framebuffer memory  
// - Hardware MMIO regions
// - Reserved system areas
// =============================================================================

#define MAX_PROTECTED_REGIONS 16
#define MEMORY_REGION_MAGIC 0x4D454D52  // "MEMR"

typedef enum {
    REGION_TYPE_KERNEL_CODE = 0x10,
    REGION_TYPE_KERNEL_DATA = 0x11,
    REGION_TYPE_KERNEL_HEAP = 0x12,
    REGION_TYPE_FRAMEBUFFER = 0x20,
    REGION_TYPE_VGA_TEXT = 0x21,
    REGION_TYPE_MMIO = 0x30,
    REGION_TYPE_BIOS = 0x40,
    REGION_TYPE_HARDWARE = 0x50,
    REGION_TYPE_USER_SPACE = 0x60
} protected_region_type_t;

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    protected_region_type_t type;
    const char* description;
    bool read_only;
    bool critical;
} protected_region_t;

static struct {
    uint32_t magic;
    protected_region_t regions[MAX_PROTECTED_REGIONS];
    uint32_t region_count;
    bool initialized;
} region_manager = {
    .magic = MEMORY_REGION_MAGIC,
    .region_count = 0,
    .initialized = false
};

// =============================================================================
// REGION REGISTRATION FUNCTIONS
// =============================================================================

static void add_protected_region(uint32_t start, uint32_t end, 
                                protected_region_type_t type,
                                const char* description, 
                                bool read_only, bool critical) {
    if (region_manager.region_count >= MAX_PROTECTED_REGIONS) {
        return; // Silent failure to avoid recursion
    }
    
    protected_region_t* region = &region_manager.regions[region_manager.region_count];
    region->start_addr = start;
    region->end_addr = end;
    region->type = type;
    region->description = description;
    region->read_only = read_only;
    region->critical = critical;
    
    region_manager.region_count++;
}

void memory_region_manager_init(void) {
    if (region_manager.initialized) {
        return;
    }
    
    // === CRITICAL SYSTEM REGIONS ===
    
    // 1. Null pointer protection (first page)
    add_protected_region(0x00000000, 0x00001000,
                        REGION_TYPE_HARDWARE,
                        "Null pointer protection zone", true, true);
    
    // 2. Interrupt Vector Table (Real mode)
    add_protected_region(0x00000000, 0x000003FF,
                        REGION_TYPE_BIOS,
                        "Interrupt Vector Table", true, true);
    
    // 3. BIOS Data Area (BDA)
    add_protected_region(0x00000400, 0x000004FF,
                        REGION_TYPE_BIOS,
                        "BIOS Data Area", true, true);
    
    // 4. DOS Compatibility Area
    add_protected_region(0x00000500, 0x00007C00,
                        REGION_TYPE_BIOS,
                        "DOS compatibility area", true, false);
    
    // 5. Video memory regions
    add_protected_region(0x000A0000, 0x000BFFFF,
                        REGION_TYPE_VGA_TEXT,
                        "VGA graphics memory", false, false);
    
    // 6. VGA text mode buffer  
    add_protected_region(0x000B8000, 0x000C0000,
                        REGION_TYPE_VGA_TEXT,
                        "VGA text mode buffer", false, false);
    
    // 7. Video BIOS ROM
    add_protected_region(0x000C0000, 0x000C7FFF,
                        REGION_TYPE_BIOS,
                        "Video BIOS ROM", true, false);
    
    // 8. Adapter ROM space
    add_protected_region(0x000C8000, 0x000EFFFF,
                        REGION_TYPE_BIOS,
                        "Adapter ROM space", true, false);
    
    // 9. System BIOS ROM
    add_protected_region(0x000F0000, 0x000FFFFF,
                        REGION_TYPE_BIOS,
                        "System BIOS ROM", true, true);
    
    // === KERNEL REGIONS ===
    
    // 10. Kernel code and data (1MB - estimated 3MB for safety)
    add_protected_region(MEMORY_KERNEL_START, 0x00400000, 
                        REGION_TYPE_KERNEL_CODE,
                        "Kernel code and data", true, true);
    
    // 11. PMM bitmap area  
    add_protected_region(MEMORY_PMM_START, MEMORY_PMM_START + MEMORY_PMM_SIZE,
                        REGION_TYPE_KERNEL_DATA,
                        "Physical Memory Manager bitmap", false, true);
    
    // 12. Kernel heap
    add_protected_region(MEMORY_KERNEL_HEAP_START, 0x02000000,
                        REGION_TYPE_KERNEL_HEAP,
                        "Kernel heap", false, true);
    
    // === HARDWARE MMIO REGIONS ===
    
    // 13. PCI Configuration Space
    add_protected_region(0xF0000000, 0xF8000000,
                        REGION_TYPE_MMIO,
                        "PCI Configuration Space", false, false);
    
    // 14. APIC/Local APIC
    add_protected_region(0xFEE00000, 0xFEE01000,
                        REGION_TYPE_MMIO,
                        "Local APIC", false, true);
    
    // 15. I/O APIC
    add_protected_region(0xFEC00000, 0xFEC01000,
                        REGION_TYPE_MMIO,
                        "I/O APIC", false, true);
    
    // === GRAPHICS FRAMEBUFFER REGIONS ===
    
    // 16. Bochs/QEMU typical framebuffer
    add_protected_region(0xFE000000, 0xFF000000,
                        REGION_TYPE_FRAMEBUFFER,
                        "Graphics framebuffer (Bochs/QEMU)", false, false);
    
    // 17. Intel integrated graphics typical range
    add_protected_region(0x80000000, 0x90000000,
                        REGION_TYPE_FRAMEBUFFER,
                        "Graphics framebuffer (Intel)", false, false);
    
    // 18. VESA framebuffer common range
    add_protected_region(0xE0000000, 0xF0000000,
                        REGION_TYPE_FRAMEBUFFER,
                        "Graphics framebuffer (VESA)", false, false);
    
    // 19. AMD graphics memory range
    add_protected_region(0xD0000000, 0xE0000000,
                        REGION_TYPE_FRAMEBUFFER,
                        "Graphics framebuffer (AMD)", false, false);
    
    // === DANGEROUS MEMORY PATTERNS ===
    
    // These aren't real regions but help with pattern detection
    // The address checks will be done in the analysis functions
    
    region_manager.initialized = true;
}

// =============================================================================
// REGION ANALYSIS FUNCTIONS
// =============================================================================

const char* memory_region_get_description(uint32_t addr) {
    if (!region_manager.initialized) {
        return "Unknown region (manager not initialized)";
    }
    
    for (uint32_t i = 0; i < region_manager.region_count; i++) {
        protected_region_t* region = &region_manager.regions[i];
        if (addr >= region->start_addr && addr < region->end_addr) {
            return region->description;
        }
    }
    
    // Comprehensive memory corruption pattern analysis
    if (addr < 0x1000) {
        return "NULL POINTER DEREFERENCE - Critical security violation";
    } else if ((addr & 0xFF000000) == 0xAA000000) {
        return "HEAP CORRUPTION - Uninitialized memory pattern detected";
    } else if ((addr & 0xFF000000) == 0xDE000000) {
        return "USE-AFTER-FREE - Accessing freed memory (security risk)";
    } else if ((addr & 0xFF000000) == 0xCC000000) {
        return "BUFFER OVERFLOW - Red zone memory pattern (stack smashing)";
    } else if ((addr & 0xFF000000) == 0x55000000) {
        return "CANARY VIOLATION - Stack protection canary pattern";
    } else if ((addr & 0xFF000000) == 0xFF000000) {
        return "INVALID POINTER - All bits set pattern";
    } else if ((addr & 0xFFFF0000) == 0xFEED0000) {
        return "DEBUG PATTERN - Feed/Face debug marker";
    } else if ((addr & 0xFFFF0000) == 0xFACE0000) {
        return "DEBUG PATTERN - Face debug marker";
    } else if ((addr & 0xFFFF0000) == 0xDEAD0000) {
        return "DEATH PATTERN - Dead/Beef marker (likely corruption)";
    } else if ((addr & 0xFFFF0000) == 0xBEEF0000) {
        return "DEBUG PATTERN - Beef marker";
    } else if ((addr & 0xF0000000) == 0x80000000) {
        return "KERNEL VIRTUAL SPACE - High kernel addresses";
    } else if (addr >= 0xC0000000) {
        return "KERNEL SPACE - Reserved high memory region";
    } else if (addr >= MEMORY_USER_START && addr < 0xC0000000) {
        return "USER SPACE - Application memory region";
    } else if (addr >= 0x00100000 && addr < MEMORY_USER_START) {
        return "KERNEL LOW MEMORY - System critical region";
    } else {
        return "UNKNOWN MEMORY REGION - Potentially dangerous access";
    }
}

bool memory_region_is_critical(uint32_t addr) {
    if (!region_manager.initialized) {
        return true; // Assume critical if not initialized
    }
    
    for (uint32_t i = 0; i < region_manager.region_count; i++) {
        protected_region_t* region = &region_manager.regions[i];
        if (addr >= region->start_addr && addr < region->end_addr) {
            return region->critical;
        }
    }
    
    // Unknown regions in kernel space are considered critical
    return (addr < MEMORY_USER_START);
}

bool memory_region_is_writable(uint32_t addr) {
    if (!region_manager.initialized) {
        return false; // Assume read-only if not initialized
    }
    
    for (uint32_t i = 0; i < region_manager.region_count; i++) {
        protected_region_t* region = &region_manager.regions[i];
        if (addr >= region->start_addr && addr < region->end_addr) {
            return !region->read_only;
        }
    }
    
    return false; // Unknown regions are not writable
}

protected_region_type_t memory_region_get_type(uint32_t addr) {
    if (!region_manager.initialized) {
        return REGION_TYPE_KERNEL_DATA; // Safe default
    }
    
    for (uint32_t i = 0; i < region_manager.region_count; i++) {
        protected_region_t* region = &region_manager.regions[i];
        if (addr >= region->start_addr && addr < region->end_addr) {
            return region->type;
        }
    }
    
    return REGION_TYPE_USER_SPACE;
}

// =============================================================================
// DYNAMIC FRAMEBUFFER REGISTRATION
// =============================================================================

void memory_region_register_framebuffer(uint32_t phys_addr, uint32_t size) {
    if (!region_manager.initialized) {
        memory_region_manager_init();
    }
    
    add_protected_region(phys_addr, phys_addr + size,
                        REGION_TYPE_FRAMEBUFFER,
                        "Active graphics framebuffer", false, false);
}

void memory_region_register_mmio(uint32_t phys_addr, uint32_t size, const char* device_name) {
    if (!region_manager.initialized) {
        memory_region_manager_init();
    }
    
    add_protected_region(phys_addr, phys_addr + size,
                        REGION_TYPE_MMIO,
                        device_name ? device_name : "Hardware MMIO", false, false);
}

// =============================================================================
// DEBUG AND REPORTING
// =============================================================================

void memory_region_dump_info(void) {
    if (!region_manager.initialized) {
        print("[MRM] Region manager not initialized\n");
        return;
    }
    
    print("[MRM] Protected memory regions:\n");
    for (uint32_t i = 0; i < region_manager.region_count; i++) {
        protected_region_t* region = &region_manager.regions[i];
        print("[MRM] 0x");
        print_hex(region->start_addr);
        print(" - 0x");
        print_hex(region->end_addr);
        print(" ");
        print(region->description);
        if (region->critical) print(" [CRITICAL]");
        if (region->read_only) print(" [READ-only]");
        print("\n");
    }
}

// =============================================================================
// PAGE FAULT ANALYSIS INTEGRATION
// =============================================================================

void memory_analyze_page_fault(uint32_t fault_addr, uint32_t error_code,
                               char* analysis_buffer, size_t buffer_size) {
    if (!region_manager.initialized || !analysis_buffer) {
        return;
    }
    
    const char* region_desc = memory_region_get_description(fault_addr);
    protected_region_type_t region_type = memory_region_get_type(fault_addr);
    bool is_critical = memory_region_is_critical(fault_addr);
    bool is_writable = memory_region_is_writable(fault_addr);
    
    // Build analysis string manually (avoiding complex string functions)
    char* p = analysis_buffer;
    const char* prefix = "Region: ";
    for (int i = 0; prefix[i] != '\0' && (p - analysis_buffer) < (int)buffer_size - 1; i++) {
        *p++ = prefix[i];
    }
    for (int i = 0; region_desc[i] != '\0' && (p - analysis_buffer) < (int)buffer_size - 1; i++) {
        *p++ = region_desc[i];
    }
    
    if (is_critical && (p - analysis_buffer) < (int)buffer_size - 20) {
        const char* crit = " [CRITICAL]";
        for (int i = 0; crit[i] != '\0'; i++) *p++ = crit[i];
    }
    
    if ((error_code & 0x02) && !is_writable && (p - analysis_buffer) < (int)buffer_size - 30) {
        const char* readonly = " [Write to read-only]";
        for (int i = 0; readonly[i] != '\0'; i++) *p++ = readonly[i];
    }
    
    *p = '\0';
}