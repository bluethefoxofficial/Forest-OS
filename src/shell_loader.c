#include "include/shell_loader.h"
#include "include/elf.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/panic.h"
#include "include/vfs.h"
#include "include/memory.h"
#include "include/task.h"
#include "include/debuglog.h"

#define USER_STACK_PAGES 4
#define MAX_ELF_SIZE (1024 * 1024)
#define MAX_BSS_SIZE (4 * 1024 * 1024)

static uint32 g_last_shell_pid = 0;
static task_t* g_last_shell_task = NULL;

uint32 shell_get_last_pid(void) {
    return g_last_shell_pid;
}

task_t* shell_get_last_task(void) {
    return g_last_shell_task;
}

bool shell_launch_embedded(void) {
    const uint8 *elf_data = NULL;
    uint32 elf_size = 0;

    g_last_shell_pid = 0;
    g_last_shell_task = NULL;

    debuglog(DEBUG_INFO, "[SHELL] Loading shell from /bin/invalidshellname.elf\n");
    print("[SHELL] Loading shell from /bin/invalidshellname.elf\n");



    if (!vfs_read_file("bin/invalidshellname.elf", &elf_data, &elf_size)) {
        debuglog(DEBUG_ERROR, "[SHELL] ERROR: Shell ELF not found (/bin/invalidshellname.elf)\n");
        print_colored("[SHELL] ERROR: Shell ELF not found (/bin/invalidshellname.elf)\n", 0x0C, 0x00);
        return false;
    }

    debuglog(DEBUG_INFO, "[SHELL] Read shell ELF: data=%p, size=%u\n", (void*)elf_data, elf_size);

    if (!elf_data || elf_size == 0 || elf_size > MAX_ELF_SIZE) {
        debuglog(DEBUG_ERROR, "[SHELL] ERROR: Invalid shell ELF size (data=%p, size=%u)\n", (void*)elf_data, elf_size);
        print_colored("[SHELL] ERROR: Invalid shell ELF size\n", 0x0C, 0x00);
        return false;
    }

    if (!elf_is_valid(elf_data, elf_size)) {
        debuglog(DEBUG_ERROR, "[SHELL] ERROR: Shell ELF header invalid\n");
        print_colored("[SHELL] ERROR: Shell ELF header invalid\n", 0x0C, 0x00);
        return false;
    }

    debuglog(DEBUG_INFO, "[SHELL] ELF validated, creating shell task through scheduler\n");
    print("[SHELL] Creating shell task through scheduler\n");

    // Create the shell task using the task system
    task_t* shell_task = task_create_elf(elf_data, elf_size, "shell");
    if (!shell_task) {
        debuglog(DEBUG_ERROR, "[SHELL] ERROR: Failed to create shell task\n");
        print_colored("[SHELL] ERROR: Failed to create shell task\n", 0x0C, 0x00);
        return false;
    }

    debuglog(DEBUG_INFO, "[SHELL] Shell task created successfully with ID: %u\n", shell_task->id);
    print("[SHELL] Shell task created successfully with ID: ");
    print(int_to_string(shell_task->id));
    print(". Scheduler will handle execution.\n");

    g_last_shell_pid = shell_task->id;
    g_last_shell_task = shell_task;
    return true;
}
