#include "include/memory.h"
#include "include/memory_safe.h"
#include "include/multiboot.h"
#include "include/screen.h"
#include "include/panic.h"
#include <stdint.h>

// === SAFE MEMORY MAP PROCESSING ===

#define MAX_MEMORY_REGIONS_SAFE 32  // Reduced from 64 for safety
#define MEMORY_MAP_MAGIC 0x4D4D4150U  // "MMAP"

static memory_region_t safe_memory_regions[MAX_MEMORY_REGIONS_SAFE];
static uint32 safe_region_count = 0;
static uint32 total_usable_memory = 0;
static bool memory_map_initialized = false;
static uint32 memory_map_checksum = 0;

// === INTERNAL VALIDATION HELPERS ===

static bool validate_multiboot_magic(uint32 magic) {
    return (magic == MULTIBOOT_BOOTLOADER_MAGIC || magic == MULTIBOOT2_BOOTLOADER_MAGIC);
}

static bool is_bootloader_accessible_range(uint32 addr, uint32 length) {
    if (length == 0) {
        return false;
    }
    // Bootloader structures typically live below 1MB; allow anything in low memory
    // that does not overlap the first 4KB (null/IVT) and stays below the PMM area.
    const uint32 min_addr = 0x00001000U; // skip IVT/BIOS data
    const uint32 max_addr = MEMORY_PMM_START; // identity mapped during early boot
    
    if (addr < min_addr) {
        return false;
    }
    if (addr > max_addr) {
        return false;
    }
    if (addr > UINT32_MAX - length) {
        return false;
    }
    if (addr + length > max_addr) {
        return false;
    }
    return true;
}

static bool validate_multiboot_info_pointer(uint32 mbi_addr) {
    // Multiboot info structures are not guaranteed to be page aligned.
    if (!is_bootloader_accessible_range(mbi_addr, sizeof(multiboot_info_t))) {
        return false;
    }
    // Require word alignment only
    if ((mbi_addr & 0x3U) != 0) {
        return false;
    }
    return true;
}

static bool validate_memory_region_entry(const multiboot_mmap_entry_t* entry) {
    if (!is_bootloader_accessible_range((uint32)entry, sizeof(multiboot_mmap_entry_t))) {
        return false;
    }
    
    // Check entry size field
    if (entry->size < sizeof(multiboot_mmap_entry_t) - sizeof(entry->size)) {
        return false;
    }
    
    // Validate memory region type
    if (entry->type < MULTIBOOT_MEMORY_AVAILABLE || 
        entry->type > MULTIBOOT_MEMORY_BADRAM) {
        return false;
    }
    
    // Check for address overflow
    uint64 base = ((uint64)entry->addr_high << 32) | entry->addr_low;
    uint64 length = ((uint64)entry->len_high << 32) | entry->len_low;
    
    if (length == 0) return false;
    if (base > UINT64_MAX - length) return false;  // Overflow check
    
    // Reject ridiculously large regions
    if (length > MEMORY_MAX_USABLE_BYTES) return false;
    
    return true;
}

static bool validate_region_for_conflicts(const memory_region_t* new_region) {
    for (uint32 i = 0; i < safe_region_count; i++) {
        const memory_region_t* existing = &safe_memory_regions[i];
        
        uint64 new_start = new_region->base_address;
        uint64 new_end = new_region->base_address + new_region->length;
        uint64 existing_start = existing->base_address;
        uint64 existing_end = existing->base_address + existing->length;
        
        // Check for overlap
        if ((new_start < existing_end) && (new_end > existing_start)) {
            print("[MEMMAP] Region conflict detected:\n");
            print("  New region: ");
            print_hex(new_start);
            print(" - ");
            print_hex(new_end - 1);
            print("\n");
            print("  Existing region: ");
            print_hex(existing_start);
            print(" - ");
            print_hex(existing_end - 1);
            print("\n");
            return false;
        }
    }
    return true;
}

static memory_validation_result_t sanitize_region(memory_region_t* region) {
    // Convert multiboot types to our safe types
    switch (region->type) {
        case MULTIBOOT_MEMORY_AVAILABLE:
            region->type = MEMORY_REGION_AVAILABLE;
            break;
        case MULTIBOOT_MEMORY_RESERVED:
            region->type = MEMORY_REGION_RESERVED;
            break;
        case MULTIBOOT_MEMORY_ACPI_RECLAIM:
            region->type = MEMORY_REGION_ACPI_RECLAIM;
            break;
        case MULTIBOOT_MEMORY_ACPI_NVS:
            region->type = MEMORY_REGION_ACPI_NVS;
            break;
        case MULTIBOOT_MEMORY_BADRAM:
            region->type = MEMORY_REGION_BADRAM;
            break;
        default:
            region->type = MEMORY_REGION_INVALID;
            return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    // Reject regions below 1MB
    if (region->base_address + region->length <= MEMORY_KERNEL_START) {
        region->usable = false;
        region->type = MEMORY_REGION_RESERVED;
        return MEMORY_VALIDATION_SUCCESS;
    }
    
    // Truncate regions that start below 1MB
    if (region->base_address < MEMORY_KERNEL_START) {
        uint64 adjustment = MEMORY_KERNEL_START - region->base_address;
        if (region->length <= adjustment) {
            region->usable = false;
            region->type = MEMORY_REGION_RESERVED;
            return MEMORY_VALIDATION_SUCCESS;
        }
        region->base_address = MEMORY_KERNEL_START;
        region->length -= adjustment;
    }
    
    // Reject tiny regions
    if (region->length < MEMORY_PAGE_SIZE) {
        region->usable = false;
        region->type = MEMORY_REGION_RESERVED;
        return MEMORY_VALIDATION_SUCCESS;
    }
    
    // Cap regions that exceed our maximum
    if (region->base_address + region->length > MEMORY_MAX_USABLE_BYTES) {
        if (region->base_address >= MEMORY_MAX_USABLE_BYTES) {
            region->usable = false;
            region->type = MEMORY_REGION_RESERVED;
            return MEMORY_VALIDATION_SUCCESS;
        }
        region->length = MEMORY_MAX_USABLE_BYTES - region->base_address;
    }
    
    // Align available regions to page boundaries
    if (region->type == MEMORY_REGION_AVAILABLE) {
        uint64 aligned_base = memory_align_up(region->base_address, MEMORY_PAGE_SIZE);
        uint64 original_end = region->base_address + region->length;
        uint64 aligned_end = memory_align_down(original_end, MEMORY_PAGE_SIZE);
        
        if (aligned_base >= aligned_end) {
            region->usable = false;
            region->type = MEMORY_REGION_RESERVED;
            return MEMORY_VALIDATION_SUCCESS;
        }
        
        region->base_address = aligned_base;
        region->length = aligned_end - aligned_base;
        region->usable = true;
    } else {
        region->usable = false;
    }
    
    region->validated = true;
    return MEMORY_VALIDATION_SUCCESS;
}

static memory_validation_result_t add_safe_region(uint64 base, uint64 length, 
                                                   memory_region_type_t type) {
    if (safe_region_count >= MAX_MEMORY_REGIONS_SAFE) {
        print("[MEMMAP] ERROR: Too many memory regions\n");
        return MEMORY_VALIDATION_OUT_OF_BOUNDS;
    }
    
    memory_region_t new_region = {
        .base_address = base,
        .length = length,
        .type = type,
        .validated = false,
        .usable = false
    };
    
    // Sanitize the region
    memory_validation_result_t result = sanitize_region(&new_region);
    if (result != MEMORY_VALIDATION_SUCCESS) {
        return result;
    }
    
    // Check for conflicts only if region is usable
    if (new_region.usable && !validate_region_for_conflicts(&new_region)) {
        return MEMORY_VALIDATION_REGION_OVERLAP;
    }
    
    // Add the region
    safe_memory_regions[safe_region_count] = new_region;
    safe_region_count++;
    
    // Update usable memory count
    if (new_region.usable && new_region.type == MEMORY_REGION_AVAILABLE) {
        uint32 region_kb = new_region.length / 1024;
        if (total_usable_memory <= MEMORY_MAX_USABLE_KB - region_kb) {
            total_usable_memory += region_kb;
        } else {
            total_usable_memory = MEMORY_MAX_USABLE_KB;
        }
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

static memory_validation_result_t create_fallback_memory_map(void) {
    print("[MEMMAP] Creating fallback memory map\n");
    
    // Clear existing regions
    safe_region_count = 0;
    total_usable_memory = 0;
    
    // Add reserved low memory region
    memory_validation_result_t result = add_safe_region(
        0, MEMORY_KERNEL_START, MEMORY_REGION_RESERVED);
    if (result != MEMORY_VALIDATION_SUCCESS) {
        return result;
    }
    
    // Add usable memory starting at 1MB
    result = add_safe_region(
        MEMORY_KERNEL_START, 32 * 1024 * 1024, MEMORY_REGION_AVAILABLE);
    if (result != MEMORY_VALIDATION_SUCCESS) {
        return result;
    }
    
    print("[MEMMAP] Fallback memory map created (32MB total)\n");
    memory_map_initialized = true; // Set initialized flag for fallback map
    return MEMORY_VALIDATION_SUCCESS;
}

// === PUBLIC MEMORY MAP FUNCTIONS ===

memory_validation_result_t memory_process_multiboot_map(uint32 multiboot_magic, 
                                                         uint32 multiboot_addr) {
    if (memory_map_initialized) {
        print("[MEMMAP] ERROR: Memory map already initialized\n");
        return MEMORY_VALIDATION_INVALID_STATE_TRANSITION;
    }
    
    // Initialize state
    safe_region_count = 0;
    total_usable_memory = 0;
    memory_map_checksum = 0;
    
    print("[MEMMAP] Processing multiboot memory map\n");
    print("[MEMMAP] Magic: ");
    print_hex(multiboot_magic);
    print(" (expected: ");
    print_hex(MULTIBOOT_BOOTLOADER_MAGIC);
    print(")\n");
    
    // Validate multiboot magic
    if (!validate_multiboot_magic(multiboot_magic)) {
        print("[MEMMAP] Invalid multiboot magic, using fallback\n");
        return create_fallback_memory_map();
    }
    
    // Validate multiboot info pointer
    if (!validate_multiboot_info_pointer(multiboot_addr)) {
        print("[MEMMAP] Invalid multiboot info pointer, using fallback\n");
        return create_fallback_memory_map();
    }
    
    multiboot_info_t* mbi = (multiboot_info_t*)multiboot_addr;
    
    print("[MEMMAP] MBI flags: ");
    print_hex(mbi->flags);
    print("\n");
    
    // Process memory map if available
    if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
        print("[MEMMAP] Processing MMAP entries\n");
        
        // Validate memory map range
        if (!is_bootloader_accessible_range(mbi->mmap_addr, mbi->mmap_length)) {
            print("[MEMMAP] Invalid memory map range, using fallback\n");
            return create_fallback_memory_map();
        }
        
        uint32 mmap_end = mbi->mmap_addr + mbi->mmap_length;
        multiboot_mmap_entry_t* entry = (multiboot_mmap_entry_t*)mbi->mmap_addr;
        uint32 processed_entries = 0;
        
        while ((uint32)entry < mmap_end && processed_entries < MAX_MEMORY_REGIONS_SAFE) {
            if (!validate_memory_region_entry(entry)) {
                print("[MEMMAP] Skipping invalid memory map entry\n");
                break;
            }
            
            uint64 base = ((uint64)entry->addr_high << 32) | entry->addr_low;
            uint64 length = ((uint64)entry->len_high << 32) | entry->len_low;
            
            print("[MEMMAP] Entry: ");
            print_hex((uint32)base);
            print(" - ");
            print_hex((uint32)(base + length - 1));
            print(" type=");
            print_dec(entry->type);
            print("\n");
            
            memory_validation_result_t result = add_safe_region(base, length, entry->type);
            if (result != MEMORY_VALIDATION_SUCCESS) {
                print("[MEMMAP] Failed to add region, result=");
                print_dec(result);
                print("\n");
                // Continue processing other regions
            }
            
            // Move to next entry
            entry = (multiboot_mmap_entry_t*)((uint32)entry + entry->size + sizeof(entry->size));
            processed_entries++;
        }
        
    } else if (mbi->flags & MULTIBOOT_FLAG_MEM) {
        print("[MEMMAP] Using basic memory info\n");
        
        // Add low memory region
        if (mbi->mem_lower > 0) {
            memory_validation_result_t result = add_safe_region(
                0, (uint64)mbi->mem_lower * 1024, MEMORY_REGION_AVAILABLE);
            if (result != MEMORY_VALIDATION_SUCCESS) {
                print("[MEMMAP] Failed to add low memory region\n");
            }
        }
        
        // Add high memory region
        if (mbi->mem_upper > 0) {
            memory_validation_result_t result = add_safe_region(
                MEMORY_KERNEL_START, (uint64)mbi->mem_upper * 1024, MEMORY_REGION_AVAILABLE);
            if (result != MEMORY_VALIDATION_SUCCESS) {
                print("[MEMMAP] Failed to add high memory region\n");
            }
        }
        
    } else {
        print("[MEMMAP] No memory information in multiboot, using fallback\n");
        return create_fallback_memory_map();
    }
    
    // Ensure we have at least some usable memory
    if (total_usable_memory < 1024) {  // Less than 1MB
        print("[MEMMAP] Insufficient usable memory detected, adding fallback\n");
        return create_fallback_memory_map();
    }
    
    // Calculate checksum
    memory_map_checksum = MEMORY_MAP_MAGIC;
    for (uint32 i = 0; i < safe_region_count; i++) {
        memory_map_checksum ^= (uint32)safe_memory_regions[i].base_address;
        memory_map_checksum ^= (uint32)safe_memory_regions[i].length;
        memory_map_checksum ^= safe_memory_regions[i].type;
    }
    
    memory_map_initialized = true;
    
    print("[MEMMAP] Memory map initialization complete\n");
    print("[MEMMAP] Total regions: ");
    print_dec(safe_region_count);
    print("\n");
    print("[MEMMAP] Usable memory: ");
    print_dec(total_usable_memory);
    print(" KB\n");
    
    return MEMORY_VALIDATION_SUCCESS;
}

bool memory_map_init(uint32 magic, uint32 mbi_addr) {
    memory_validation_result_t result = memory_process_multiboot_map(magic, mbi_addr);
    return (result == MEMORY_VALIDATION_SUCCESS);
}

bool memory_map_is_ready(void) {
    return memory_map_initialized;
}

memory_region_t* memory_get_regions(uint32* count) {
    if (!memory_map_initialized) {
        if (count) *count = 0;
        return NULL;
    }
    
    // Verify checksum
    uint32 calculated_checksum = MEMORY_MAP_MAGIC;
    for (uint32 i = 0; i < safe_region_count; i++) {
        calculated_checksum ^= (uint32)safe_memory_regions[i].base_address;
        calculated_checksum ^= (uint32)safe_memory_regions[i].length;
        calculated_checksum ^= safe_memory_regions[i].type;
    }
    
    if (calculated_checksum != memory_map_checksum) {
        print("[MEMMAP] ERROR: Memory map checksum mismatch!\n");
        memory_panic_with_details("Memory map corruption", 
                                calculated_checksum, memory_map_checksum);
    }
    
    if (count) *count = safe_region_count;
    return safe_memory_regions;
}

uint32 memory_get_usable_kb(void) {
    if (!memory_map_initialized) {
        return 0;
    }
    return total_usable_memory;
}

memory_validation_result_t memory_check_map_integrity(void) {
    if (!memory_map_initialized) {
        return MEMORY_VALIDATION_INVALID_STATE_TRANSITION;
    }
    
    // Check basic state
    if (safe_region_count > MAX_MEMORY_REGIONS_SAFE) {
        return MEMORY_VALIDATION_OUT_OF_BOUNDS;
    }
    
    // Validate each region
    for (uint32 i = 0; i < safe_region_count; i++) {
        memory_validation_result_t result = memory_validate_region_descriptor(&safe_memory_regions[i]);
        if (result != MEMORY_VALIDATION_SUCCESS) {
            return result;
        }
    }
    
    // Check checksum
    uint32 calculated_checksum = MEMORY_MAP_MAGIC;
    for (uint32 i = 0; i < safe_region_count; i++) {
        calculated_checksum ^= (uint32)safe_memory_regions[i].base_address;
        calculated_checksum ^= (uint32)safe_memory_regions[i].length;
        calculated_checksum ^= safe_memory_regions[i].type;
    }
    
    if (calculated_checksum != memory_map_checksum) {
        return MEMORY_VALIDATION_CORRUPTED_METADATA;
    }
    
    return MEMORY_VALIDATION_SUCCESS;
}

void memory_dump_regions(void) {
    if (!memory_map_initialized) {
        print("[MEMMAP] Memory map not initialized\n");
        return;
    }
    
    print("\n=== MEMORY REGIONS ===\n");
    print("Total regions: ");
    print_dec(safe_region_count);
    print("\n");
    print("Usable memory: ");
    print_dec(total_usable_memory);
    print(" KB\n\n");
    
    for (uint32 i = 0; i < safe_region_count; i++) {
        const memory_region_t* region = &safe_memory_regions[i];
        
        print("Region ");
        print_dec(i);
        print(": ");
        print_hex((uint32)region->base_address);
        print(" - ");
        print_hex((uint32)(region->base_address + region->length - 1));
        print(" (");
        print_dec(region->length / 1024);
        print(" KB) ");
        
        switch (region->type) {
            case MEMORY_REGION_AVAILABLE:
                print("AVAILABLE");
                break;
            case MEMORY_REGION_RESERVED:
                print("RESERVED");
                break;
            case MEMORY_REGION_ACPI_RECLAIM:
                print("ACPI_RECLAIM");
                break;
            case MEMORY_REGION_ACPI_NVS:
                print("ACPI_NVS");
                break;
            case MEMORY_REGION_BADRAM:
                print("BADRAM");
                break;
            default:
                print("INVALID");
                break;
        }
        
        if (region->usable) {
            print(" [USABLE]");
        }
        if (region->validated) {
            print(" [VALIDATED]");
        }
        
        print("\n");
    }
    
    print("=== END MEMORY REGIONS ===\n");
}
