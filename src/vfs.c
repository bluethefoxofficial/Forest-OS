#include "include/vfs.h"
#include "include/ramdisk.h"
#include "include/panic.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/string.h"

bool vfs_init(void) {
    // Currently the initrd is our root filesystem.
    if (ramdisk_file_count() == 0) {
        kernel_panic("VFS: no files in initrd");
        return false;
    }

    print("[VFS] Mounted initrd as root (");
    print(int_to_string(ramdisk_file_count()));
    print(" entries)\n");
    return true;
}

bool vfs_read_file(const char* path, const uint8** data, uint32* size) {
    if (!path) {
        return false;
    }

    // Allow both "bin/file" and "/bin/file".
    if (*path == '/') {
        path++;
    }

    const ramdisk_file_t* f = ramdisk_find(path);
    if (!f || f->is_dir) {
        // Debug: List all files when failing to find shell.elf
        if (path && strstr(path, "shell.elf")) {
            print("[VFS DEBUG] Failed to find: ");
            print(path);
            print("\n[VFS DEBUG] Available files:\n");
            uint32 count = ramdisk_file_count();
            for (uint32 i = 0; i < count; i++) {
                const ramdisk_file_t* file = ramdisk_get(i);
                if (file && file->name) {
                    print("  ");
                    print(file->name);
                    if (file->is_dir) {
                        print(" [DIR]");
                    }
                    print("\n");
                }
            }
        }
        return false;
    }

    if (data) {
        *data = f->data;
    }
    if (size) {
        *size = f->size;
    }
    return true;
}
