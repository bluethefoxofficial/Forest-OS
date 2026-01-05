#ifndef SHELL_LOADER_H
#define SHELL_LOADER_H

#include "types.h"

// Load and jump to the userspace shell ELF found in the initrd (VFS).
// Returns true if the ELF was found and control was transferred.
bool shell_launch_embedded(void);
uint32 shell_get_last_pid(void);
struct task;
struct task* shell_get_last_task(void);

#endif
