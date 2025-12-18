#include "include/shell_loader.h"
#include "include/elf.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/panic.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/task.h"

#define USER_STACK_PAGES 4
#define MAX_ELF_SIZE (1024 * 1024)
#define MAX_BSS_SIZE (4 * 1024 * 1024)

bool shell_launch_embedded(void) {
    const uint8 *elf_data = NULL;
    uint32 elf_size = 0;

    print("[SHELL] Loading shell from /bin/shell.elf\n");

    if (!vfs_read_file("/bin/shell.elf", &elf_data, &elf_size)) {
        print_colored("[SHELL] ERROR: Shell ELF not found (/bin/shell.elf)\n", 0x0C, 0x00);
        return false;
    }

    if (!elf_data || elf_size == 0 || elf_size > MAX_ELF_SIZE) {
        print_colored("[SHELL] ERROR: Invalid shell ELF size\n", 0x0C, 0x00);
        return false;
    }

    if (!elf_is_valid(elf_data, elf_size)) {
        print_colored("[SHELL] ERROR: Shell ELF header invalid\n", 0x0C, 0x00);
        return false;
    }

    print("[SHELL] Creating shell task through scheduler\n");

    // Create the shell task using the task system
    task_t* shell_task = task_create_elf(elf_data, elf_size, "shell");
    if (!shell_task) {
        print_colored("[SHELL] ERROR: Failed to create shell task\n", 0x0C, 0x00);
        return false;
    }

    print("[SHELL] Shell task created successfully with ID: ");
    print(int_to_string(shell_task->id));
    print(". Scheduler will handle execution.\n");

    return true;
}
