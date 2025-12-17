#ifndef VFS_H
#define VFS_H

#include "types.h"

// Minimal VFS facade backed by the initrd tar.
bool vfs_init(void);

// Read-only lookup; returns true and fills data/size if found.
bool vfs_read_file(const char* path, const uint8** data, uint32* size);

#endif
