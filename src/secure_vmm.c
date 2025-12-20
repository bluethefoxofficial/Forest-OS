#include "include/secure_vmm.h"
#include "include/bitmap_pmm.h"
#include "include/tlb_manager.h"
#include "include/memory_corruption.h"
#include "include/screen.h"
#include "include/memory.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// SECURE VIRTUAL MEMORY MANAGER IMPLEMENTATION
// =============================================================================
// Advanced virtual memory management with comprehensive protection features
// =============================================================================

// Global VMM state
static struct {
    bool initialized;
    vmm_config_t config;
    vmm_address_space_t* kernel_space;
    vmm_address_space_t* current_space;
    vmm_stats_t stats;
    uint32_t next_allocation_id;
    uint32_t time_counter;
    
    // Page fault handler
    int (*page_fault_handler)(uint32_t addr, uint32_t error);
    
} vmm_state = {0};

// Emergency reporting for critical VMM errors
static void vmm_emergency_report(const char* msg, uint32_t addr) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    int row = 7; // Use row 7 for VMM errors
    int col = 0;
    
    while (*msg && col < 80) {
        vga[row * 80 + col] = (0x4E << 8) | *msg; // Yellow on red
        msg++;
        col++;
    }
}

// Calculate address space checksum for corruption detection
static uint32_t calculate_address_space_checksum(vmm_address_space_t* space) {
    if (!space) return 0;
    
    uint32_t checksum = 0;
    checksum ^= space->magic_header;
    checksum ^= space->page_directory_phys;
    checksum ^= (uint32_t)space->page_directory_virt;
    checksum ^= space->type;
    checksum ^= space->area_count;
    checksum ^= space->total_pages;
    checksum ^= space->magic_footer;
    
    // Include area checksums
    vmm_area_t* area = space->areas_head;
    while (area) {
        checksum ^= area->checksum;
        area = area->next;
    }
    
    return checksum;
}

// Calculate area checksum for corruption detection
static uint32_t calculate_area_checksum(vmm_area_t* area) {
    if (!area) return 0;
    
    uint32_t checksum = 0;
    checksum ^= area->magic;
    checksum ^= area->start_addr;
    checksum ^= area->end_addr;
    checksum ^= area->protection;
    checksum ^= area->type;
    checksum ^= area->flags;
    checksum ^= area->allocation_id;
    
    return checksum;
}

// Create and initialize page directory
static page_directory_t* create_page_directory(vmm_address_space_type_t type) {
    if (type == VMM_SPACE_KERNEL) {
        return vmm_get_current_page_directory();
    }
    
    // For non-kernel spaces, clone the current kernel directory so they inherit
    // the kernel identity mappings.
    return vmm_create_page_directory();
}

static inline page_directory_t* get_space_directory(vmm_address_space_t* space) {
    return (page_directory_t*)space->page_directory_virt;
}

// Initialize secure VMM
void secure_vmm_init(const vmm_config_t* config) {
    print("[SECURE_VMM] Initializing secure virtual memory manager...\n");
    
    if (config) {
        vmm_state.config = *config;
    } else {
        // Default configuration
        vmm_state.config.corruption_detection_enabled = true;
        vmm_state.config.access_tracking_enabled = true;
        vmm_state.config.guard_pages_enabled = true;
        vmm_state.config.aslr_enabled = false; // Disabled for kernel space
        vmm_state.config.dep_enabled = true;
        vmm_state.config.debug_mode_enabled = false;
        vmm_state.config.kernel_heap_start = 0x10000000;  // 256MB
        vmm_state.config.kernel_heap_size = 64 * 1024 * 1024; // 64MB
        vmm_state.config.user_space_start = 0x40000000;   // 1GB
        vmm_state.config.user_space_size = 1024 * 1024 * 1024; // 1GB
    }
    
    // Initialize statistics
    vmm_state.stats.total_mappings = 0;
    vmm_state.stats.active_mappings = 0;
    vmm_state.stats.total_page_faults = 0;
    vmm_state.stats.protection_violations = 0;
    vmm_state.stats.guard_page_hits = 0;
    vmm_state.stats.corruption_detected = 0;
    
    vmm_state.next_allocation_id = 1;
    vmm_state.time_counter = 1;
    
    // Create kernel address space
    vmm_state.kernel_space = secure_vmm_create_address_space(VMM_SPACE_KERNEL);
    if (!vmm_state.kernel_space) {
        vmm_emergency_report("Failed to create kernel address space", 0);
        return;
    }
    
    vmm_state.current_space = vmm_state.kernel_space;
    vmm_state.initialized = true;
    
    print("[SECURE_VMM] Virtual memory manager initialized with protection features\n");
    print("  Corruption detection: ");
    print(vmm_state.config.corruption_detection_enabled ? "enabled" : "disabled");
    print("\n");
    print("  Guard pages: ");
    print(vmm_state.config.guard_pages_enabled ? "enabled" : "disabled");
    print("\n");
    print("  DEP: ");
    print(vmm_state.config.dep_enabled ? "enabled" : "disabled");
    print("\n");
}

// Create new address space
vmm_address_space_t* secure_vmm_create_address_space(vmm_address_space_type_t type) {
    if (!vmm_state.initialized && type != VMM_SPACE_KERNEL) {
        return NULL;
    }
    
    // Allocate address space structure
    vmm_address_space_t* space = (vmm_address_space_t*)SAFE_MALLOC(sizeof(vmm_address_space_t));
    if (!space) {
        return NULL;
    }
    
    // Create page directory
    page_directory_t* dir = create_page_directory(type);
    if (!dir) {
        SAFE_FREE(space);
        return NULL;
    }
    space->page_directory_virt = (uint32_t*)dir;
    space->page_directory_phys = (uint32_t)dir;
    
    // Initialize address space
    space->magic_header = VMM_MAGIC_SPACE_HEADER;
    space->magic_footer = VMM_MAGIC_SPACE_FOOTER;
    space->type = type;
    space->areas_head = NULL;
    space->area_count = 0;
    space->total_pages = 0;
    space->user_pages = 0;
    space->kernel_pages = 0;
    space->guard_pages = 0;
    space->page_faults = 0;
    
    // Configure security features based on type
    if (type == VMM_SPACE_USER) {
        space->aslr_enabled = vmm_state.config.aslr_enabled;
        space->dep_enabled = vmm_state.config.dep_enabled;
        space->stack_guard_enabled = vmm_state.config.guard_pages_enabled;
        space->heap_guard_enabled = vmm_state.config.guard_pages_enabled;
    } else {
        space->aslr_enabled = false; // No ASLR for kernel space
        space->dep_enabled = vmm_state.config.dep_enabled;
        space->stack_guard_enabled = false; // Kernel manages its own stack
        space->heap_guard_enabled = vmm_state.config.guard_pages_enabled;
    }
    
    // Calculate checksum
    space->checksum = calculate_address_space_checksum(space);
    
    print("[SECURE_VMM] Created ");
    switch (type) {
        case VMM_SPACE_KERNEL:
            print("kernel");
            break;
        case VMM_SPACE_USER:
            print("user");
            break;
        case VMM_SPACE_SHARED:
            print("shared");
            break;
        case VMM_SPACE_DEVICE:
            print("device");
            break;
    }
    print(" address space at 0x");
    print_hex((uint32_t)space);
    print("\n");
    
    return space;
}

// Create memory area
vmm_area_t* secure_vmm_create_area(vmm_address_space_t* space, uint32_t start, uint32_t size,
                                  vmm_protection_t protection, vmm_mapping_type_t type,
                                  const char* description) {
    if (!space || !VMM_IS_PAGE_ALIGNED(start) || size == 0) {
        return NULL;
    }
    
    // Allocate area structure
    vmm_area_t* area = (vmm_area_t*)SAFE_MALLOC(sizeof(vmm_area_t));
    if (!area) {
        return NULL;
    }
    
    // Initialize area
    area->magic = VMM_MAGIC_AREA;
    area->start_addr = start;
    area->end_addr = start + size;
    area->protection = protection;
    area->type = type;
    area->flags = 0;
    area->next = NULL;
    area->prev = NULL;
    area->access_count = 0;
    area->fault_count = 0;
    area->creation_time = ++vmm_state.time_counter;
    area->description = description;
    area->allocation_id = ++vmm_state.next_allocation_id;
    
    // Calculate checksum
    area->checksum = calculate_area_checksum(area);
    
    // Add to address space area list
    if (space->areas_head) {
        space->areas_head->prev = area;
        area->next = space->areas_head;
    }
    space->areas_head = area;
    space->area_count++;
    
    // Update address space checksum
    space->checksum = calculate_address_space_checksum(space);
    
    return area;
}

// Map pages in address space
int secure_vmm_map_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t paddr, 
                        uint32_t count, vmm_protection_t protection) {
    if (!space || !VMM_IS_PAGE_ALIGNED(vaddr) || !VMM_IS_PAGE_ALIGNED(paddr) || count == 0) {
        return -1;
    }
    
    page_directory_t* dir = get_space_directory(space);
    uint32_t current_vaddr = vaddr;
    uint32_t current_paddr = paddr;
    
    for (uint32_t i = 0; i < count; i++) {
        memory_result_t map_res = vmm_map_page(dir, current_vaddr, current_paddr, protection);
        if (map_res != MEMORY_OK && map_res != MEMORY_ERROR_ALREADY_MAPPED) {
            vmm_emergency_report("Failed to map page", current_vaddr);
            return -1;
        }
        
        if (space == vmm_state.current_space) {
            tlb_invalidate_page(current_vaddr);
        }
        
        // Update statistics
        space->total_pages++;
        if (protection & VMM_PAGE_USER) {
            space->user_pages++;
        } else {
            space->kernel_pages++;
        }
        
        current_vaddr += 4096;
        current_paddr += 4096;
    }
    
    vmm_state.stats.total_mappings++;
    vmm_state.stats.active_mappings++;
    vmm_state.stats.pages_allocated += count;
    
    // Update address space checksum
    space->checksum = calculate_address_space_checksum(space);
    
    return 0;
}

// Unmap pages from address space
int secure_vmm_unmap_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count) {
    if (!space || !VMM_IS_PAGE_ALIGNED(vaddr) || count == 0) {
        return -1;
    }
    
    page_directory_t* dir = get_space_directory(space);
    uint32_t current_vaddr = vaddr;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t phys_addr = vmm_get_physical_addr(dir, current_vaddr);
        if (phys_addr) {
            uint32_t page_frame = bitmap_pmm_addr_to_page(phys_addr);
            bitmap_pmm_free_page(page_frame);
        }
        
        vmm_unmap_page(dir, current_vaddr);
        
        if (space == vmm_state.current_space) {
            tlb_invalidate_page(current_vaddr);
        }
        
        if (space->total_pages > 0) {
            space->total_pages--;
        }
        vmm_area_t* owner = secure_vmm_find_area(space, current_vaddr);
        if (owner && (owner->protection & VMM_PAGE_USER)) {
            if (space->user_pages > 0) {
                space->user_pages--;
            }
        } else if (space->kernel_pages > 0) {
            space->kernel_pages--;
        }
        
        current_vaddr += 4096;
    }
    
    vmm_state.stats.active_mappings--;
    vmm_state.stats.pages_freed += count;
    
    // Update address space checksum
    space->checksum = calculate_address_space_checksum(space);
    
    return 0;
}

// Allocate virtual memory region
uint32_t secure_vmm_allocate_region(vmm_address_space_t* space, uint32_t size, 
                                   vmm_protection_t protection, vmm_mapping_type_t type) {
    if (!space || size == 0) {
        return 0;
    }
    
    uint32_t pages_needed = VMM_PAGES_FOR_SIZE(size);
    
    // Find free virtual address space
    // This is a simplified implementation - a real VMM would use sophisticated algorithms
    uint32_t search_start = (space->type == VMM_SPACE_USER) ? 
                           vmm_state.config.user_space_start : 
                           vmm_state.config.kernel_heap_start;
    
    uint32_t vaddr = search_start;
    bool found = false;
    
    // Simple linear search for free space
    for (uint32_t addr = search_start; addr < search_start + (space->type == VMM_SPACE_USER ? 
                                                             vmm_state.config.user_space_size : 
                                                             vmm_state.config.kernel_heap_size); 
         addr += 4096) {
        
        // Check if pages_needed consecutive pages are free
        bool all_free = true;
        for (uint32_t i = 0; i < pages_needed; i++) {
            if (secure_vmm_is_mapped(space, addr + (i * 4096))) {
                all_free = false;
                break;
            }
        }
        
        if (all_free) {
            vaddr = addr;
            found = true;
            break;
        }
    }
    
    if (!found) {
        vmm_emergency_report("No free virtual address space", size);
        return 0;
    }
    
    // Allocate physical pages and map them
    for (uint32_t i = 0; i < pages_needed; i++) {
        uint32_t page_frame = bitmap_pmm_alloc_page(PMM_ALLOC_ANY_MEMORY);
        if (page_frame == 0) {
            // Cleanup allocated pages on failure
            for (uint32_t j = 0; j < i; j++) {
                secure_vmm_unmap_pages(space, vaddr + (j * 4096), 1);
            }
            return 0;
        }
        
        uint32_t phys_addr = bitmap_pmm_page_to_addr(page_frame);
        if (secure_vmm_map_pages(space, vaddr + (i * 4096), phys_addr, 1, protection) != 0) {
            // Cleanup on failure
            bitmap_pmm_free_page(page_frame);
            for (uint32_t j = 0; j < i; j++) {
                secure_vmm_unmap_pages(space, vaddr + (j * 4096), 1);
            }
            return 0;
        }
    }
    
    // Create memory area
    const char* type_desc;
    switch (type) {
        case VMM_MAP_ANONYMOUS: type_desc = "anonymous"; break;
        case VMM_MAP_HEAP: type_desc = "heap"; break;
        case VMM_MAP_STACK: type_desc = "stack"; break;
        case VMM_MAP_GUARD: type_desc = "guard"; break;
        default: type_desc = "unknown"; break;
    }
    
    vmm_area_t* area = secure_vmm_create_area(space, vaddr, size, protection, type, type_desc);
    if (!area) {
        // Cleanup on failure
        secure_vmm_unmap_pages(space, vaddr, pages_needed);
        return 0;
    }
    
    return vaddr;
}

// Find memory area containing virtual address
vmm_area_t* secure_vmm_find_area(vmm_address_space_t* space, uint32_t vaddr) {
    if (!space) {
        return NULL;
    }
    
    vmm_area_t* area = space->areas_head;
    while (area) {
        if (vaddr >= area->start_addr && vaddr < area->end_addr) {
            return area;
        }
        area = area->next;
    }
    
    return NULL;
}

// Check if virtual address is mapped
bool secure_vmm_is_mapped(vmm_address_space_t* space, uint32_t vaddr) {
    if (!space) {
        return false;
    }
    
    uint32_t pd_index = vaddr >> 22;
    uint32_t pt_index = (vaddr >> 12) & 0x3FF;
    
    // Check if page directory entry is present
    if (!(space->page_directory_virt[pd_index] & VMM_PAGE_PRESENT)) {
        return false;
    }
    
    // Check if page table entry is present
    uint32_t pt_phys_addr = space->page_directory_virt[pd_index] & ~0xFFF;
    uint32_t* page_table = (uint32_t*)pt_phys_addr;
    
    return (page_table[pt_index] & VMM_PAGE_PRESENT) != 0;
}

// Validate address space integrity
bool secure_vmm_validate_address_space(vmm_address_space_t* space) {
    if (!space) {
        return false;
    }
    
    // Check magic numbers
    if (space->magic_header != VMM_MAGIC_SPACE_HEADER ||
        space->magic_footer != VMM_MAGIC_SPACE_FOOTER) {
        return false;
    }
    
    // Check checksum if corruption detection enabled
    if (vmm_state.config.corruption_detection_enabled) {
        uint32_t calculated_checksum = calculate_address_space_checksum(space);
        if (calculated_checksum != space->checksum) {
            vmm_state.stats.corruption_detected++;
            return false;
        }
    }
    
    // Validate all memory areas
    vmm_area_t* area = space->areas_head;
    while (area) {
        if (area->magic != VMM_MAGIC_AREA) {
            return false;
        }
        
        if (vmm_state.config.corruption_detection_enabled) {
            uint32_t area_checksum = calculate_area_checksum(area);
            if (area_checksum != area->checksum) {
                return false;
            }
        }
        
        area = area->next;
    }
    
    return true;
}

// Get VMM statistics
void secure_vmm_get_stats(vmm_stats_t* stats) {
    if (stats) {
        *stats = vmm_state.stats;
    }
}

// Dump VMM statistics
void secure_vmm_dump_stats(void) {
    print("\n=== Secure VMM Statistics ===\n");
    print("Total mappings: ");
    print_dec(vmm_state.stats.total_mappings);
    print("\n");
    
    print("Active mappings: ");
    print_dec(vmm_state.stats.active_mappings);
    print("\n");
    
    print("Total page faults: ");
    print_dec(vmm_state.stats.total_page_faults);
    print("\n");
    
    print("Protection violations: ");
    print_dec(vmm_state.stats.protection_violations);
    print("\n");
    
    print("Guard page hits: ");
    print_dec(vmm_state.stats.guard_page_hits);
    print("\n");
    
    print("Corruption detected: ");
    print_dec(vmm_state.stats.corruption_detected);
    print("\n");
    
    print("Pages allocated: ");
    print_dec(vmm_state.stats.pages_allocated);
    print("\n");
    
    print("Pages freed: ");
    print_dec(vmm_state.stats.pages_freed);
    print("\n");
}

// Dump address space information
void secure_vmm_dump_address_space(vmm_address_space_t* space) {
    if (!space) {
        print("[SECURE_VMM] Invalid address space\n");
        return;
    }
    
    print("\n=== Address Space Information ===\n");
    print("Type: ");
    switch (space->type) {
        case VMM_SPACE_KERNEL: print("Kernel"); break;
        case VMM_SPACE_USER: print("User"); break;
        case VMM_SPACE_SHARED: print("Shared"); break;
        case VMM_SPACE_DEVICE: print("Device"); break;
        default: print("Unknown"); break;
    }
    print("\n");
    
    print("Page directory (phys): 0x");
    print_hex(space->page_directory_phys);
    print("\n");
    
    print("Page directory (virt): 0x");
    print_hex((uint32_t)space->page_directory_virt);
    print("\n");
    
    print("Total pages: ");
    print_dec(space->total_pages);
    print("\n");
    
    print("User pages: ");
    print_dec(space->user_pages);
    print("\n");
    
    print("Kernel pages: ");
    print_dec(space->kernel_pages);
    print("\n");
    
    print("Guard pages: ");
    print_dec(space->guard_pages);
    print("\n");
    
    print("Memory areas: ");
    print_dec(space->area_count);
    print("\n");
    
    print("Security features:\n");
    print("  ASLR: ");
    print(space->aslr_enabled ? "enabled" : "disabled");
    print("\n");
    print("  DEP: ");
    print(space->dep_enabled ? "enabled" : "disabled");
    print("\n");
    print("  Stack guard: ");
    print(space->stack_guard_enabled ? "enabled" : "disabled");
    print("\n");
    print("  Heap guard: ");
    print(space->heap_guard_enabled ? "enabled" : "disabled");
    print("\n");
}

// Dump memory areas
void secure_vmm_dump_memory_areas(vmm_address_space_t* space) {
    if (!space) {
        return;
    }
    
    print("\n=== Memory Areas ===\n");
    
    vmm_area_t* area = space->areas_head;
    int area_num = 0;
    
    while (area) {
        print("Area ");
        print_dec(area_num++);
        print(": 0x");
        print_hex(area->start_addr);
        print(" - 0x");
        print_hex(area->end_addr);
        print(" (");
        print_hex(area->end_addr - area->start_addr);
        print(" bytes)\n");
        
        print("  Type: ");
        switch (area->type) {
            case VMM_MAP_ANONYMOUS: print("anonymous"); break;
            case VMM_MAP_FILE: print("file"); break;
            case VMM_MAP_DEVICE: print("device"); break;
            case VMM_MAP_SHARED: print("shared"); break;
            case VMM_MAP_GUARD: print("guard"); break;
            default: print("unknown"); break;
        }
        print("\n");
        
        print("  Protection: ");
        if (area->protection & VMM_PAGE_PRESENT) print("P");
        if (area->protection & VMM_PAGE_WRITABLE) print("W");
        if (area->protection & VMM_PAGE_USER) print("U");
        print("\n");
        
        if (area->description) {
            print("  Description: ");
            print(area->description);
            print("\n");
        }
        
        print("  Access count: ");
        print_dec(area->access_count);
        print("\n");
        
        area = area->next;
    }
}

void secure_vmm_enable_protection_features(bool aslr, bool dep, bool guards) {
    vmm_state.config.aslr_enabled = aslr;
    vmm_state.config.dep_enabled = dep;
    vmm_state.config.guard_pages_enabled = guards;
    
    if (vmm_state.kernel_space) {
        vmm_state.kernel_space->aslr_enabled = false;
        vmm_state.kernel_space->dep_enabled = dep;
        vmm_state.kernel_space->heap_guard_enabled = guards;
    }
}

void secure_vmm_destroy_address_space(vmm_address_space_t* space) {
    if (!space || space == vmm_state.kernel_space) {
        return;
    }
    
    vmm_area_t* area = space->areas_head;
    while (area) {
        vmm_area_t* next = area->next;
        uint32_t size = area->end_addr - area->start_addr;
        if (size) {
            secure_vmm_unmap_pages(space, area->start_addr, VMM_PAGES_FOR_SIZE(size));
        }
        SAFE_FREE(area);
        area = next;
    }
    
    vmm_destroy_page_directory(get_space_directory(space));
    SAFE_FREE(space);
}

int secure_vmm_protect_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count,
                             vmm_protection_t protection) {
    if (!space || count == 0 || !VMM_IS_PAGE_ALIGNED(vaddr)) {
        return -1;
    }
    
    page_directory_t* dir = get_space_directory(space);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t addr = vaddr + (i * MEMORY_PAGE_SIZE);
        uint32_t phys = vmm_get_physical_addr(dir, addr);
        if (!phys) {
            return -1;
        }
        
        vmm_unmap_page(dir, addr);
        if (vmm_map_page(dir, addr, phys, protection) != MEMORY_OK) {
            return -1;
        }
        if (space == vmm_state.current_space) {
            tlb_invalidate_page(addr);
        }
    }
    
    vmm_area_t* area = secure_vmm_find_area(space, vaddr);
    if (area) {
        area->protection = protection;
        area->checksum = calculate_area_checksum(area);
    }
    
    return 0;
}

int secure_vmm_deallocate_region(vmm_address_space_t* space, uint32_t vaddr, uint32_t size) {
    if (!space || size == 0) {
        return -1;
    }
    
    vmm_area_t* area = secure_vmm_find_area(space, vaddr);
    if (!area) {
        return -1;
    }
    
    uint32_t pages = VMM_PAGES_FOR_SIZE(area->end_addr - area->start_addr);
    secure_vmm_unmap_pages(space, area->start_addr, pages);
    secure_vmm_remove_area(space, area);
    SAFE_FREE(area);
    return 0;
}

int secure_vmm_create_guard_pages(vmm_address_space_t* space, uint32_t vaddr, uint32_t count) {
    if (!space || count == 0 || !VMM_IS_PAGE_ALIGNED(vaddr)) {
        return -1;
    }
    
    secure_vmm_unmap_pages(space, vaddr, count);
    vmm_area_t* area = secure_vmm_create_area(
        space, vaddr, count * MEMORY_PAGE_SIZE, VMM_PROT_GUARD, VMM_MAP_GUARD, "guard");
    if (!area) {
        return -1;
    }
    
    area->flags |= VMM_PROT_GUARD;
    area->checksum = calculate_area_checksum(area);
    space->guard_pages += count;
    return 0;
}

int secure_vmm_remove_area(vmm_address_space_t* space, vmm_area_t* area) {
    if (!space || !area) {
        return -1;
    }
    
    if (area->prev) {
        area->prev->next = area->next;
    }
    if (area->next) {
        area->next->prev = area->prev;
    }
    if (space->areas_head == area) {
        space->areas_head = area->next;
    }
    space->area_count--;
    space->checksum = calculate_address_space_checksum(space);
    return 0;
}

uint32_t secure_vmm_virt_to_phys(vmm_address_space_t* space, uint32_t vaddr) {
    if (!space) {
        return 0;
    }
    return vmm_get_physical_addr(get_space_directory(space), vaddr);
}

vmm_protection_t secure_vmm_get_protection(vmm_address_space_t* space, uint32_t vaddr) {
    vmm_area_t* area = secure_vmm_find_area(space, vaddr);
    if (!area) {
        return VMM_PROT_NONE;
    }
    return area->protection;
}

bool secure_vmm_validate_mapping(vmm_address_space_t* space, uint32_t vaddr) {
    if (!space) {
        return false;
    }
    if (!secure_vmm_is_mapped(space, vaddr)) {
        return false;
    }
    return secure_vmm_find_area(space, vaddr) != NULL;
}

bool secure_vmm_check_corruption(vmm_address_space_t* space) {
    return secure_vmm_validate_address_space(space);
}

void secure_vmm_scan_for_corruption(vmm_address_space_t* space) {
    if (!space || !vmm_state.config.corruption_detection_enabled) {
        return;
    }
    
    if (!secure_vmm_validate_address_space(space)) {
        print("[SECURE_VMM] Corruption detected in address space!\n");
    }
}

int secure_vmm_handle_page_fault(uint32_t fault_addr, uint32_t error_code) {
    if (!vmm_state.initialized || !vmm_state.current_space) {
        return -1;
    }
    
    vmm_state.stats.total_page_faults++;
    vmm_address_space_t* space = vmm_state.current_space;
    vmm_area_t* area = secure_vmm_find_area(space, fault_addr);
    
    if (!area) {
        vmm_state.stats.protection_violations++;
        return -1;
    }
    
    area->fault_count++;
    
    if (area->protection & VMM_PROT_GUARD) {
        vmm_state.stats.guard_page_hits++;
        return -1;
    }
    
    if (vmm_state.page_fault_handler) {
        return vmm_state.page_fault_handler(fault_addr, error_code);
    }
    
    return -1;
}

void secure_vmm_register_fault_handler(int (*handler)(uint32_t addr, uint32_t error)) {
    vmm_state.page_fault_handler = handler;
}

int secure_vmm_enable_aslr(vmm_address_space_t* space) {
    if (!space) {
        return -1;
    }
    space->aslr_enabled = true;
    return 0;
}

int secure_vmm_enable_dep(vmm_address_space_t* space) {
    if (!space) {
        return -1;
    }
    space->dep_enabled = true;
    return 0;
}

int secure_vmm_create_stack_guard(vmm_address_space_t* space, uint32_t stack_base, uint32_t stack_size) {
    if (!space || stack_size == 0) {
        return -1;
    }
    return secure_vmm_create_guard_pages(space, stack_base - MEMORY_PAGE_SIZE, 1);
}

int secure_vmm_create_heap_guard(vmm_address_space_t* space, uint32_t heap_base, uint32_t heap_size) {
    if (!space || heap_size == 0) {
        return -1;
    }
    return secure_vmm_create_guard_pages(space, heap_base + heap_size, 1);
}

int secure_vmm_setup_cow_mapping(vmm_address_space_t* space, uint32_t vaddr, uint32_t count) {
    (void)space;
    (void)vaddr;
    (void)count;
    print("[SECURE_VMM] Copy-on-write not implemented\n");
    return -1;
}

int secure_vmm_handle_cow_fault(vmm_address_space_t* space, uint32_t fault_addr) {
    (void)space;
    (void)fault_addr;
    return -1;
}

void secure_vmm_dump_page_tables(vmm_address_space_t* space) {
    if (!space) {
        return;
    }
    memory_dump_page_tables((page_directory_t*)space->page_directory_virt);
}

void secure_vmm_verify_page_table_integrity(vmm_address_space_t* space) {
    if (!secure_vmm_check_corruption(space)) {
        print("[SECURE_VMM] Page table integrity check failed\n");
    }
}

void secure_vmm_trace_memory_access(uint32_t vaddr, const char* operation) {
    if (!vmm_state.config.debug_mode_enabled) {
        return;
    }
    
    print("[SECURE_VMM] ");
    if (operation) {
        print(operation);
        print(" ");
    }
    print("access at 0x");
    print_hex(vaddr);
    print("\n");
}
