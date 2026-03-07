/*
 * symlink.c - Symbolic link implementation for Forest OS
 *
 * Provides support for symbolic links (soft links) in the VFS.
 */

#include "include/vfs.h"
#include "include/string.h"
#include "include/enhanced_heap.h"
#include "include/debuglog.h"

#define SYMLINK_MAX_TARGET 4096

typedef struct {
    vfs_node_t base;
    char target[SYMLINK_MAX_TARGET];
    uint32_t target_length;
} symlink_node_t;

static uint32_t symlink_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;

    symlink_node_t* symlink = (symlink_node_t*)node->internal_data;
    if (!symlink) return 0;

    if (offset >= symlink->target_length) return 0;

    uint32_t remaining = symlink->target_length - offset;
    uint32_t to_copy = (remaining < size) ? remaining : size;

    memcpy(buffer, symlink->target + offset, to_copy);
    return to_copy;
}

static uint32_t symlink_write(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0; // Symbolic links are read-only
}

static bool symlink_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    (void)node;
    (void)index;
    (void)dirent;
    return false; // Symbolic links are not directories
}

