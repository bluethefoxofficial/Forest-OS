#include "include/ramdisk.h"
#include "include/multiboot.h"
#include "include/screen.h"
#include "include/panic.h"
#include "include/util.h"
#include "include/memory.h"

#define TAR_BLOCK_SIZE 512
#define MAX_RAMDISK_FILES 256
#define RAMDISK_MAX_NAME 256

// Maximum path length we can store for FAT entries.
#define FAT_MAX_PATH 240

// FAT cluster helpers.
#define FAT_TYPE_NONE 0
#define FAT_TYPE_12   12
#define FAT_TYPE_16   16
#define FAT_TYPE_32   32

// POSIX ustar header (fields that matter to us).
typedef struct {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
} __attribute__((packed)) tar_header_t;

typedef struct {
    uint8 bootjmp[3];
    uint8 oem_name[8];
    uint16 bytes_per_sector;
    uint8 sectors_per_cluster;
    uint16 reserved_sector_count;
    uint8 table_count;
    uint16 root_entry_count;
    uint16 total_sectors_16;
    uint8 media_type;
    uint16 table_size_16;
    uint16 sectors_per_track;
    uint16 head_side_count;
    uint32 hidden_sector_count;
    uint32 total_sectors_32;
} __attribute__((packed)) fat_bpb_t;

typedef struct {
    uint32 table_size_32;
    uint16 extended_flags;
    uint16 fat_version;
    uint32 root_cluster;
    uint16 fat_info;
    uint16 backup_BS_sector;
    uint8 reserved_0[12];
    uint8 drive_number;
    uint8 reserved1;
    uint8 boot_signature;
    uint32 volume_id;
    uint8 volume_label[11];
    uint8 fat_type_label[8];
} __attribute__((packed)) fat_ext32_t;

typedef struct {
    uint8 drive_number;
    uint8 reserved1;
    uint8 boot_signature;
    uint32 volume_id;
    uint8 volume_label[11];
    uint8 fat_type_label[8];
} __attribute__((packed)) fat_ext16_t;

typedef struct {
    uint8 name[11];
    uint8 attr;
    uint8 nt_reserved;
    uint8 creation_tenths;
    uint16 creation_time;
    uint16 creation_date;
    uint16 last_access_date;
    uint16 first_cluster_high;
    uint16 write_time;
    uint16 write_date;
    uint16 first_cluster_low;
    uint32 size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct {
    uint8 order;
    uint16 name1[5];
    uint8 attr;
    uint8 type;
    uint8 checksum;
    uint16 name2[6];
    uint16 first_cluster_low;
    uint16 name3[2];
} __attribute__((packed)) fat_lfn_entry_t;

typedef struct {
    const uint8* base;
    uint32 size;
    uint32 bytes_per_sector;
    uint32 sectors_per_cluster;
    uint32 reserved_sector_count;
    uint32 num_fats;
    uint32 fat_size_sectors;
    uint32 fat_offset_bytes;
    uint32 fat_size_bytes;
    uint32 root_entry_count;
    uint32 root_dir_first_sector;
    uint32 root_dir_sectors;
    uint32 first_data_sector;
    uint32 total_sectors;
    uint32 total_clusters;
    uint32 cluster_size_bytes;
    uint32 root_cluster;
    uint32 fat_type;
} fat_volume_t;

typedef struct {
    char chars[FAT_MAX_PATH];
    bool active;
} fat_lfn_buffer_t;

static ramdisk_file_t files[MAX_RAMDISK_FILES];
static char file_names[MAX_RAMDISK_FILES][RAMDISK_MAX_NAME];
static uint32 file_total = 0;
static bool ramdisk_ready = false;
static uint32 initrd_start = 0;
static uint32 initrd_size = 0;
static uint8 empty_file_stub = 0;



static bool cstring_equals(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static uint32 octal_to_uint(const char* src, uint32 len) {
    uint32 value = 0;
    for (uint32 i = 0; i < len; i++) {
        char c = src[i];
        if (c == ' ' || c == '\0') {
            break;
        }
        if (c < '0' || c > '7') {
            break;
        }
        value = (value << 3) + (uint32)(c - '0');
    }
    return value;
}

static const char* copy_filename(const char* src, uint32 index) {
    uint32 max_len = sizeof(file_names[0]) - 1;
    uint32 i = 0;

    // Skip tar prefixes like "./" or leading slashes.
    if (src[0] == '.' && src[1] == '/') {
        src += 2;
    }
    while (*src == '/') {
        src++;
    }

    for (; i < max_len && *src != '\0'; i++, src++) {
        file_names[index][i] = *src;
    }

    while (i > 0 && file_names[index][i - 1] == '/') {
        i--;
    }

    file_names[index][i] = '\0';
    return file_names[index];
}

static bool parse_tar(const uint8* base, uint32 size) {
    const uint8* cursor = base;
    const uint8* end = base + size;
    file_total = 0;

    while (cursor + TAR_BLOCK_SIZE <= end) {
        tar_header_t* header = (tar_header_t*)cursor;

        // End of archive is indicated by a zeroed filename.
        if (header->filename[0] == '\0') {
            return true;
        }

        uint32 file_size = octal_to_uint(header->size, sizeof(header->size));
        const uint8* data = cursor + TAR_BLOCK_SIZE;

        if (data > end || data + file_size > end) {
            print("[RAMDISK] Tar entry extends beyond module size\n");
            return false;
        }

        if (file_total < MAX_RAMDISK_FILES) {
            const char* name = copy_filename(header->filename, file_total);
            files[file_total].name = name;
            files[file_total].data = file_size ? data : &empty_file_stub;
            files[file_total].size = file_size;
            files[file_total].is_dir = (header->typeflag == '5');
            file_total++;
        } else {
            print("[RAMDISK] Too many files in archive; skipping extras\n");
        }

        // Move to next header (aligned to 512-byte boundary).
        uint32 file_blocks = (file_size + (TAR_BLOCK_SIZE - 1)) / TAR_BLOCK_SIZE;
        cursor = data + file_blocks * TAR_BLOCK_SIZE;
    }

    print("[RAMDISK] Reached end of module without tar terminator\n");
    return false;
}

static bool load_initrd_range(uint32 base, uint32 end) {
    if (base == 0 || end <= base) {
        print("[RAMDISK] Invalid initrd range\n");
        return false;
    }

    uint32 size = end - base;
    initrd_start = base;
    initrd_size = size;

    print("[RAMDISK] Initrd @ ");
    print(int_to_string(base));
    print(" size ");
    print(int_to_string(size));
    print(" bytes\n");

    // Identity map the initrd module before touching it.
    vmm_identity_map_range(vmm_get_current_page_directory(), base, end, PAGE_WRITABLE);

    if (!parse_tar((const uint8*)base, size)) {
        kernel_panic("Failed to parse initrd tar");
    }

    ramdisk_ready = true;
    print("[RAMDISK] Parsed ");
    print(int_to_string(file_total));
    print(" entries\n");
    return true;
}

static bool load_initrd_multiboot1(multiboot_info_t* mbi) {
    if (!mbi || mbi->mods_count == 0 || mbi->mods_addr == 0) {
        print("[RAMDISK] No multiboot modules; initrd not loaded\n");
        return false;
    }

    multiboot_module_t* mod = (multiboot_module_t*)mbi->mods_addr;
    return load_initrd_range(mod->mod_start, mod->mod_end);
}

static bool load_initrd_multiboot2(uint32 info_addr) {
    if (info_addr == 0) {
        return false;
    }

    multiboot2_info_t* hdr = (multiboot2_info_t*)info_addr;
    uint8* cursor = (uint8*)info_addr + sizeof(multiboot2_info_t);
    uint8* end = (uint8*)info_addr + hdr->total_size;

    while (cursor < end) {
        multiboot2_tag_t* tag = (multiboot2_tag_t*)cursor;
        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_MODULE) {
            multiboot2_tag_module_t* module = (multiboot2_tag_module_t*)tag;
            return load_initrd_range(module->mod_start, module->mod_end);
        }

        uint32 advance = (tag->size + 7) & ~7;
        cursor += advance;
    }

    print("[RAMDISK] No Multiboot2 module tag for initrd\n");
    return false;
}

bool ramdisk_init(uint32 magic, uint32 mbi_addr) {
    bool loaded = false;

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        loaded = load_initrd_multiboot1((multiboot_info_t*)mbi_addr);
    } else if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        loaded = load_initrd_multiboot2(mbi_addr);
    } else {
        print("[RAMDISK] Unsupported bootloader magic, cannot locate initrd\n");
    }

    return loaded;
}

uint32 ramdisk_file_count(void) {
    return ramdisk_ready ? file_total : 0;
}

const ramdisk_file_t* ramdisk_get(uint32 index) {
    if (!ramdisk_ready || index >= file_total) {
        return 0;
    }
    return &files[index];
}

const ramdisk_file_t* ramdisk_find(const char* path) {
    if (!ramdisk_ready || !path) {
        return 0;
    }

    for (uint32 i = 0; i < file_total; i++) {
        if (cstring_equals(files[i].name, path)) {
            return &files[i];
        }
    }
    return 0;
}
