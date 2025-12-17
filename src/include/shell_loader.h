#ifndef SHELL_LOADER_H
#define SHELL_LOADER_H

#include "types.h"

// Load and jump to the userspace shell ELF found in the initrd (VFS).
// Returns true if the ELF was found and control was transferred.
bool shell_launch_embedded(void);

#endif
