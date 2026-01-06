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
    if (!new_dir)
        return -4;

    // Ensure the ELF source buffer is visible in the new page directory while
    // we copy it. The initrd is identity-mapped in the kernel, but the fresh
    // directory may not have that range mapped yet.
    uint32 src_start = align_down((uint32)elf_data, MEMORY_PAGE_SIZE);
    uint32 src_end   = align_up((uint32)elf_data + elf_size, MEMORY_PAGE_SIZE);
    vmm_identity_map_range(new_dir, src_start, src_end, PAGE_PRESENT | PAGE_WRITABLE);

    page_directory_t* prev_dir = vmm_get_current_page_directory();
    vmm_switch_page_directory(new_dir);

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

        const uint8* file_src = elf_data + segment->p_offset;
        uint8* dest = (uint8*)segment->p_vaddr;
        memory_copy((char*)file_src, (char*)dest, (int)segment->p_filesz);

        if (segment->p_memsz > segment->p_filesz) {
            uint32 bss_start = segment->p_vaddr + segment->p_filesz;
            uint32 bss_end = segment->p_vaddr + segment->p_memsz;
            zero_bss_region(bss_start, bss_end);

            total_bss += (segment->p_memsz - segment->p_filesz);
            if (total_bss > ELF_MAX_BSS_SIZE) {
                debuglog(DEBUG_ERROR, "[ELF] BSS size too large (total=%u, max=%u)\n",
                         total_bss, ELF_MAX_BSS_SIZE);
                goto fail;
            }

            if (bss_start < bss_min) {
                bss_min = bss_start;
            }
            if (bss_end > bss_max) {
                bss_max = bss_end;
            }
        }

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

    vmm_switch_page_directory(prev_dir);

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

    return 0;

    fail:
    vmm_switch_page_directory(prev_dir);
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
