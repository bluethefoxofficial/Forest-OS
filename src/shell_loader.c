#include "include/shell_loader.h"
#include "include/elf.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/panic.h"
#include "include/vfs.h"
#include "include/memory.h"

#define USER_STACK_PAGES 4
#define MAX_ELF_SIZE (1024 * 1024)
#define MAX_BSS_SIZE (4 * 1024 * 1024)

bool shell_launch_embedded(void) {
    elf_load_info_t info = {0};
    const uint8 *elf_data = NULL;
    uint32 elf_size = 0;

    if (!vfs_read_file("/bin/shell.elf", &elf_data, &elf_size)) {
        PANIC("Shell ELF not found (/bin/shell.elf)");
        return false;
    }

    if (!elf_data || elf_size == 0 || elf_size > MAX_ELF_SIZE) {
        PANIC("Invalid shell ELF size");
        return false;
    }

    if (!elf_is_valid(elf_data, elf_size)) {
        PANIC("Shell ELF header invalid");
        return false;
    }

    int status = elf_load_executable(elf_data, elf_size, &info);
    if (status != 0 || !info.valid || info.entry_point == 0) {
        PANIC("Shell ELF failed to load");
        return false;
    }

    if (info.bss_size > MAX_BSS_SIZE) {
        PANIC("Shell ELF BSS too large");
        return false;
    }

    /* Map user stack */
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        uint32 phys = pmm_alloc_frame();
        if (!phys) {
            PANIC("Failed to allocate user stack");
            return false;
        }

        vmm_map_page(
            (page_directory_t *)info.page_directory,
                     USER_STACK_TOP - (i + 1) * MEMORY_PAGE_SIZE,
                     phys,
                     PAGE_USER | PAGE_WRITABLE
        );
    }

    print("[KERNEL] Shell ELF loaded. Entering user mode...\n");

    vmm_switch_page_directory((page_directory_t*)info.page_directory);

    uint32 user_stack = USER_STACK_TOP;

    /* Enter ring 3 properly */
    __asm__ __volatile__ (
        "cli\n"
        "mov $0x23, %%ax\n"     /* user data segment */
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        "pushl $0x23\n"         /* SS */
        "pushl %0\n"            /* ESP */
        "pushfl\n"
        "pop %%eax\n"
        "or $0x200, %%eax\n"    /* IF */
        "push %%eax\n"
        "pushl $0x1B\n"         /* CS */
        "pushl %1\n"            /* EIP */
        "iret\n"
        :
        : "r"(user_stack), "r"(info.entry_point)
        : "eax"
    );

    PANIC("Shell returned unexpectedly");
    return false;
}
