#include "include/memory.h"
#include "include/screen.h"
#include "include/panic.h"

static void print_hex_byte(uint8 byte);
static void print_char(char c);

// =============================================================================
// MEMORY VALIDATION AND DEBUGGING IMPLEMENTATION
// =============================================================================
// Comprehensive debugging and validation functions for memory subsystem
// =============================================================================

// External function declarations
extern memory_stats_t memory_get_stats(void);
extern page_directory_t* vmm_get_current_page_directory(void);
extern uint32 vmm_get_physical_addr(page_directory_t* dir, uint32 vaddr);
extern bool vmm_is_mapped(page_directory_t* dir, uint32 vaddr);

// =============================================================================
// PAGE FAULT HANDLING
// =============================================================================

// Page fault error codes
#define PF_PRESENT      0x01    // Page protection violation
#define PF_WRITE        0x02    // Write access
#define PF_USER         0x04    // User mode access
#define PF_RESERVED     0x08    // Reserved bit violation
#define PF_FETCH        0x10    // Instruction fetch

void page_fault_handler(uint32 fault_addr, uint32 error_code) {
    print("\n=== PAGE FAULT ===\n");
    
    print("Fault Address: 0x");
    print_hex(fault_addr);
    print("\n");
    
    print("Error Code: 0x");
    print_hex(error_code);
    print(" (");
    
    if (error_code & PF_PRESENT) {
        print("Protection violation");
    } else {
        print("Page not present");
    }
    
    if (error_code & PF_WRITE) {
        print(", Write access");
    } else {
        print(", Read access");
    }
    
    if (error_code & PF_USER) {
        print(", User mode");
    } else {
        print(", Kernel mode");
    }
    
    if (error_code & PF_RESERVED) {
        print(", Reserved bit set");
    }
    
    if (error_code & PF_FETCH) {
        print(", Instruction fetch");
    }
    
    print(")\n");
    
    // Try to determine cause
    page_directory_t* current_dir = vmm_get_current_page_directory();
    
    if (fault_addr < MEMORY_KERNEL_START) {
        print("Cause: Access to low memory (null pointer?)\n");
    } else if (fault_addr >= MEMORY_KERNEL_START && fault_addr < MEMORY_KERNEL_HEAP_START) {
        print("Cause: Access to kernel code area\n");
    } else if (fault_addr >= MEMORY_KERNEL_HEAP_START && fault_addr < MEMORY_USER_START) {
        print("Cause: Access to kernel heap area\n");
        
        if (!vmm_is_mapped(current_dir, fault_addr)) {
            print("Note: Heap page not allocated\n");
        }
    } else if (fault_addr >= MEMORY_USER_START) {
        print("Cause: Access to user space\n");
    } else {
        print("Cause: Unknown memory region\n");
    }
    
    // Show current memory statistics
    memory_dump_info();
    
    print("==================\n");
    
    // For now, panic on any page fault
    PANIC("Unhandled page fault");
}

// =============================================================================
// PAGE TABLE DEBUGGING
// =============================================================================

void memory_dump_page_tables(page_directory_t* dir) {
    if (!dir) {
        dir = vmm_get_current_page_directory();
    }
    
    print("\n=== PAGE TABLE DUMP ===\n");
    print("Page Directory at: 0x");
    print_hex((uint32)dir);
    print("\n\n");
    
    uint32 mapped_pages = 0;
    uint32 present_directories = 0;
    
    // Scan page directory
    page_entry_t* pd_entries = (page_entry_t*)dir;
    
    for (int i = 0; i < 1024; i++) {
        if (pd_entries[i].present) {
            present_directories++;
            
            print("PDE ");
            print_dec(i);
            print(": 0x");
            print_hex(pd_entries[i].frame << MEMORY_PAGE_SHIFT);
            
            if (pd_entries[i].user) print(" [U]");
            if (pd_entries[i].writable) print(" [W]");
            if (pd_entries[i].accessed) print(" [A]");
            if (pd_entries[i].dirty) print(" [D]");
            print("\n");
            
            // Scan page table
            page_table_t* page_table = (page_table_t*)(pd_entries[i].frame << MEMORY_PAGE_SHIFT);
            page_entry_t* pt_entries = (page_entry_t*)page_table;
            
            uint32 table_pages = 0;
            for (int j = 0; j < 1024; j++) {
                if (pt_entries[j].present) {
                    table_pages++;
                    mapped_pages++;
                    
                    // Only show first few and last few entries to avoid spam
                    if (table_pages <= 3 || table_pages >= 1021) {
                        uint32 vaddr = (i << 22) | (j << 12);
                        print("  PTE ");
                        print_dec(j);
                        print(": VA=0x");
                        print_hex(vaddr);
                        print(" -> PA=0x");
                        print_hex(pt_entries[j].frame << MEMORY_PAGE_SHIFT);
                        
                        if (pt_entries[j].user) print(" [U]");
                        if (pt_entries[j].writable) print(" [W]");
                        if (pt_entries[j].accessed) print(" [A]");
                        if (pt_entries[j].dirty) print(" [D]");
                        print("\n");
                    } else if (table_pages == 4) {
                        print("  ... (");
                        print_dec(table_pages);
                        print(" more entries) ...\n");
                    }
                }
            }
            
            if (table_pages > 0) {
                print("  Total pages in table: ");
                print_dec(table_pages);
                print("\n");
            }
            print("\n");
        }
    }
    
    print("Summary:\n");
    print("Present page directories: ");
    print_dec(present_directories);
    print("\n");
    print("Total mapped pages: ");
    print_dec(mapped_pages);
    print("\n");
    print("======================\n\n");
}

// =============================================================================
// MEMORY INTEGRITY CHECKING
// =============================================================================

bool memory_check_integrity(void) {
    print("[MEM] Checking memory subsystem integrity...\n");
    
    bool all_good = true;
    
    // Check if we can access memory stats (basic functionality test)
    memory_stats_t stats = memory_get_stats();
    
    if (stats.total_frames == 0) {
        print("[MEM] ERROR: No frames reported by PMM\n");
        all_good = false;
    }
    
    if (stats.free_frames > stats.total_frames) {
        print("[MEM] ERROR: More free frames than total frames\n");
        all_good = false;
    }
    
    if (stats.used_frames + stats.free_frames != stats.total_frames) {
        print("[MEM] ERROR: Frame count mismatch\n");
        all_good = false;
    }
    
    // Test current page directory
    page_directory_t* current_dir = vmm_get_current_page_directory();
    if (!current_dir) {
        print("[MEM] ERROR: No current page directory\n");
        all_good = false;
    } else {
        // Test if we can access the page directory
        page_entry_t* pd_entries = (page_entry_t*)current_dir;
        
        // The first few page directory entries should be present for kernel
        bool found_kernel_mappings = false;
        for (int i = 0; i < 4; i++) {  // Check first 16MB
            if (pd_entries[i].present) {
                found_kernel_mappings = true;
                break;
            }
        }
        
        if (!found_kernel_mappings) {
            print("[MEM] ERROR: No kernel mappings found in page directory\n");
            all_good = false;
        }
    }
    
    // Test heap functionality with a small allocation
    void* test_ptr = kmalloc(64);
    if (!test_ptr) {
        print("[MEM] ERROR: Failed to allocate test memory\n");
        all_good = false;
    } else {
        // Write and read test pattern
        uint32* test_data = (uint32*)test_ptr;
        test_data[0] = 0xDEADBEEF;
        test_data[1] = 0xCAFEBABE;
        
        if (test_data[0] != 0xDEADBEEF || test_data[1] != 0xCAFEBABE) {
            print("[MEM] ERROR: Memory corruption in test allocation\n");
            all_good = false;
        } else {
            print("[MEM] Heap allocation test passed\n");
        }
        
        kfree(test_ptr);
    }
    
    // Test virtual address translation
    uint32 kernel_vaddr = MEMORY_KERNEL_START;
    uint32 physical = vmm_get_physical_addr(current_dir, kernel_vaddr);
    
    if (physical == 0) {
        print("[MEM] ERROR: Kernel virtual address not mapped\n");
        all_good = false;
    } else if (physical != kernel_vaddr) {
        print("[MEM] WARNING: Kernel not identity mapped (expected for higher-half)\n");
    }
    
    if (all_good) {
        print("[MEM] Integrity check passed\n");
    } else {
        print("[MEM] Integrity check FAILED\n");
    }
    
    return all_good;
}

// =============================================================================
// ADVANCED DEBUGGING FUNCTIONS  
// =============================================================================

void memory_trace_allocation(void* ptr) {
    if (!ptr) {
        print("[TRACE] NULL pointer\n");
        return;
    }
    
    print("[TRACE] Memory at 0x");
    print_hex((uint32)ptr);
    print(":\n");
    
    // Check if address is mapped
    page_directory_t* current_dir = vmm_get_current_page_directory();
    uint32 physical = vmm_get_physical_addr(current_dir, (uint32)ptr);
    
    if (physical == 0) {
        print("  Status: NOT MAPPED\n");
        return;
    }
    
    print("  Physical: 0x");
    print_hex(physical);
    print("\n");
    
    // Determine memory region
    if ((uint32)ptr < MEMORY_KERNEL_START) {
        print("  Region: Low memory\n");
    } else if ((uint32)ptr < MEMORY_KERNEL_HEAP_START) {
        print("  Region: Kernel code/data\n");
    } else if ((uint32)ptr < MEMORY_USER_START) {
        print("  Region: Kernel heap\n");
    } else {
        print("  Region: User space\n");
    }
    
    // Check alignment
    if (memory_is_aligned((uint32)ptr, 4)) {
        print("  Alignment: 4-byte aligned\n");
    } else {
        print("  Alignment: Not aligned\n");
    }
    
    if (memory_is_aligned((uint32)ptr, MEMORY_PAGE_SIZE)) {
        print("  Page alignment: Page aligned\n");
    }
}

void memory_dump_range(uint32 start, uint32 length) {
    print("\n=== MEMORY DUMP ===\n");
    print("Range: 0x");
    print_hex(start);
    print(" - 0x");
    print_hex(start + length);
    print("\n\n");
    
    uint32 end = start + length;
    start = memory_align_down(start, 16); // Align to 16-byte boundary for nice display
    
    for (uint32 addr = start; addr < end; addr += 16) {
        print("0x");
        print_hex(addr);
        print(": ");
        
        // Hex dump
        for (int i = 0; i < 16; i++) {
            uint32 byte_addr = addr + i;
            if (byte_addr >= start && byte_addr < end) {
                // Check if address is mapped before reading
                page_directory_t* current_dir = vmm_get_current_page_directory();
                if (vmm_is_mapped(current_dir, byte_addr)) {
                    uint8 byte = *(uint8*)byte_addr;
                    if (byte < 16) print("0");
                    print_hex_byte(byte);
                } else {
                    print("??");
                }
            } else {
                print("  ");
            }
            print(" ");
        }
        
        print(" |");
        
        // ASCII dump
        for (int i = 0; i < 16; i++) {
            uint32 byte_addr = addr + i;
            if (byte_addr >= start && byte_addr < end) {
                page_directory_t* current_dir = vmm_get_current_page_directory();
                if (vmm_is_mapped(current_dir, byte_addr)) {
                    uint8 byte = *(uint8*)byte_addr;
                    if (byte >= 32 && byte <= 126) {
                        print_char((char)byte);
                    } else {
                        print(".");
                    }
                } else {
                    print("?");
                }
            } else {
                print(" ");
            }
        }
        
        print("|\n");
    }
    
    print("===================\n\n");
}

// Helper function for hex byte printing
static void print_hex_byte(uint8 byte) {
    const char* hex = "0123456789ABCDEF";
    print_char(hex[(byte >> 4) & 0xF]);
    print_char(hex[byte & 0xF]);
}

static void print_char(char c) {
    printch(c);
}
