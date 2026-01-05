#include "include/vfs.h"
#include "include/ramdisk.h"
#include "include/panic.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/string.h"
#include "include/debuglog.h"

bool vfs_init(void) {
    // Currently the initrd is our root filesystem.
    if (ramdisk_file_count() == 0) {
        kernel_panic("VFS: no files in initrd");
        return false;
    }

    debuglog(DEBUG_INFO, "[VFS] Mounted initrd as root (%u entries)\n", ramdisk_file_count());
    print("[VFS] Mounted initrd as root (");
    print(int_to_string(ramdisk_file_count()));
    print(" entries)\n");

    // Debug: List sample of files to verify parsing including around index 89 where shell.elf should be
    debuglog(DEBUG_INFO, "[VFS] Sample files in initrd:\n");
    uint32 count = ramdisk_file_count();
    for (uint32 i = 0; i < count; i++) {
        // Show first 5, around index 85-95 (where shell.elf should be), and last 3
        if (i < 5 || (i >= 85 && i <= 95) || i >= count - 3) {
            const ramdisk_file_t* file = ramdisk_get(i);
            if (file && file->name) {
                debuglog(DEBUG_INFO, "  [%u] '%s' %s size=%u\n", i, file->name,
                         file->is_dir ? "[DIR]" : "[FILE]", file->size);
            }
        }
    }

    return true;
}

bool vfs_read_file(const char* path, const uint8** data, uint32* size) {
    if (!path) {
        return false;
    }

    // Allow both "bin/file" and "/bin/file".
    const char* lookup_path = path;
    if (*lookup_path == '/') {
        lookup_path++;
    }

    debuglog(DEBUG_INFO, "[VFS] Looking up: '%s' (original: '%s')\n", lookup_path, path);

    const ramdisk_file_t* f = ramdisk_find(lookup_path);
    if (!f || f->is_dir) {
        debuglog(DEBUG_ERROR, "[VFS] File not found: '%s'\n", lookup_path);

        // Debug: List files containing the search term
        if (lookup_path && strstr(lookup_path, "shell")) {
            debuglog(DEBUG_INFO, "[VFS] Searching for files containing 'shell':\n");
            uint32 count = ramdisk_file_count();
            for (uint32 i = 0; i < count; i++) {
                const ramdisk_file_t* file = ramdisk_get(i);
                if (file && file->name && strstr(file->name, "shell")) {
                    debuglog(DEBUG_INFO, "  Found: '%s' size=%u\n", file->name, file->size);
                }
            }
        }
        return false;
    }

    debuglog(DEBUG_INFO, "[VFS] Found: '%s' size=%u\n", f->name, f->size);

    if (data) {
        *data = f->data;
    }
    if (size) {
        *size = f->size;
    }
    return true;
}
