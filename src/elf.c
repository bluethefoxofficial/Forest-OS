#include "include/elf.h"
#include "include/system.h"
#include "include/util.h"
#include "include/screen.h"
#include "include/memory.h"
#include "include/panic.h"
#include "include/debuglog.h"

// Explicit forward declaration to help compiler resolve implicit declaration
extern page_directory_t* vmm_get_current_page_directory(void);

#define ELF_MAX_IMAGE_SIZE   (16 * 1024 * 1024)
#define ELF_MAX_BSS_SIZE     (4 * 1024 * 1024)

static inline uint32 align_down(uint32 value, uint32 align) {
    return value & ~(align - 1);
}

static inline uint32 align_up(uint32 value, uint32 align) {
    return (value + align - 1) & ~(align - 1);
}

static bool header_has_valid_magic(const elf32_ehdr_t* header) {
    return header->e_ident[EI_MAG0] == ELF_MAGIC_0 &&
           header->e_ident[EI_MAG1] == ELF_MAGIC_1 &&
           header->e_ident[EI_MAG2] == ELF_MAGIC_2 &&
           header->e_ident[EI_MAG3] == ELF_MAGIC_3;
}

static bool elf_range_in_bounds(size_t offset, size_t length, size_t total_size) {
    if (offset > total_size) {
        return false;
    }
    return length <= (total_size - offset);
}

static inline bool phdr_in_bounds(const elf32_phdr_t* ph, size_t elf_size) {
    return elf_range_in_bounds(ph->p_offset, ph->p_filesz, elf_size);
}

static uint32 phdr_page_flags(const elf32_phdr_t* ph) {
    uint32 flags = PAGE_PRESENT | PAGE_USER;
    if (ph->p_flags & PF_W) {
        flags |= PAGE_WRITABLE;
    }
    return flags;
}

int elf_validate_header(const elf32_ehdr_t* header) {
    if (!header) {
        return -1;
    }

    if (!header_has_valid_magic(header)) {
        return -2;
    }

    if (header->e_ident[EI_CLASS] != ELF_CLASS_32) {
        return -3;
    }

    if (header->e_ident[EI_DATA] != ELF_DATA_2LSB) {
        return -4;
    }

    if (header->e_type != ELF_TYPE_EXEC) {
        return -5;
    }

    if (header->e_machine != ELF_MACHINE_386) {
        return -6;
    }

    if (header->e_version != ELF_VERSION_CURRENT) {
        return -7;
    }

    if (header->e_ehsize != sizeof(elf32_ehdr_t)) {
        return -8;
    }

    if (header->e_phentsize != sizeof(elf32_phdr_t)) {
        return -9;
    }

    if (header->e_phnum == 0) {
        return -10;
    }

    return 0;
}

bool elf_is_valid(const uint8* elf_data, size_t size) {
    if (!elf_data || size < sizeof(elf32_ehdr_t)) {
        return false;
    }

    return elf_validate_header((const elf32_ehdr_t*)elf_data) == 0;
}

static bool map_segment_pages(page_directory_t* dir, uint32 start, uint32 end, uint32 flags) {
    debuglog(DEBUG_INFO, "[ELF] map_segment_pages: start=0x%x end=0x%x flags=0x%x, free_frames=%u\n", 
             start, end, flags, pmm_get_free_frames());
    for (uint32 va = start; va < end; va += MEMORY_PAGE_SIZE) {
        uint32 frame = pmm_alloc_frame();
        if (!frame) {
            debuglog(DEBUG_ERROR, "[ELF] pmm_alloc_frame failed while mapping segment page 0x%x-0x%x\n", start, end);
            return false;
        }

        memory_result_t map_res = vmm_map_page(dir, va, frame, flags);
        if (map_res != MEMORY_OK && map_res != MEMORY_ERROR_ALREADY_MAPPED) {
            debuglog(DEBUG_ERROR, "[ELF] vmm_map_page failed (res=%d) va=0x%x frame=0x%x flags=0x%x\n",
                     map_res, va, frame, flags);
            return false;
        }
    }

    return true;
}

static void record_segment_info(const elf32_phdr_t* phdr, elf_load_info_t* info) {
    if ((phdr->p_flags & PF_X) && info->text_size == 0) {
        info->text_start = phdr->p_vaddr;
        info->text_size = phdr->p_memsz;
    }

    if ((phdr->p_flags & PF_W) && info->data_size == 0 && !(phdr->p_flags & PF_X)) {
        info->data_start = phdr->p_vaddr;
        info->data_size = phdr->p_memsz;
    }
}

static void zero_bss_region(uint32 start, uint32 end) {
    if (end <= start) {
        return;
    }

    memory_set((uint8*)start, 0, end - start);
}

// Temporarily map a user page in the current directory for access
// Returns the temporary virtual address to use, or 0 on failure
static uint32 temp_map_for_copy(uint32 user_va, uint32 flags) {
    // Calculate the physical frame for this user VA
    // We need to look it up in the new_dir, but we don't have access to it here
    // Instead, we'll just identity-map the physical frame allocated for the user page
    // For now, allocate a new frame and map it temporarily
    uint32 frame = pmm_alloc_frame();
    if (!frame) {
        return 0;
    }
    
    // Use a fixed temporary mapping area just above kernel space
    // 0xFF000000 is the last 16MB, use that for temp mappings
    static uint32 next_temp_va = 0xFF000000;
    uint32 temp_va = next_temp_va;
    next_temp_va += MEMORY_PAGE_SIZE;
    
    page_directory_t* cur_dir = vmm_get_current_page_directory();
    memory_result_t res = vmm_map_page(cur_dir, temp_va, frame, PAGE_PRESENT | PAGE_WRITABLE);
    if (res != MEMORY_OK) {
        pmm_free_frame(frame);
        return 0;
    }
    
    return temp_va;
}

// Copy data to user space in new_dir by temporarily mapping in current_dir
static bool copy_to_user_space(page_directory_t* user_dir, uint32 user_va, 
                                const uint8* src, uint32 size, uint32 flags) {
    uint32 offset = user_va & (MEMORY_PAGE_SIZE - 1);
    uint32 remaining = size;
    const uint8* src_ptr = src;
    page_directory_t* cur_dir = vmm_get_current_page_directory();
    uint32 temp_va = 0xFFC00000; // Fixed temporary mapping address
    
    while (remaining > 0) {
        uint32 page_va = align_down(user_va, MEMORY_PAGE_SIZE);
        
        // Check if page is already mapped in user_dir
        uint32 frame = vmm_get_physical_addr(user_dir, page_va);
        if (!frame) {
            frame = pmm_alloc_frame();
            if (!frame) {
                debuglog(DEBUG_ERROR, "[ELF] copy_to_user_space: out of memory\n");
                return false;
            }
            
            memory_result_t res = vmm_map_page(user_dir, page_va, frame, flags);
            if (res != MEMORY_OK && res != MEMORY_ERROR_ALREADY_MAPPED) {
                pmm_free_frame(frame);
                debuglog(DEBUG_ERROR, "[ELF] copy_to_user_space: vmm_map_page failed\n");
                return false;
            }
        }
        
        // Temporarily map in current directory for copying
        vmm_unmap_page(cur_dir, temp_va);
        memory_result_t res = vmm_map_page(cur_dir, temp_va, frame, PAGE_PRESENT | PAGE_WRITABLE);
        if (res != MEMORY_OK) {
            debuglog(DEBUG_ERROR, "[ELF] copy_to_user_space: temp map failed\n");
            return false;
        }
        
        // Copy data to the temp mapping
        uint32 copy_offset = user_va - page_va;
        uint32 copy_size = MEMORY_PAGE_SIZE - copy_offset;
        if (copy_size > remaining) {
            copy_size = remaining;
        }
        
        memory_copy((char*)(temp_va + copy_offset), (char*)src_ptr, (int)copy_size);
        
        // Unmap temp mapping
        vmm_unmap_page(cur_dir, temp_va);
        
        // Advance
        src_ptr += copy_size;
        remaining -= copy_size;
        user_va += copy_size;
    }
    
    return true;
}

//This is busting my balls.
//TODO: fix sloppy code later
int elf_load_executable(const uint8 *elf_data, size_t elf_size,
                        elf_load_info_t *info)
{
    if (!elf_data || !info)
        return -1;

    if (elf_size == 0 || elf_size > ELF_MAX_IMAGE_SIZE) {
        debuglog(DEBUG_ERROR, "[ELF] ELF size invalid: %u (max %u)\n", (uint32)elf_size, ELF_MAX_IMAGE_SIZE);
        return -6;
    }

    memory_set((uint8*)info, 0, sizeof(*info));

    if (!elf_is_valid(elf_data, elf_size))
        return -2;

    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)elf_data;

    size_t ph_table_size = (size_t)eh->e_phnum * sizeof(elf32_phdr_t);
    if (!elf_range_in_bounds(eh->e_phoff, ph_table_size, elf_size))
        return -3;

    page_directory_t* new_dir = vmm_create_page_directory();
    if (!new_dir) {
        debuglog(DEBUG_ERROR, "[ELF] vmm_create_page_directory failed! free_frames=%u\n", pmm_get_free_frames());
        return -4;
    }
    debuglog(DEBUG_INFO, "[ELF] Created new page directory at %p\n", (void*)new_dir);

    // Ensure the ELF source buffer is visible in the new page directory.
    // The initrd is identity-mapped in the kernel, so we just copy those mappings.
    uint32 src_start = align_down((uint32)elf_data, MEMORY_PAGE_SIZE);
    uint32 src_end   = align_up((uint32)elf_data + elf_size, MEMORY_PAGE_SIZE);
    debuglog(DEBUG_INFO, "[ELF] Mapping ELF source: 0x%x-0x%x in new_dir\n", src_start, src_end);
    vmm_identity_map_range(new_dir, src_start, src_end, PAGE_PRESENT | PAGE_WRITABLE);

    page_directory_t* prev_dir = vmm_get_current_page_directory();
    debuglog(DEBUG_INFO, "[ELF] Loading segments WITHOUT switching page directory\n");
    debuglog(DEBUG_INFO, "[ELF] About to iterate over segments\n");

    uint32 base = 0xFFFFFFFF;
    uint32 end  = 0;
    bool any_segment_loaded = false;
    uint32 bss_min = 0xFFFFFFFF;
    uint32 bss_max = 0;
    uint32 total_bss = 0;
    bool entrypoint_in_segment = false;
    bool entrypoint_executable = false;

    const elf32_phdr_t *ph =
    (const elf32_phdr_t *)(elf_data + eh->e_phoff);

    for (uint32 i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t* segment = &ph[i];

        if (segment->p_type != PT_LOAD)
            continue;

        if (segment->p_memsz == 0)
            continue;

        if (!phdr_in_bounds(segment, elf_size) || segment->p_filesz > segment->p_memsz) {
            debuglog(DEBUG_ERROR, "[ELF] Invalid program header: type=%u off=0x%x filesz=0x%x memsz=0x%x elf_size=0x%x\n",
                     segment->p_type, segment->p_offset, segment->p_filesz, segment->p_memsz, (uint32)elf_size);
            goto fail;
        }

        uint64 seg_end_unaligned = (uint64)segment->p_vaddr + (uint64)segment->p_memsz;
        if (seg_end_unaligned > USER_STACK_TOP || segment->p_vaddr < MEMORY_USER_START || seg_end_unaligned < segment->p_vaddr) {
            debuglog(DEBUG_ERROR, "[ELF] Segment %u outside user range: vaddr=0x%x memsz=0x%x\n",
                     i, segment->p_vaddr, segment->p_memsz);
            goto fail;
        }

        uint32 segment_start = align_down(segment->p_vaddr, MEMORY_PAGE_SIZE);
        uint32 segment_end = align_up((uint32)seg_end_unaligned, MEMORY_PAGE_SIZE);

        if (segment_end <= segment_start) {
            debuglog(DEBUG_ERROR, "[ELF] Segment %u computed invalid range: start=0x%x end=0x%x\n",
                     i, segment_start, segment_end);
            goto fail;
        }

        uint32 flags = phdr_page_flags(segment);
        if (!map_segment_pages(new_dir, segment_start, segment_end, flags)) {
            debuglog(DEBUG_ERROR, "[ELF] Failed to map segment %u: vaddr=0x%x-0x%x flags=0x%x\n",
                     i, segment_start, segment_end, flags);
            goto fail;
        }

        // Copy segment data using temporary mapping (without switching directories)
        const uint8* file_src = elf_data + segment->p_offset;
        debuglog(DEBUG_INFO, "[ELF] Copying segment %u: filesz=%u to vaddr=0x%x\n",
                 i, segment->p_filesz, segment->p_vaddr);
        if (!copy_to_user_space(new_dir, segment->p_vaddr, file_src, segment->p_filesz, flags)) {
            debuglog(DEBUG_ERROR, "[ELF] Failed to copy segment %u data\n", i);
            goto fail;
        }

        // Zero BSS if present
        if (segment->p_memsz > segment->p_filesz) {
            uint32 bss_size = segment->p_memsz - segment->p_filesz;
            uint32 bss_va = segment->p_vaddr + segment->p_filesz;
            debuglog(DEBUG_INFO, "[ELF] Zeroing BSS: 0x%x size=%u\n", bss_va, bss_size);
            
            // Zero BSS using temporary mapping
            page_directory_t* cur_dir = vmm_get_current_page_directory();
            uint32 temp_va = 0xFFC00000;
            uint32 remaining_bss = bss_size;
            uint32 bss_offset = 0;
            
            while (remaining_bss > 0) {
                uint32 page_va = align_down(bss_va + bss_offset, MEMORY_PAGE_SIZE);
                uint32 frame = vmm_get_physical_addr(new_dir, page_va);
                if (!frame) {
                    debuglog(DEBUG_ERROR, "[ELF] BSS page not mapped: 0x%x\n", page_va);
                    goto fail;
                }
                
                vmm_unmap_page(cur_dir, temp_va);
                vmm_map_page(cur_dir, temp_va, frame, PAGE_PRESENT | PAGE_WRITABLE);
                
                uint32 copy_offset = (bss_va + bss_offset) - page_va;
                uint32 copy_size = MEMORY_PAGE_SIZE - copy_offset;
                if (copy_size > remaining_bss) {
                    copy_size = remaining_bss;
                }
                
                memory_set((char*)(temp_va + copy_offset), 0, copy_size);
                vmm_unmap_page(cur_dir, temp_va);
                
                remaining_bss -= copy_size;
                bss_offset += copy_size;
            }

            total_bss += bss_size;
            if (total_bss > ELF_MAX_BSS_SIZE) {
                debuglog(DEBUG_ERROR, "[ELF] BSS size too large (total=%u, max=%u)\n",
                         total_bss, ELF_MAX_BSS_SIZE);
                goto fail;
            }

            if (bss_va < bss_min) {
                bss_min = bss_va;
            }
            if (bss_va + bss_size > bss_max) {
                bss_max = bss_va + bss_size;
            }
        }

        debuglog(DEBUG_INFO, "[ELF] Segment %u: vaddr=0x%x, memsz=0x%x, end=0x%x, flags=0x%x\n",
                 i, segment->p_vaddr, segment->p_memsz, segment->p_vaddr + segment->p_memsz, segment->p_flags);
        debuglog(DEBUG_INFO, "[ELF] Entry point: 0x%x\n", eh->e_entry);
        if (eh->e_entry >= segment->p_vaddr && eh->e_entry < segment->p_vaddr + segment->p_memsz) {
            entrypoint_in_segment = true;
            if (segment->p_flags & PF_X) {
                entrypoint_executable = true;
            }
        }

        if (segment->p_vaddr < base)
            base = segment->p_vaddr;

        if (segment->p_vaddr + segment->p_memsz > end)
            end = segment->p_vaddr + segment->p_memsz;

        record_segment_info(segment, info);
        any_segment_loaded = true;
    }

    if (!any_segment_loaded || base == 0xFFFFFFFF)
        goto fail_destroy;

    if (!entrypoint_in_segment || !entrypoint_executable) {
        debuglog(DEBUG_ERROR, "[ELF] Entry point 0x%x not in an executable load segment\n", eh->e_entry);
        goto fail_destroy;
    }

    info->entry_point  = eh->e_entry;
    info->base_address = base;
    info->total_size   = end - base;
    info->page_directory = (uint32)new_dir;
    if (bss_min != 0xFFFFFFFF) {
        info->bss_start = bss_min;
        info->bss_size = bss_max - bss_min;
    }

    info->valid        = true;

    debuglog(DEBUG_INFO, "[ELF] Successfully loaded ELF, entry=0x%x, pd=0x%x\n", 
             info->entry_point, info->page_directory);
    return 0;

    fail:
    fail_destroy:
    vmm_destroy_page_directory(new_dir);
    return -5;
}


int elf_load_from_file(const char* filename, elf_load_info_t* load_info) {
    // No filesystem support yet; leave a clear failure path.
    (void)filename;
    (void)load_info;
    print("[ELF] Loading from files is not supported in this kernel build\n");
    return -1;
}

uint32 elf_get_entry_point(const uint8* elf_data) {
    if (!elf_is_valid(elf_data, sizeof(elf32_ehdr_t))) {
        return 0;
    }

    const elf32_ehdr_t* header = (const elf32_ehdr_t*)elf_data;
    return header->e_entry;
}
