#include "include/vfs.h"
#include "include/ramdisk.h"
#include "include/panic.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/string.h"
#include "include/debuglog.h"
#include "include/enhanced_heap.h"
#include "include/ioctl.h"

// Root filesystem node
static vfs_node_t* vfs_root = NULL;

// Mount table (no block device required)
static vfs_mount_t* vfs_mounts = NULL;

// Filesystem registry
static vfs_filesystem_t* vfs_filesystems = NULL;

// Deferred operation queue
static vfs_deferred_op_entry_t* deferred_ops = NULL;
static bool deferred_ops_enabled = true;

// Forward declarations
static uint32_t initrd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static uint32_t initrd_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
static void initrd_open(vfs_node_t* node, uint32_t flags);
static void initrd_close(vfs_node_t* node);
static bool initrd_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent);
static vfs_node_t* initrd_finddir(vfs_node_t* node, const char* name);

// Device filesystem
extern vfs_node_t* devfs_open(const char* path, uint32_t flags);

// Create a VFS node for initrd file
static vfs_node_t* create_initrd_node(const ramdisk_file_t* file) {
    vfs_node_t* node = (vfs_node_t*)enhanced_heap_alloc(sizeof(vfs_node_t), "vfs_node");
    if (!node) return NULL;
    
    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, file->name, sizeof(node->name) - 1);
    node->flags = file->is_dir ? VFS_DIRECTORY : VFS_FILE;
    node->length = file->size;
    node->internal_data = (void*)file;
    
    // Set up function pointers
    node->read = initrd_read;
    node->write = initrd_write;
    node->open = initrd_open;
    node->close = initrd_close;
    node->readdir = initrd_readdir;
    node->finddir = initrd_finddir;
    
    return node;
}

// Read from initrd file
static uint32_t initrd_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;
    
    const ramdisk_file_t* file = (const ramdisk_file_t*)node->internal_data;
    if (!file || file->is_dir) return 0;
    
    if (offset >= file->size) return 0;
    if (offset + size > file->size) {
        size = file->size - offset;
    }
    
    memcpy(buffer, file->data + offset, size);
    return size;
}

// Write to initrd file (not supported - read-only)
static uint32_t initrd_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0; // Read-only filesystem
}

// Open initrd file
static void initrd_open(vfs_node_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    // Nothing to do for initrd
}

// Close initrd file
static void initrd_close(vfs_node_t* node) {
    (void)node;
    // Nothing to do for initrd
}

// Read directory entry
static bool initrd_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !dirent) return false;

    if (!(node->flags & VFS_DIRECTORY)) {
        return false;
    }

    const char* parent = (node == vfs_root || node->name[0] == '/' || node->name[0] == '\0') ? "" : node->name;
    uint32_t parent_len = strlen(parent);
    uint32_t count = ramdisk_file_count();
    uint32_t match_index = 0;

    for (uint32_t i = 0; i < count; i++) {
        const ramdisk_file_t* file = ramdisk_get(i);
        if (!file || !file->name || !file->name[0]) {
            continue;
        }

        const char* path = file->name;
        if (parent_len > 0) {
            if (strncmp(path, parent, parent_len) != 0 || path[parent_len] != '/') {
                continue;
            }
            path += parent_len + 1;
        }

        if (!path[0]) {
            continue;
        }

        char child[sizeof(dirent->name)];
        uint32_t cpos = 0;
        while (path[cpos] && path[cpos] != '/' && cpos < sizeof(child) - 1) {
            child[cpos] = path[cpos];
            cpos++;
        }
        child[cpos] = '\0';
        if (cpos == 0) {
            continue;
        }

        bool duplicate = false;
        for (uint32_t j = 0; j < i; j++) {
            const ramdisk_file_t* prev = ramdisk_get(j);
            if (!prev || !prev->name) {
                continue;
            }
            const char* prev_path = prev->name;
            if (parent_len > 0) {
                if (strncmp(prev_path, parent, parent_len) != 0 || prev_path[parent_len] != '/') {
                    continue;
                }
                prev_path += parent_len + 1;
            }
            if (!prev_path[0]) {
                continue;
            }
            uint32_t k = 0;
            while (prev_path[k] && prev_path[k] != '/' && k < sizeof(child) - 1) {
                if (prev_path[k] != child[k]) {
                    break;
                }
                k++;
            }
            if (child[k] == '\0' && (prev_path[k] == '\0' || prev_path[k] == '/')) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        if (match_index == index) {
            strncpy(dirent->name, child, sizeof(dirent->name) - 1);
            dirent->name[sizeof(dirent->name) - 1] = '\0';
            dirent->inode = i;
            return true;
        }
        match_index++;
    }

    return false;
}

// Find file in directory
static vfs_node_t* initrd_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name) return NULL;

    const char* parent = (node == vfs_root || node->name[0] == '/' || node->name[0] == '\0') ? "" : node->name;
    char full[256];
    if (parent[0]) {
        string_format(full, sizeof(full), "%s/%s", parent, name);
    } else {
        string_format(full, sizeof(full), "%s", name);
    }

    const ramdisk_file_t* file = ramdisk_find(full);
    if (!file) {
        return NULL;
    }
    return create_initrd_node(file);
}

// Initialize VFS
bool vfs_init(void) {
    // Initialize mount subsystem first
    vfs_mount_init();
    
    // Currently the initrd is our root filesystem.
    if (ramdisk_file_count() == 0) {
        kernel_panic("VFS: no files in initrd");
        return false;
    }

    debuglog(DEBUG_INFO, "[VFS] Mounted initrd as root (%u entries)\n", ramdisk_file_count());
    print("[VFS] Mounted initrd as root (");
    print(int_to_string(ramdisk_file_count()));
    print(" entries)\n");

    // Create root node
    vfs_root = (vfs_node_t*)enhanced_heap_alloc(sizeof(vfs_node_t), "vfs_root");
    if (!vfs_root) {
        kernel_panic("VFS: failed to allocate root node");
        return false;
    }
    
    memset(vfs_root, 0, sizeof(vfs_node_t));
    strcpy(vfs_root->name, "/");
    vfs_root->flags = VFS_DIRECTORY;
    vfs_root->readdir = initrd_readdir;
    vfs_root->finddir = initrd_finddir;

    // Debug: List sample of files
    debuglog(DEBUG_INFO, "[VFS] Sample files in initrd:\n");
    uint32 count = ramdisk_file_count();
    for (uint32 i = 0; i < count; i++) {
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

// Open a file
vfs_node_t* vfs_open(const char* path, uint32_t flags) {
    if (!path) return NULL;

    // Handle root directory
    if (strcmp(path, "/") == 0) {
        return vfs_root;
    }

    // Remove leading slash
    const char* lookup_path = path;
    if (*lookup_path == '/') {
        lookup_path++;
    }

    // Handle /dev paths specially
    if (strncmp(lookup_path, "dev/", 4) == 0) {
        return devfs_open(path, flags);
    }

    // Handle /proc paths specially - commented out due to missing procfs implementation
    /*
    if (strncmp(lookup_path, "proc/", 5) == 0) {
        static vfs_node_t* procfs_root = NULL;
        if (!procfs_root) {
            extern vfs_node_t* procfs_create(void);
            procfs_root = procfs_create();
        }

        // Find the requested file/directory in /proc
        const char* proc_path = lookup_path + 5; // Skip "proc/"
        vfs_node_t* current = procfs_root;

        if (*proc_path == '\0') {
            return procfs_root;
        }

        // Traverse the /proc filesystem
        const char* token = proc_path;
        while (1) {
            const char* next_slash = strchr(token, '/');
            char name[64];
            if (next_slash) {
                strncpy(name, token, next_slash - token);
                name[next_slash - token] = '\0';
            } else {
                strncpy(name, token, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
            }

            vfs_node_t* child = NULL;
            if (current->finddir) {
                child = current->finddir(current, name);
            }

            if (!child) {
                return NULL;
            }

            if (!next_slash) {
                return child; // Found the end of path
            }

            current = child;
            token = next_slash + 1;
        }
    }

    // Handle /sys paths specially - commented out due to missing sysfs implementation
    /*
    if (strncmp(lookup_path, "sys/", 4) == 0) {
        static vfs_node_t* sysfs_root = NULL;
        if (!sysfs_root) {
            extern vfs_node_t* sysfs_create(void);
            sysfs_root = sysfs_create();
        }

        // Find the requested file/directory in /sys
        const char* sys_path = lookup_path + 4; // Skip "sys/"
        vfs_node_t* current = sysfs_root;

        if (*sys_path == '\0') {
            return sysfs_root;
        }

        // Traverse the /sys filesystem
        const char* token = sys_path;
        while (1) {
            const char* next_slash = strchr(token, '/');
            char name[64];
            if (next_slash) {
                strncpy(name, token, next_slash - token);
                name[next_slash - token] = '\0';
            } else {
                strncpy(name, token, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
            }

            vfs_node_t* child = NULL;
            if (current->finddir) {
                child = current->finddir(current, name);
            }

            if (!child) {
                return NULL;
            }

            if (!next_slash) {
                return child; // Found the end of path
            }

            current = child;
            token = next_slash + 1;
        }
    }
    */

    debuglog(DEBUG_INFO, "[VFS] Opening: '%s' (flags=0x%x)\n", lookup_path, flags);

    // Find file in initrd
    const ramdisk_file_t* file = ramdisk_find(lookup_path);
    if (!file) {
        debuglog(DEBUG_ERROR, "[VFS] File not found: '%s'\n", lookup_path);
        return NULL;
    }

    // Create node
    vfs_node_t* node = create_initrd_node(file);
    if (node && node->open) {
        node->open(node, flags);
    }

    return node;
}

// Close a file
void vfs_close(vfs_node_t* node) {
    if (!node) return;
    
    // Don't close root
    if (node == vfs_root) return;
    
    if (node->close) {
        node->close(node);
    }
    
    enhanced_heap_free(node, "vfs_node");
}

// Read from a file
uint32_t vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;
    
    if (node->read) {
        return node->read(node, offset, size, buffer);
    }
    
    return 0;
}

// Write to a file
uint32_t vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;
    
    if (node->write) {
        return node->write(node, offset, size, buffer);
    }
    
    return 0;
}

// Read directory entry
bool vfs_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    if (!node || !dirent) return false;
    
    if (node->readdir) {
        return node->readdir(node, index, dirent);
    }
    
    return false;
}

// Find file in directory
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name) {
    if (!node || !name) return NULL;
    
    if (node->finddir) {
        return node->finddir(node, name);
    }
    
    return NULL;
}

// Legacy function (kept for compatibility)
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

// Execute ioctl on a device node
int vfs_ioctl(vfs_node_t* node, uint32 request, void* arg) {
    if (!node) {
        return IOCTL_ENODEV;
    }

    // Only character and block devices support ioctl
    if ((node->flags & VFS_CHARDEVICE) == 0 &&
        (node->flags & VFS_BLOCKDEVICE) == 0) {
        return IOCTL_ENOTTY;
    }

    // Call the device's ioctl handler if present
    if (node->ioctl) {
        return node->ioctl(node, request, arg);
    }

    return IOCTL_ENOSYS;
}

// Poll a device for events
int vfs_poll(vfs_node_t* node, uint32 events) {
    if (!node) {
        return POLLNVAL;
    }

    // Only character and block devices support poll
    if ((node->flags & VFS_CHARDEVICE) == 0 &&
        (node->flags & VFS_BLOCKDEVICE) == 0) {
        // Regular files are always ready for read/write
        return events & (POLLIN | POLLOUT);
    }

    // Call the device's poll handler if present
    if (node->poll) {
        return node->poll(node, events);
    }

    // Default: assume device is always ready
    return events & (POLLIN | POLLOUT);
}

// Create a device node
vfs_node_t* vfs_create_device_node(const char* name, uint32 flags,
                                    uint16 major, uint16 minor,
                                    void* driver_data) {
    if (!name) return NULL;

    vfs_node_t* node = (vfs_node_t*)enhanced_heap_alloc(sizeof(vfs_node_t), "vfs_device_node");
    if (!node) {
        debuglog(DEBUG_ERROR, "[VFS] Failed to allocate device node for '%s'\n", name);
        return NULL;
    }

    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->flags = flags;
    node->major = major;
    node->minor = minor;
    node->internal_data = driver_data;
    node->open_count = 0;
    node->flags_mode = 0;

    debuglog(DEBUG_INFO, "[VFS] Created device node '%s' (major=%u, minor=%u)\n",
             name, major, minor);

    return node;
}

// Initialize mount subsystem
bool vfs_mount_init(void) {
    vfs_mounts = NULL;
    debuglog(DEBUG_INFO, "[VFS] Mount subsystem initialized (no block device required)\n");
    return true;
}

// Register a filesystem
int vfs_register_filesystem(vfs_filesystem_t* fs) {
    if (!fs || !fs->name) return -1;
    
    fs->next = vfs_filesystems;
    vfs_filesystems = fs;
    
    debuglog(DEBUG_INFO, "[VFS] Registered filesystem: %s\n", fs->name);
    return 0;
}

// Get filesystem by name
vfs_filesystem_t* vfs_get_filesystem(const char* name) {
    if (!name) return NULL;
    
    for (vfs_filesystem_t* fs = vfs_filesystems; fs; fs = fs->next) {
        if (strcmp(fs->name, name) == 0) {
            return fs;
        }
    }
    return NULL;
}

// Get mount table
vfs_mount_t* vfs_get_mounts(void) {
    return vfs_mounts;
}

// Find mount point for a path
static vfs_mount_t* find_mount(const char* path) {
    if (!path) return NULL;
    
    vfs_mount_t* best = NULL;
    int best_len = 0;
    
    for (vfs_mount_t* m = vfs_mounts; m; m = m->next) {
        int len = strlen(m->mountpoint);
        if (len > best_len && strncmp(path, m->mountpoint, len) == 0) {
            best = m;
            best_len = len;
        }
    }
    return best;
}

// Mount a filesystem (no block device required)
int vfs_mount(const char* device, const char* mountpoint, const char* fstype, void* dev_data,
              void* read_sector, void* write_sector, uint64_t sectors) {
    if (!mountpoint || !fstype) return -1;
    
    vfs_filesystem_t* fs = vfs_get_filesystem(fstype);
    if (!fs) {
        debuglog(DEBUG_ERROR, "[VFS] Unknown filesystem: %s\n", fstype);
        return -1;
    }
    
    void* fs_data = NULL;
    if (fs->mount) {
        if (!fs->mount(dev_data, read_sector, write_sector, sectors, &fs_data)) {
            debuglog(DEBUG_ERROR, "[VFS] Failed to mount %s on %s\n", fstype, mountpoint);
            return -1;
        }
    }
    
    vfs_node_t* root = NULL;
    if (fs->get_root) {
        root = fs->get_root(fs_data);
    }
    
    vfs_mount_t* mount = (vfs_mount_t*)enhanced_heap_alloc(sizeof(vfs_mount_t), "vfs_mount");
    if (!mount) {
        if (fs->umount) fs->umount(fs_data);
        return -1;
    }
    
    memset(mount, 0, sizeof(vfs_mount_t));
    strncpy(mount->mountpoint, mountpoint, sizeof(mount->mountpoint) - 1);
    if (device) {
        strncpy(mount->device, device, sizeof(mount->device) - 1);
    }
    mount->fs = fs;
    mount->fs_data = fs_data;
    mount->root = root;
    mount->next = vfs_mounts;
    vfs_mounts = mount;
    
    debuglog(DEBUG_INFO, "[VFS] Mounted %s on %s\n", fstype, mountpoint);
    return 0;
}

// Unmount a filesystem
int vfs_umount(const char* mountpoint) {
    if (!mountpoint) return -1;
    
    vfs_mount_t** prev = &vfs_mounts;
    for (vfs_mount_t* m = vfs_mounts; m; m = m->next) {
        if (strcmp(m->mountpoint, mountpoint) == 0) {
            if (m->fs && m->fs->umount) {
                m->fs->umount(m->fs_data);
            }
            *prev = m->next;
            enhanced_heap_free(m, "vfs_mount");
            debuglog(DEBUG_INFO, "[VFS] Unmounted %s\n", mountpoint);
            return 0;
        }
        prev = &m->next;
    }
    return -1;
}

// Process deferred operations
static int process_deferred_op(vfs_deferred_op_entry_t* op) {
    if (!op) return -1;
    
    vfs_mount_t* m = find_mount(op->path);
    if (!m || !m->fs) {
        debuglog(DEBUG_ERROR, "[VFS] No filesystem for deferred op on %s\n", op->path);
        return -1;
    }
    
    const char* rel_path = op->path;
    if (strlen(m->mountpoint) > 1) {
        rel_path = op->path + strlen(m->mountpoint);
        if (*rel_path == '/') rel_path++;
    }
    
    int result = -1;
    switch (op->type) {
        case VFS_DEFERRED_DELETE:
            if (m->fs->unlink) result = m->fs->unlink(m->fs_data, rel_path);
            break;
        case VFS_DEFERRED_RENAME:
            if (m->fs->rename) result = m->fs->rename(m->fs_data, rel_path, op->newpath);
            break;
        case VFS_DEFERRED_RMDIR:
            if (m->fs->rmdir) result = m->fs->rmdir(m->fs_data, rel_path);
            break;
        case VFS_DEFERRED_TRUNCATE:
            if (m->fs->truncate) result = m->fs->truncate(m->fs_data, rel_path, op->value);
            break;
        case VFS_DEFERRED_SYNC:
            if (m->fs->sync) result = m->fs->sync(m->fs_data);
            break;
    }
    
    if (op->data) {
        enhanced_heap_free(op->data, "deferred_data");
    }
    
    return result;
}

// Process all deferred operations
int vfs_process_deferred_ops(void) {
    if (!deferred_ops_enabled) return 0;
    
    int processed = 0;
    vfs_deferred_op_entry_t** prev = &deferred_ops;
    
    while (*prev) {
        vfs_deferred_op_entry_t* op = *prev;
        
        if (process_deferred_op(op) == 0) {
            *prev = op->next;
            enhanced_heap_free(op, "deferred_op");
            processed++;
        } else {
            prev = &op->next;
        }
    }
    
    if (processed > 0) {
        debuglog(DEBUG_INFO, "[VFS] Processed %d deferred operations\n", processed);
    }
    
    return processed;
}

// Cancel a deferred operation
int vfs_cancel_deferred_op(const char* path) {
    if (!path) return -1;
    
    vfs_deferred_op_entry_t** prev = &deferred_ops;
    for (vfs_deferred_op_entry_t* op = deferred_ops; op; op = op->next) {
        if (strcmp(op->path, path) == 0) {
            *prev = op->next;
            if (op->data) {
                enhanced_heap_free(op->data, "deferred_data");
            }
            enhanced_heap_free(op, "deferred_op");
            return 0;
        }
        prev = &op->next;
    }
    return -1;
}

// Check if there are deferred operations
bool vfs_has_deferred_ops(void) {
    return deferred_ops != NULL;
}

// Add a deferred operation
static int add_deferred_op(vfs_deferred_op_entry_t* op) {
    if (!deferred_ops_enabled) return -1;
    
    op->next = deferred_ops;
    deferred_ops = op;
    
    debuglog(DEBUG_INFO, "[VFS] Deferred %s on %s (will execute when file handles close)\n",
             op->type == VFS_DEFERRED_DELETE ? "delete" :
             op->type == VFS_DEFERRED_RENAME ? "rename" :
             op->type == VFS_DEFERRED_RMDIR ? "rmdir" :
             op->type == VFS_DEFERRED_TRUNCATE ? "truncate" : "sync",
             op->path);
    
    return 0;
}

// Deferred delete - mark file as deleted immediately, actual deletion when handles close
int vfs_defer_delete(const char* path) {
    if (!path) return -1;
    
    // First check if file has any open handles
    vfs_node_t* node = vfs_open(path, 0);
    if (node) {
        // File exists and is open - mark for deferred deletion
        node->flags |= VFS_DELETED;
        
        vfs_deferred_op_entry_t* op = (vfs_deferred_op_entry_t*)
            enhanced_heap_alloc(sizeof(vfs_deferred_op_entry_t), "deferred_delete");
        if (!op) {
            vfs_close(node);
            return -1;
        }
        
        memset(op, 0, sizeof(vfs_deferred_op_entry_t));
        op->type = VFS_DEFERRED_DELETE;
        strncpy(op->path, path, sizeof(op->path) - 1);
        
        add_deferred_op(op);
        
        // To the user, file is gone
        vfs_close(node);
        return 0;
    }
    
    // No open handles - try immediate delete
    return vfs_unlink(path);
}

// Deferred rename
int vfs_defer_rename(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return -1;
    
    vfs_node_t* node = vfs_open(oldpath, 0);
    if (node) {
        node->flags |= VFS_DELETED;
        
        vfs_deferred_op_entry_t* op = (vfs_deferred_op_entry_t*)
            enhanced_heap_alloc(sizeof(vfs_deferred_op_entry_t), "deferred_rename");
        if (!op) {
            vfs_close(node);
            return -1;
        }
        
        memset(op, 0, sizeof(vfs_deferred_op_entry_t));
        op->type = VFS_DEFERRED_RENAME;
        strncpy(op->path, oldpath, sizeof(op->path) - 1);
        strncpy(op->newpath, newpath, sizeof(op->newpath) - 1);
        
        add_deferred_op(op);
        
        vfs_close(node);
        return 0;
    }
    
    return vfs_rename(oldpath, newpath);
}

// Deferred truncate
int vfs_defer_truncate(const char* path, uint32_t length) {
    if (!path) return -1;
    
    vfs_node_t* node = vfs_open(path, VFS_WRITE);
    if (node) {
        if (node->truncate) {
            node->truncate(node, length);
        }
        vfs_close(node);
        
        // Also queue for sync
        vfs_deferred_op_entry_t* op = (vfs_deferred_op_entry_t*)
            enhanced_heap_alloc(sizeof(vfs_deferred_op_entry_t), "deferred_truncate");
        if (!op) return -1;
        
        memset(op, 0, sizeof(vfs_deferred_op_entry_t));
        op->type = VFS_DEFERRED_TRUNCATE;
        strncpy(op->path, path, sizeof(op->path) - 1);
        op->value = length;
        
        add_deferred_op(op);
        return 0;
    }
    
    return vfs_truncate(path, length);
}

// Unlink (delete) a file - uses deferred deletion if file is open
int vfs_unlink(const char* path) {
    if (!path) return -1;
    
    vfs_mount_t* m = find_mount(path);
    if (!m || !m->fs || !m->fs->unlink) {
        // Fall back to initrd - not supported
        debuglog(DEBUG_ERROR, "[VFS] unlink not supported for %s\n", path);
        return -1;
    }
    
    const char* rel_path = path;
    if (strlen(m->mountpoint) > 1) {
        rel_path = path + strlen(m->mountpoint);
        if (*rel_path == '/') rel_path++;
    }
    
    return m->fs->unlink(m->fs_data, rel_path);
}

// Create a directory
int vfs_mkdir(const char* path, uint32_t mode) {
    if (!path) return -1;
    
    vfs_mount_t* m = find_mount(path);
    if (!m || !m->fs || !m->fs->mkdir) {
        debuglog(DEBUG_ERROR, "[VFS] mkdir not supported for %s\n", path);
        return -1;
    }
    
    const char* rel_path = path;
    if (strlen(m->mountpoint) > 1) {
        rel_path = path + strlen(m->mountpoint);
        if (*rel_path == '/') rel_path++;
    }
    
    return m->fs->mkdir(m->fs_data, rel_path, mode);
}

// Remove a directory
int vfs_rmdir(const char* path) {
    if (!path) return -1;
    
    vfs_mount_t* m = find_mount(path);
    if (!m || !m->fs || !m->fs->rmdir) {
        debuglog(DEBUG_ERROR, "[VFS] rmdir not supported for %s\n", path);
        return -1;
    }
    
    const char* rel_path = path;
    if (strlen(m->mountpoint) > 1) {
        rel_path = path + strlen(m->mountpoint);
        if (*rel_path == '/') rel_path++;
    }
    
    return m->fs->rmdir(m->fs_data, rel_path);
}

// Rename a file
int vfs_rename(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return -1;
    
    vfs_mount_t* m = find_mount(oldpath);
    if (!m || !m->fs || !m->fs->rename) {
        debuglog(DEBUG_ERROR, "[VFS] rename not supported\n");
        return -1;
    }
    
    const char* rel_old = oldpath;
    const char* rel_new = newpath;
    
    if (strlen(m->mountpoint) > 1) {
        rel_old = oldpath + strlen(m->mountpoint);
        rel_new = newpath + strlen(m->mountpoint);
        if (*rel_old == '/') rel_old++;
        if (*rel_new == '/') rel_new++;
    }
    
    return m->fs->rename(m->fs_data, rel_old, rel_new);
}

// Truncate a file
int vfs_truncate(const char* path, uint32_t length) {
    if (!path) return -1;
    
    vfs_mount_t* m = find_mount(path);
    if (!m || !m->fs || !m->fs->truncate) {
        debuglog(DEBUG_ERROR, "[VFS] truncate not supported\n");
        return -1;
    }
    
    const char* rel_path = path;
    if (strlen(m->mountpoint) > 1) {
        rel_path = path + strlen(m->mountpoint);
        if (*rel_path == '/') rel_path++;
    }
    
    return m->fs->truncate(m->fs_data, rel_path, length);
}

// Get file status
int vfs_stat(const char* path, struct vfs_stat* statbuf) {
    if (!path || !statbuf) return -1;
    
    vfs_node_t* node = vfs_open(path, 0);
    if (!node) return -1;
    
    if (node->stat) {
        int result = node->stat(node, statbuf);
        vfs_close(node);
        return result;
    }
    
    // Default stat implementation
    memset(statbuf, 0, sizeof(struct vfs_stat));
    statbuf->ino = node->inode;
    statbuf->mode = node->flags;
    statbuf->nlink = node->open_count;
    statbuf->size = node->length;
    
    vfs_close(node);
    return 0;
}

// Change file mode
int vfs_chmod(const char* path, uint32_t mode) {
    if (!path) return -1;
    
    vfs_node_t* node = vfs_open(path, 0);
    if (!node) return -1;
    
    if (node->chmod) {
        int result = node->chmod(node, mode);
        vfs_close(node);
        return result;
    }
    
    node->mask = mode;
    vfs_close(node);
    return 0;
}

// Change file owner
int vfs_chown(const char* path, uint32_t uid, uint32_t gid) {
    if (!path) return -1;
    
    vfs_node_t* node = vfs_open(path, 0);
    if (!node) return -1;
    
    if (node->chown) {
        int result = node->chown(node, uid, gid);
        vfs_close(node);
        return result;
    }
    
    node->uid = uid;
    node->gid = gid;
    vfs_close(node);
    return 0;
}

// Sync filesystem
int vfs_sync(const char* path) {
    if (!path) {
        // Sync all filesystems
        for (vfs_mount_t* m = vfs_mounts; m; m = m->next) {
            if (m->fs && m->fs->sync) {
                m->fs->sync(m->fs_data);
            }
        }
        return 0;
    }
    
    vfs_mount_t* m = find_mount(path);
    if (!m || !m->fs || !m->fs->sync) {
        return 0; // Not an error for initrd
    }
    
    return m->fs->sync(m->fs_data);
}
