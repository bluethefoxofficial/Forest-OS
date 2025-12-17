#ifndef RAMDISK_H
#define RAMDISK_H

#include "types.h"

typedef struct {
    const char* name;
    const uint8* data;
    uint32 size;
    bool is_dir;
} ramdisk_file_t;

// Initialize the ramdisk from the first multiboot module (ustar/tar archive).
// Returns true on success.
bool ramdisk_init(uint32 magic, uint32 mbi_addr);

// Query functions
uint32 ramdisk_file_count(void);
const ramdisk_file_t* ramdisk_get(uint32 index);
const ramdisk_file_t* ramdisk_find(const char* path);

#endif
