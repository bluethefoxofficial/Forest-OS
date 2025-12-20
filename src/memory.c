#include "include/memory.h"
#include "include/screen.h"
#include "include/string.h"
#include "include/panic.h"
#include "include/multiboot.h"
#include "include/interrupt.h"

// =============================================================================
// MEMORY DETECTION AND INITIALIZATION
// =============================================================================
// Handles GRUB memory map parsing and complete memory subsystem initialization
// =============================================================================

#define MULTIBOOT_MAGIC         MULTIBOOT_BOOTLOADER_MAGIC
#define MULTIBOOT2_MAGIC        MULTIBOOT2_BOOTLOADER_MAGIC

// Memory detection state
static struct {
    bool detected;
    memory_region_t regions[32];  // Support up to 32 memory regions
    uint32 region_count;
    uint32 total_memory_kb;
    uint32 usable_memory_kb;
} memory_info = {0};

static void memory_panic_stage(const char* stage, memory_result_t result) {
    static char panic_message[192];
    strcpy(panic_message, "Memory subsystem failure during ");
    strcat(panic_message, stage);
    if (result != MEMORY_OK) {
        strcat(panic_message, " (");
        strcat(panic_message, memory_result_to_string(result));
        strcat(panic_message, ")");
    }
    kernel_panic(panic_message);
}

// External function declarations
extern memory_result_t pmm_init(memory_region_t* regions, uint32 region_count);
extern memory_result_t vmm_init(void);
extern memory_result_t heap_init(uint32 start_addr, uint32 initial_size);
extern memory_result_t vmm_identity_map_range(page_directory_t* dir, uint32 start, uint32 end, uint32 flags);
extern page_directory_t* vmm_get_current_page_directory(void);
extern void vmm_enable_paging(void);
extern void page_fault_handler(uint32 fault_addr, uint32 error_code);

static memory_result_t parse_multiboot1_info(multiboot_info_t* mbi);
static memory_result_t parse_multiboot2_info(uint32 info_addr);
static void reset_region_info(void);
static void add_memory_region(uint64 base, uint64 length, uint32 type);
static void print_basic_memory(uint32 lower_mem, uint32 upper_mem);
static memory_result_t unmap_identity_range(page_directory_t* dir, uint32 start, uint32 end);
static void memory_page_fault_wrapper(struct interrupt_frame* frame, uint32 error_code);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

uint32 memory_align_up(uint32 addr, uint32 align) {
    if (align == 0 || (align & (align - 1)) != 0) {
        return addr; // Invalid alignment
    }
    return (addr + align - 1) & ~(align - 1);
}

uint32 memory_align_down(uint32 addr, uint32 align) {
    if (align == 0 || (align & (align - 1)) != 0) {
        return addr; // Invalid alignment
    }
    return addr & ~(align - 1);
}

bool memory_is_aligned(uint32 addr, uint32 align) {
    if (align == 0 || (align & (align - 1)) != 0) {
        return false; // Invalid alignment
    }
    return (addr & (align - 1)) == 0;
}

const char* memory_result_to_string(memory_result_t result) {
    switch (result) {
        case MEMORY_OK: return "Success";
        case MEMORY_ERROR_NULL_PTR: return "Null pointer";
        case MEMORY_ERROR_INVALID_ADDR: return "Invalid address";
        case MEMORY_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case MEMORY_ERROR_ALREADY_MAPPED: return "Already mapped";
        case MEMORY_ERROR_NOT_MAPPED: return "Not mapped";
        case MEMORY_ERROR_INVALID_SIZE: return "Invalid size";
        case MEMORY_ERROR_NOT_INITIALIZED: return "Not initialized";
        default: return "Unknown error";
    }
}

// =============================================================================
// MEMORY MAP PARSING
// =============================================================================

// Parse GRUB memory map
memory_result_t memory_detect_grub(uint32 multiboot_magic, uint32 multiboot_info) {
    print("[MEM] Detecting memory via GRUB...\n");
    
    memory_info.region_count = 0;
    memory_info.usable_memory_kb = 0;
    memory_info.total_memory_kb = 0;
    
    memory_result_t result;
    
    if (multiboot_magic == MULTIBOOT_MAGIC) {
        result = parse_multiboot1_info((multiboot_info_t*)multiboot_info);
    } else if (multiboot_magic == MULTIBOOT2_MAGIC) {
        print("[MEM] Detected Multiboot2 bootloader\n");
        result = parse_multiboot2_info(multiboot_info);
    } else {
        print("[MEM] Unsupported multiboot magic: 0x");
        print_hex(multiboot_magic);
        print("\n");
        return MEMORY_ERROR_INVALID_ADDR;
    }
    
    if (result != MEMORY_OK) {
        return result;
    }
    
    print("[MEM] Total usable memory: ");
    print_dec(memory_info.usable_memory_kb);
    print(" KB\n");
    
    memory_info.detected = true;
    
    return MEMORY_OK;
}

static void print_basic_memory(uint32 lower_mem, uint32 upper_mem) {
    print("[MEM] Lower memory: ");
    print_dec(lower_mem);
    print(" KB, Upper memory: ");
    print_dec(upper_mem);
    print(" KB\n");
}

static memory_result_t unmap_identity_range(page_directory_t* dir, uint32 start, uint32 end) {
    if (!dir) {
        dir = vmm_get_current_page_directory();
        if (!dir) {
            return MEMORY_ERROR_NOT_INITIALIZED;
        }
    }

    start = memory_align_down(start, MEMORY_PAGE_SIZE);
    end = memory_align_up(end, MEMORY_PAGE_SIZE);

    for (uint32 addr = start; addr < end; addr += MEMORY_PAGE_SIZE) {
        memory_result_t result = vmm_unmap_page(dir, addr);
        if (result != MEMORY_OK && result != MEMORY_ERROR_NOT_MAPPED) {
            return result;
        }
    }

    return MEMORY_OK;
}

static void reset_region_info(void) {
    memory_info.region_count = 0;
    memory_info.usable_memory_kb = 0;
}

static void add_memory_region(uint64 base, uint64 length, uint32 type) {
    if (memory_info.region_count >= 32 || length == 0) {
        return;
    }
    
    memory_region_t* region = &memory_info.regions[memory_info.region_count];
    region->base_address = base;
    region->length = length;
    region->type = type;
    
    print("[MEM] Region ");
    print_dec(memory_info.region_count);
    print(": 0x");
    print_hex((uint32)base);
    print(" - 0x");
    print_hex((uint32)(base + length));
    print(" (");
    print_dec((uint32)(length / 1024));
    print(" KB) Type: ");
    print_dec(type);
    
    if (type == 1) {
        print(" [Available]");
        memory_info.usable_memory_kb += (uint32)(length / 1024);
    } else {
        print(" [Reserved]");
    }
    print("\n");
    
    memory_info.region_count++;
}

static memory_result_t parse_multiboot1_info(multiboot_info_t* mbi) {
    if (!mbi) {
        return MEMORY_ERROR_NULL_PTR;
    }
    
    if (!(mbi->flags & MULTIBOOT_FLAG_MEM)) {
        print("[MEM] No memory info in multiboot1 structure\n");
        return MEMORY_ERROR_NOT_INITIALIZED;
    }
    
    uint32 lower_mem = mbi->mem_lower;
    uint32 upper_mem = mbi->mem_upper;
    
    print_basic_memory(lower_mem, upper_mem);
    memory_info.total_memory_kb = lower_mem + upper_mem;
    
    reset_region_info();
    
    if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
        print("[MEM] Parsing detailed memory map...\n");
        
        multiboot_mmap_entry_t* mmap = (multiboot_mmap_entry_t*)mbi->mmap_addr;
        uint32 mmap_end = mbi->mmap_addr + mbi->mmap_length;
        
        while ((uint32)mmap < mmap_end && memory_info.region_count < 32) {
            uint64 base = ((uint64)mmap->addr_high << 32) | mmap->addr_low;
            uint64 length = ((uint64)mmap->len_high << 32) | mmap->len_low;
            add_memory_region(base, length, mmap->type);
            mmap = (multiboot_mmap_entry_t*)((uint32)mmap + mmap->size + sizeof(uint32));
        }
    } else {
        print("[MEM] Creating basic memory map...\n");
        add_memory_region(0x00000000, ((uint64)lower_mem) * 1024, 1);
        add_memory_region(0x00100000, ((uint64)upper_mem) * 1024, 1);
    }
    
    return MEMORY_OK;
}

static memory_result_t parse_multiboot2_info(uint32 info_addr) {
    if (info_addr == 0) {
        return MEMORY_ERROR_NULL_PTR;
    }
    
    uint8* info = (uint8*)info_addr;
    uint32 total_size = *(uint32*)info;
    uint8* tag_ptr = info + 8; // Skip total_size and reserved
    
    uint32 lower_mem = 0;
    uint32 upper_mem = 0;
    bool have_basic = false;
    bool have_mmap = false;
    
    reset_region_info();
    
    while ((uint32)(tag_ptr - info) < total_size) {
        multiboot2_tag_t* tag = (multiboot2_tag_t*)tag_ptr;
        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }
        
        switch (tag->type) {
            case MULTIBOOT2_TAG_BASIC_MEMINFO: {
                multiboot2_tag_basic_mem_t* mem_tag = (multiboot2_tag_basic_mem_t*)tag;
                lower_mem = mem_tag->mem_lower;
                upper_mem = mem_tag->mem_upper;
                have_basic = true;
            } break;
            
            case MULTIBOOT2_TAG_MMAP: {
                multiboot2_tag_mmap_t* mmap_tag = (multiboot2_tag_mmap_t*)tag;
                print("[MEM] Parsing Multiboot2 memory map...\n");
                have_mmap = true;
                
                uint8* entry_ptr = (uint8*)mmap_tag + sizeof(multiboot2_tag_mmap_t);
                uint8* entry_end = (uint8*)mmap_tag + mmap_tag->tag.size;
                
                while (entry_ptr < entry_end && memory_info.region_count < 32) {
                    multiboot2_mmap_entry_t* entry = (multiboot2_mmap_entry_t*)entry_ptr;
                    add_memory_region(entry->base_addr, entry->length, entry->type);
                    entry_ptr += mmap_tag->entry_size;
                }
            } break;
            
            default:
                break;
        }
        
        uint32 advance = (tag->size + 7) & ~7;
        tag_ptr += advance;
    }
    
    if (have_basic) {
        print_basic_memory(lower_mem, upper_mem);
        memory_info.total_memory_kb = lower_mem + upper_mem;
    }
    
    if (!have_mmap) {
        if (!have_basic) {
            print("[MEM] Multiboot2 info lacks both meminfo and mmap tags\n");
            return MEMORY_ERROR_INVALID_ADDR;
        }
        print("[MEM] Creating basic memory map from Multiboot2 info...\n");
        reset_region_info();
        add_memory_region(0x00000000, ((uint64)lower_mem) * 1024, 1);
        add_memory_region(0x00100000, ((uint64)upper_mem) * 1024, 1);
    } else if (!have_basic) {
        uint64 total = 0;
        for (uint32 i = 0; i < memory_info.region_count; i++) {
            total += memory_info.regions[i].length;
        }
        memory_info.total_memory_kb = (uint32)(total / 1024);
    }
    
    return MEMORY_OK;
}

// =============================================================================
// MEMORY SUBSYSTEM INITIALIZATION
// =============================================================================

extern char kernel_end;

// =============================================================================
// PAGE FAULT HANDLER WRAPPER
// =============================================================================

// Wrapper function to bridge interrupt handler interface to page fault handler
static void memory_page_fault_wrapper(struct interrupt_frame* frame, uint32 error_code) {
    // Get the fault address from CR2 register
    uint32 fault_addr;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(fault_addr));
    
    // Call the actual page fault handler
    page_fault_handler(fault_addr, error_code);
}

memory_result_t memory_init(uint32 multiboot_magic, uint32 multiboot_info) {
    print("\n=== FOREST OS MEMORY MANAGER v2.0 ===\n");
    print("[MEM] Initializing memory subsystem...\n");
    
    // Step 1: Detect available memory
    memory_result_t result = memory_detect_grub(multiboot_magic, multiboot_info);
    if (result != MEMORY_OK) {
        print("[MEM] Memory detection failed: ");
        print(memory_result_to_string(result));
        print("\n");
        memory_panic_stage("memory_detect_grub", result);
        return result;
    }
    
    // Step 2: Initialize Physical Memory Manager
    print("[MEM] Initializing Physical Memory Manager...\n");
    result = pmm_init(memory_info.regions, memory_info.region_count);
    if (result != MEMORY_OK) {
        print("[MEM] PMM initialization failed: ");
        print(memory_result_to_string(result));
        print("\n");
        memory_panic_stage("pmm_init", result);
        return result;
    }
    
    // Step 3: Initialize Virtual Memory Manager
    print("[MEM] Initializing Virtual Memory Manager...\n");
    result = vmm_init();
    if (result != MEMORY_OK) {
        print("[MEM] VMM initialization failed: ");
        print(memory_result_to_string(result));
        print("\n");
        memory_panic_stage("vmm_init", result);
        return result;
    }
    
    // Step 4: Identity map essential memory regions before enabling paging
    print("[MEM] Setting up identity mapping...\n");
    page_directory_t* kernel_dir = vmm_get_current_page_directory();
    
    const uint32 initial_heap_size = 1024 * 1024; // 1MB heap bootstrap
    uint32 kernel_buffer_end = (uint32)&kernel_end + 0x100000; // keep extra space for modules
    uint32 heap_region_end = MEMORY_KERNEL_HEAP_START + initial_heap_size;
    uint32 identity_end = kernel_buffer_end > heap_region_end ?
                          kernel_buffer_end : heap_region_end;
    uint32 kernel_map_end = memory_align_up(identity_end, MEMORY_PAGE_SIZE);
    result = vmm_identity_map_range(kernel_dir, 0x00000000, kernel_map_end,
                                   PAGE_PRESENT | PAGE_WRITABLE);
    if (result != MEMORY_OK) {
        print("[MEM] Failed to identity map kernel area\n");
        memory_panic_stage("vmm_identity_map_range (kernel)", result);
        return result;
    }
    
    // Identity map VGA text buffer
    result = vmm_identity_map_range(kernel_dir, 0x000B8000, 0x000B9000,
                                   PAGE_PRESENT | PAGE_WRITABLE);
    if (result != MEMORY_OK) {
        print("[MEM] Failed to map VGA buffer\n");
        memory_panic_stage("vmm_identity_map_range (VGA buffer)", result);
        return result;
    }
    
    // Step 5: Enable paging
    print("[MEM] Enabling paging...\n");
    vmm_enable_paging();
    print("[MEM] Paging enabled successfully\n");
    
    // Step 5.5: Register page fault handler now that paging is enabled
    print("[MEM] Registering page fault handler...\n");
    interrupt_set_handler(EXCEPTION_PAGE_FAULT, memory_page_fault_wrapper);
    print("[MEM] Page fault handler registered\n");
    
    // Step 6: Drop the identity map for the heap range so heap_init can map
    // fresh physical frames without running into duplicate mappings.
    result = unmap_identity_range(kernel_dir,
                                  MEMORY_KERNEL_HEAP_START,
                                  MEMORY_KERNEL_HEAP_START + initial_heap_size);
    if (result != MEMORY_OK) {
        print("[MEM] Failed to unmap temporary heap mapping\n");
        memory_panic_stage("unmap_identity_range (heap)", result);
        return result;
    }

    // Step 7: Initialize kernel heap
    print("[MEM] Initializing kernel heap...\n");
    result = heap_init(MEMORY_KERNEL_HEAP_START, initial_heap_size);
    if (result != MEMORY_OK) {
        print("[MEM] Heap initialization failed: ");
        print(memory_result_to_string(result));
        print("\n");
        memory_panic_stage("heap_init", result);
        return result;
    }
    
    print("[MEM] Memory subsystem initialization complete!\n");
    print("=== MEMORY MANAGER READY ===\n\n");
    
    return MEMORY_OK;
}

// =============================================================================
// INFORMATION FUNCTIONS
// =============================================================================

memory_region_t* memory_get_regions(uint32* count) {
    if (count) {
        *count = memory_info.detected ? memory_info.region_count : 0;
    }
    return memory_info.detected ? memory_info.regions : NULL;
}

uint32 memory_get_usable_kb(void) {
    return memory_info.detected ? memory_info.usable_memory_kb : 0;
}

memory_stats_t memory_get_stats(void) {
    memory_stats_t stats = {0};
    
    if (memory_info.detected) {
        stats.total_memory_kb = memory_info.total_memory_kb;
        stats.usable_memory_kb = memory_info.usable_memory_kb;
        
        // Get PMM stats
        extern uint32 pmm_get_total_frames(void);
        extern uint32 pmm_get_free_frames(void);
        
        stats.total_frames = pmm_get_total_frames();
        stats.free_frames = pmm_get_free_frames();
        stats.used_frames = stats.total_frames - stats.free_frames;
        
        // Get heap stats
        uint32 heap_total, heap_used, heap_free;
        heap_get_stats(&heap_total, &heap_used, &heap_free);
        
        stats.heap_size_kb = heap_total / 1024;
        stats.heap_used_kb = heap_used / 1024;
        stats.heap_free_kb = heap_free / 1024;
        
        // Calculate kernel memory usage (rough estimate)
        uint32 kernel_size = MEMORY_KERNEL_HEAP_START - MEMORY_KERNEL_START;
        stats.kernel_frames = (kernel_size / MEMORY_PAGE_SIZE) + (heap_used / MEMORY_PAGE_SIZE);
    }
    
    return stats;
}

void memory_dump_info(void) {
    memory_stats_t stats = memory_get_stats();
    
    print("\n=== MEMORY SYSTEM STATUS ===\n");
    
    print("Total Memory: ");
    print_dec(stats.total_memory_kb);
    print(" KB\n");
    
    print("Usable Memory: ");
    print_dec(stats.usable_memory_kb);
    print(" KB\n");
    
    print("Page Frames: ");
    print_dec(stats.used_frames);
    print(" used, ");
    print_dec(stats.free_frames);
    print(" free (");
    print_dec(stats.total_frames);
    print(" total)\n");
    
    print("Kernel Heap: ");
    print_dec(stats.heap_used_kb);
    print(" KB used, ");
    print_dec(stats.heap_free_kb);
    print(" KB free (");
    print_dec(stats.heap_size_kb);
    print(" KB total)\n");
    
    print("===========================\n\n");
}
