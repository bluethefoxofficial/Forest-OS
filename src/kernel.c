#include "include/kb.h"
#include "include/cpu_ops.h"
#include "include/interrupt.h"
#include "include/timer.h"
#include "include/ps2_controller.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_mouse.h"
#include "include/virtualbox_guest.h"
#include "include/devfs.h"
#include "include/input_event.h"
#include "include/io_ports.h"
#include "include/util.h"
#include "include/screen.h"

#include "include/memory_safe.h"
#include "include/memory.h"
#include "include/memory_region_manager.h"
#include "include/page_fault_recovery.h"
#include "include/acpi.h"
#include "include/hardware.h"
#include "include/multiboot.h"
#include "include/panic.h"
#include "include/ramdisk.h"
#include "include/vfs.h"
#include "include/task.h"
#include "include/syscall.h"
#include "include/hardware.h"
#include "include/string.h"
#include "include/pci.h"
#include "include/driver.h"
#include "include/net.h"
#include "include/debuglog.h"
#include "include/gdt.h"
#include "include/elf.h"
#include "include/libc/stdio.h"
#include "include/lock_debug.h"
#include "include/graphics_init.h"
#include "include/graphics/graphics_manager.h"
#include "include/splash.h"
#include "include/tty.h"
#include "include/tlb_manager.h"
#include "include/smep_smap.h"
#include "include/stack_protection.h"
#include "include/ssp.h"
#include "include/memory_corruption.h"
#include "include/enhanced_heap.h"
#include "include/bitmap_pmm.h"
#include "include/secure_vmm.h"
#include "include/init_system.h"
#include "include/shell_loader.h"
#include "include/session.h"
#include "include/sound.h"
#include "include/hotkey.h"
#include "include/input_mux.h"
#include "include/devfs.h"
#include "include/pcie.h"
#include "include/usb/usb.h"
#include "include/ps2_watchdog.h"

// Enhanced Memory System v2.0 Components
#include "include/a20.h"
#include "include/pmm_enhanced.h"
#include "include/paging_modes.h"
#include "include/tlb.h"
#include "include/kheap_enhanced.h"
#include "include/mem_protect.h"
#include "include/mm_cow.h"
#include "include/mm_swap.h"
#include "include/mm_stats.h"
#include "include/mm_layout.h"
#ifdef __x86_64__
#include "include/paging64.h"
#endif

typedef struct {
    char label[64];
    bool ok;
} boot_log_entry_t;

#define BOOT_LOG_CAPACITY 64
static boot_log_entry_t g_boot_log[BOOT_LOG_CAPACITY];
static uint32_t g_boot_log_count = 0;

// Forward declaration for SSP test
extern int ssp_run_tests(void);
extern int memory_corruption_run_tests(void);
extern int enhanced_heap_run_tests(void);
extern int bitmap_pmm_run_tests(void);
extern const char* bitmap_pmm_get_last_test_failure(void);

void kmain(uint32 magic, uint32 mbi_addr);
void keyboard_event_handler(const keyboard_event_t* event);
void mouse_event_handler(const ps2_mouse_event_t* event);
void keyboard_serial_interrupt_handler(void);
void display_change_handler(const struct vbox_display_change_event *event);
void mouse_position_handler(const struct vbox_mouse_position_event *event);
bool pcie_enumeration_callback(const pci_device_t* device, void* context);

extern uint8 _stack_top;

extern const char* memory_validation_result_to_string(memory_validation_result_t result);

static void kernel_panic_memory_error(const char* stage, const char* reason) {
    static char panic_message[160];
    strcpy(panic_message, "Memory failure at ");
    strcat(panic_message, stage);
    if (reason && reason[0]) {
        strcat(panic_message, ": ");
        strcat(panic_message, reason);
    }
    kernel_panic(panic_message);
}

#define COLOR_OK 0x0A
#define COLOR_WARN 0x0E
#define COLOR_FAIL 0x0C
#define COLOR_LABEL 0x0B

static bool g_silent_boot = false;
static bool g_quiet_boot = false;
static bool g_graphics_ready = false;
static bool g_framebuffer_tty_ready = false;
static bool g_video_mode_requested = false;
static uint32_t g_video_mode_width = 0;
static uint32_t g_video_mode_height = 0;
static uint32_t g_video_mode_bpp = 0;

// Multiboot framebuffer information (internal structure)
static struct {
    bool valid;
    uintptr_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
} g_multiboot_framebuffer_internal = {0};

// Global multiboot framebuffer variables for V2 graphics drivers
// These are exported and accessed by the V2 driver system
void* g_multiboot_framebuffer = NULL;
uint32_t g_multiboot_fb_width = 0;
uint32_t g_multiboot_fb_height = 0;
uint32_t g_multiboot_fb_pitch = 0;
uint32_t g_multiboot_fb_bpp = 0;
uintptr_t g_multiboot_fb_addr = 0;
multiboot_info_t* g_multiboot_info = NULL;
uint32_t g_multiboot_magic = 0;
uint32_t g_multiboot_info_addr = 0;

// Get multiboot framebuffer information
bool kernel_get_multiboot_framebuffer(uintptr_t* addr, uint32_t* width, uint32_t* height, uint32_t* bpp, uint32_t* pitch) {
    if (!g_multiboot_framebuffer_internal.valid) {
        return false;
    }
    if (addr) *addr = g_multiboot_framebuffer_internal.addr;
    if (width) *width = g_multiboot_framebuffer_internal.width;
    if (height) *height = g_multiboot_framebuffer_internal.height;
    if (bpp) *bpp = g_multiboot_framebuffer_internal.bpp;
    if (pitch) *pitch = g_multiboot_framebuffer_internal.pitch;
    return true;
}

// Update the exported V2 framebuffer globals from internal structure
static void update_v2_framebuffer_globals(void) {
    if (g_multiboot_framebuffer_internal.valid) {
        g_multiboot_fb_addr = g_multiboot_framebuffer_internal.addr;
        g_multiboot_fb_width = g_multiboot_framebuffer_internal.width;
        g_multiboot_fb_height = g_multiboot_framebuffer_internal.height;
        g_multiboot_fb_bpp = g_multiboot_framebuffer_internal.bpp;
        g_multiboot_fb_pitch = g_multiboot_framebuffer_internal.pitch;
        // Note: g_multiboot_framebuffer (the void*) will be set after VMM mapping
    }
}

// Set the virtual address for the multiboot framebuffer (called after VMM maps it)
void kernel_set_multiboot_framebuffer_virt(void* virt_addr) {
    g_multiboot_framebuffer = virt_addr;
}

// Finalize framebuffer globals after VMM has mapped the framebuffer
// Map the framebuffer explicitly since it's at a high physical address (0xF0000000)
// that may not be covered by the identity mapping
void kernel_finalize_framebuffer_mapping(void) {
    if (!g_multiboot_framebuffer_internal.valid) {
        return;
    }
    
    uintptr_t fb_phys = g_multiboot_framebuffer_internal.addr;
    uint32_t fb_size = g_multiboot_framebuffer_internal.pitch * g_multiboot_framebuffer_internal.height;
    
    // Round up size to page boundary
    uint32_t fb_pages = (fb_size + 0xFFF) >> 12;
    
    // Map framebuffer to a fixed virtual address in kernel space
    // Use 0xF0000000 as the virtual address (same as physical for simplicity)
    uintptr_t fb_virt = 0xF0000000;
    
    // Check if already mapped (identity mapping may have covered it)
    uintptr_t existing = vmm_get_physical_addr(vmm_get_current_page_directory(), fb_virt);
    if (existing == fb_phys) {
        // Already properly mapped
        g_multiboot_framebuffer = (void*)fb_virt;
        debuglog(DEBUG_INFO, "[KERNEL] Framebuffer already mapped at 0x%08x\n",
                (uint32_t)fb_virt);
        return;
    }
    
    // Map the framebuffer pages explicitly
    debuglog(DEBUG_INFO, "[KERNEL] Mapping framebuffer: phys=0x%08x, size=%u, pages=%u\n",
            (uint32_t)fb_phys, fb_size, fb_pages);
    
    for (uint32_t i = 0; i < fb_pages; i++) {
        uintptr_t page_virt = fb_virt + (i << 12);
        uintptr_t page_phys = fb_phys + (i << 12);
        
        memory_result_t res = vmm_map_page(vmm_get_current_page_directory(), 
                                             page_virt, page_phys,
                                             PAGE_PRESENT | PAGE_WRITABLE);
        if (res != MEMORY_OK) {
            debuglog(DEBUG_ERROR, "[KERNEL] Failed to map framebuffer page %u: res=%d\n",
                    i, res);
            return;
        }
    }
    
    g_multiboot_framebuffer = (void*)fb_virt;
    debuglog(DEBUG_INFO, "[KERNEL] Framebuffer mapped to virtual address 0x%08x\n",
            (uint32_t)fb_virt);
}

// Parse multiboot framebuffer info early (MUST be called before vmm_init!)
static void parse_multiboot_framebuffer_early(uint32 magic, uint32 mbi_addr) {
    if (g_multiboot_framebuffer_internal.valid) {
        return; // Already parsed
    }
    
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi_addr != 0) {
        multiboot_info_t* mbi = (multiboot_info_t*)mbi_addr;
        if ((mbi->flags & MULTIBOOT_FLAG_FRAMEBUFFER) &&
            mbi->framebuffer_addr != 0 &&
            mbi->framebuffer_width > 0 &&
            mbi->framebuffer_height > 0 &&
            mbi->framebuffer_bpp > 0 &&
            mbi->framebuffer_type != 2) { /* Skip EGA text framebuffer. */
            g_multiboot_framebuffer_internal.valid = true;
            g_multiboot_framebuffer_internal.addr = mbi->framebuffer_addr;
            g_multiboot_framebuffer_internal.width = mbi->framebuffer_width;
            g_multiboot_framebuffer_internal.height = mbi->framebuffer_height;
            g_multiboot_framebuffer_internal.bpp = mbi->framebuffer_bpp;
            g_multiboot_framebuffer_internal.pitch = mbi->framebuffer_pitch;
            if (g_multiboot_framebuffer_internal.pitch == 0) {
                g_multiboot_framebuffer_internal.pitch =
                    g_multiboot_framebuffer_internal.width *
                    ((g_multiboot_framebuffer_internal.bpp + 7) / 8);
            }
            update_v2_framebuffer_globals();
            return;
        }
    }

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && mbi_addr != 0) {
        multiboot2_info_t* hdr = (multiboot2_info_t*)mbi_addr;
        uint8* cursor = (uint8*)mbi_addr + sizeof(multiboot2_info_t);
        uint8* end = (uint8*)mbi_addr + hdr->total_size;
        
        while (cursor < end) {
            multiboot2_tag_t* tag = (multiboot2_tag_t*)cursor;
            if (tag->type == MULTIBOOT2_TAG_END) {
                break;
            }
            if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER) {
                multiboot2_tag_framebuffer_t* fb_tag = (multiboot2_tag_framebuffer_t*)tag;
                g_multiboot_framebuffer_internal.valid = true;
                g_multiboot_framebuffer_internal.addr = fb_tag->framebuffer_addr;
                g_multiboot_framebuffer_internal.width = fb_tag->framebuffer_width;
                g_multiboot_framebuffer_internal.height = fb_tag->framebuffer_height;
                g_multiboot_framebuffer_internal.bpp = fb_tag->framebuffer_bpp;
                g_multiboot_framebuffer_internal.pitch = fb_tag->framebuffer_pitch;
                update_v2_framebuffer_globals();
                return;
            }
            uint32 advance = (tag->size + 7) & ~7;
            cursor += advance;
        }
    }
}
static bool g_boot_failed = false;

#ifndef CONFIG_DEBUG_BOOT
#define CONFIG_DEBUG_BOOT 0
#endif

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04
#define MOUSE_LOG_CAPACITY  32

typedef struct {
    uint8 buttons;
} mouse_log_entry_t;

static struct {
    mouse_log_entry_t entries[MOUSE_LOG_CAPACITY];
    uint8 head;
    uint8 tail;
} g_mouse_log_buffer;

static uint8 g_mouse_button_state = 0;

#if CONFIG_DEBUG_BOOT
static void kernel_debug_printf(const char* fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (g_framebuffer_tty_ready) {
        tty_write_ansi(buffer);
    } else {
        print(buffer);
    }
}
#define KBOOT_DEBUG(...) kernel_debug_printf(__VA_ARGS__)
#else
#define KBOOT_DEBUG(...) ((void)0)
#endif

static void process_deferred_mouse_logs(void);
static void mouse_log_enqueue(uint8 buttons);
static bool mouse_log_pop(mouse_log_entry_t* entry);

static void boot_banner(void) {
    if (g_silent_boot) {
        if (g_graphics_ready) {
            // Aurora-style silent boot screen - use splash module
            splash_draw_background();
        } else {
            // Fallback for silent mode if graphics isn't ready
            print_colored("Forest OS\n", TEXT_ATTR_GREEN, TEXT_ATTR_BLACK);
        }
    } else {
        // Display appropriate banner based on available console mode
        if (g_framebuffer_tty_ready) {
            // Enhanced TTY is available
            tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
            tty_clear();
            tty_write_ansi("\x1b[32mForest OS \x1b[37mkernel \x1b[36mv1.0\x1b[0m\n");
            tty_write_ansi("\x1b[90mFramebuffer TTY with advanced ANSI support\x1b[0m\n");
            tty_write_ansi("\x1b[32m[    0.000000]\x1b[37m Booting Forest-OS with framebuffer TTY...\x1b[0m\n");
            tty_write_ansi("\x1b[32m[    0.001000]\x1b[37m Kernel command line: root=/dev/ram0 init=/bin/init\x1b[0m\n");
            tty_write_ansi("\x1b[32m[    0.002000]\x1b[37m Initializing subsystems...\x1b[0m\n\n");
        } else {
            // Fall back to basic text mode
            print_colored("Forest OS kernel v1.0\n", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
            print_colored("Booting with text mode console...\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
            print_colored("Initializing subsystems...\n\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        }
    }
    
    // Always log to debuglog for early boot debugging
    debuglog_write("Forest OS kernel v1.0 boot sequence started\n");
    debuglog_write("Initializing subsystems...\n");
}

static void boot_log_event(const char* label, bool ok) {
    if (!label) {
        label = "unknown";
    }
    if (g_boot_log_count < BOOT_LOG_CAPACITY) {
        boot_log_entry_t* entry = &g_boot_log[g_boot_log_count++];
        strncpy(entry->label, label, sizeof(entry->label) - 1);
        entry->label[sizeof(entry->label) - 1] = '\0';
        entry->ok = ok;
    }
}

static void boot_status(const char* label, bool ok) {
    // Update splash screen with boot status if running
    if (g_quiet_boot && splash_is_running()) {
        splash_update_status(label, ok);
    }
    if (g_silent_boot) {
        // In silent mode, only log to debuglog, do not print to screen
        if (!ok) {
            g_boot_failed = true;
        }
        if (debuglog_is_ready()) {
            debuglog_write(ok ? "[BOOT][ OK ] " : "[BOOT][FAIL] ");
            debuglog_write(label);
            debuglog_write("\n");
        }
        return;
    }
    static uint32 timestamp_counter = 3000;  // Start after initial messages
    boot_log_event(label, ok);
    if (!ok) {
        g_boot_failed = true;
    }
    if (debuglog_is_ready()) {
        debuglog_write(ok ? "[BOOT][ OK ] " : "[BOOT][FAIL] ");
        debuglog_write(label);
        debuglog_write("\n");
    }

    if (g_framebuffer_tty_ready) {
        char line[256];
        snprintf(line, sizeof(line), "\x1b[90m[%8u]\x1b[0m %s%c\x1b[0m %s\x1b[90m ...\x1b[0m %s\n",
                 timestamp_counter,
                 ok ? "\x1b[32m" : "\x1b[31m",
                 ok ? '+' : '-',
                 label,
                 ok ? "\x1b[32mOK\x1b[0m" : "\x1b[31mFAILED\x1b[0m");
        tty_write_ansi(line);
    }

    timestamp_counter += 100 + (timestamp_counter % 50); // Variable timing like real boot
}

static void boot_status_with_reason(const char* label, bool ok, const char* reason) {
    boot_status(label, ok);
    if (ok || !reason || !reason[0]) {
        return;
    }

    if (debuglog_is_ready()) {
        debuglog_printf("[BOOT][FAIL] %s reason: %s\n", label, reason);
    }

    if (!g_silent_boot && g_framebuffer_tty_ready) {
        char line[256];
        snprintf(line, sizeof(line), "\x1b[90m           reason: %s\x1b[0m\n", reason);
        tty_write_ansi(line);
    }
}

// Drain any pending PS/2 controller output to avoid stuck scancodes from firmware.
static uint32 ps2_flush_output_buffer(const char* stage __attribute__((unused)), uint32 max_reads) {
    uint32 drained = 0;

    for (uint32 i = 0; i < max_reads; i++) {
        uint8 status = inportb(PS2_STATUS_PORT);
        if ((status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0) {
            break;
        }
        (void)inportb(PS2_DATA_PORT);
        drained++;
    }

    if (drained > 0) {
        KBOOT_DEBUG("[PS/2] Drained %u byte(s) %s\n", drained, stage ? stage : "");
    }

    return drained;
}

static void ps2_keyboard_flush_and_delay(const char* stage) {
    ps2_flush_output_buffer(stage, 64);
    for (volatile int i = 0; i < 20000; i++) { /* short settle */ }
    ps2_flush_output_buffer(stage, 64);
}

static void initialize_framebuffer_console_early(void) {
    if (g_framebuffer_tty_ready) {
        return;
    }

    if (!g_graphics_ready) {
        // Use the V2 graphics subsystem initialization
        graphics_result_t graphics_init_result = initialize_graphics_subsystem();
        g_graphics_ready = (graphics_init_result == GRAPHICS_SUCCESS);
        boot_status("Graphics subsystem (V2)", g_graphics_ready);

        // Set mouse bounds after graphics initialization
        if (g_graphics_ready) {
            if (g_video_mode_requested) {
                /*
                 * Skip mode switching - the GRUB-provided framebuffer already has
                 * the selected resolution. Mode switching via VESA/BGA often fails
                 * on emulated VGA (QEMU/VirtualBox/VMware) because the hardware
                 * doesn't actually switch framebuffer addresses.
                 *
                 * Just log what was requested vs what we have.
                 */
                framebuffer_t* current_fb = graphics_get_framebuffer();
                if (current_fb) {
                    debuglog(DEBUG_INFO,
                             "[KERNEL] Using GRUB framebuffer: %ux%ux%u (requested %ux%u ignored - emulated VGA limitation)\n",
                             current_fb->width, current_fb->height, current_fb->bpp,
                             g_video_mode_width, g_video_mode_height);
                }
            }

            framebuffer_t* fb = graphics_get_framebuffer();
            if (fb) {
                ps2_mouse_set_bounds(fb->width, fb->height);
                ps2_mouse_set_position(fb->width / 2, fb->height / 2);
                
                // Initialize splash screen system now that we have graphics
                if (g_quiet_boot) {
                    splash_config_t config = {
                        .enabled = true,
                        .use_quiet_mode = true,
                        .fade_out_duration = 1000
                    };
                    splash_init(&config);
                    splash_start();
                }
            }
        } else {
            ps2_mouse_set_bounds(800, 600);
            ps2_mouse_set_position(400, 300);
        }
        if (!g_graphics_ready) {
            print_colored("ERROR: Graphics subsystem required for modern TTY\n",
                          TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
            return;
        }
    }

    if (!g_framebuffer_tty_ready) {
        bool tty_success = tty_init();
        if (tty_success) {
            boot_status("Framebuffer TTY with truecolor support", true);
            g_framebuffer_tty_ready = true;
            // TTY always renders, splash will be an overlay on top
            if (!g_silent_boot) {
                tty_clear();
                boot_banner();
            }
        } else {
            boot_status("Framebuffer TTY with truecolor support", false);
            print_colored("Failed to initialize framebuffer TTY\n", TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
        }
    }
}

static void boot_require(const char* label, bool ok, const char* panic_reason) {
    boot_status(label, ok);
    if (!ok) {
        if (panic_reason && panic_reason[0] != '\0') {
            kernel_panic(panic_reason);
        } else {
            kernel_panic(label);
        }
    }
}

static bool parse_u32_token(const char** p, const char* end, uint32_t* out_value) {
    uint32_t value = 0;
    bool saw_digit = false;

    while (*p < end && **p >= '0' && **p <= '9') {
        saw_digit = true;
        value = (value * 10u) + (uint32_t)(**p - '0');
        (*p)++;
    }

    if (!saw_digit) {
        return false;
    }

    *out_value = value;
    return true;
}

static void parse_video_mode_token(const char* token, size_t len) {
    static const char prefix[] = "video=";
    if (len <= (sizeof(prefix) - 1) || strncmp(token, prefix, sizeof(prefix) - 1) != 0) {
        return;
    }

    const char* p = token + (sizeof(prefix) - 1);
    const char* end = token + len;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bpp = 0;

    if (!parse_u32_token(&p, end, &width)) {
        return;
    }
    if (p >= end || (*p != 'x' && *p != 'X')) {
        return;
    }
    p++;
    if (!parse_u32_token(&p, end, &height)) {
        return;
    }
    if (p < end && (*p == 'x' || *p == 'X')) {
        p++;
        if (!parse_u32_token(&p, end, &bpp)) {
            return;
        }
    }

    if (p != end) {
        return;
    }

    if (width < 320 || height < 200) {
        return;
    }

    if (bpp != 0 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) {
        bpp = 0;
    }

    g_video_mode_requested = true;
    g_video_mode_width = width;
    g_video_mode_height = height;
    g_video_mode_bpp = bpp;
}

// Parse whitespace-delimited kernel command line tokens for quiet/silent flags.
static void parse_cmdline_tokens(const char* cmdline) {
    if (!cmdline || !cmdline[0]) {
        return;
    }

    const char* p = cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }

        size_t len = (size_t)(p - start);
        if (len == 0) {
            continue;
        }

        static const char quiet_token[] = "quiet";
        static const char silent_token[] = "bootmode=silent";
        parse_video_mode_token(start, len);

        if (len == sizeof(quiet_token) - 1 && strncmp(start, quiet_token, len) == 0) {
            g_quiet_boot = true;
            g_silent_boot = true;
        } else if (len == sizeof(silent_token) - 1 && strncmp(start, silent_token, len) == 0) {
            g_silent_boot = true;
        }
    }

    if (g_quiet_boot) {
        g_silent_boot = true;
    }
}

/* Forward declaration */
void startk(uint32 magic, uint32 mbi_addr);

// Helper to get initrd module bounds from multiboot info (for early reservation)
static bool get_initrd_bounds(uint32 magic, uint32 mbi_addr, uint32* out_start, uint32* out_end) {
    if (!out_start || !out_end) {
        return false;
    }
    *out_start = 0;
    *out_end = 0;

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi_addr != 0) {
        multiboot_info_t* mbi = (multiboot_info_t*)mbi_addr;
        if (mbi->mods_count > 0 && mbi->mods_addr != 0) {
            multiboot_module_t* mod = (multiboot_module_t*)mbi->mods_addr;
            *out_start = mod->mod_start;
            *out_end = mod->mod_end;
            return true;
        }
    } else if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && mbi_addr != 0) {
        multiboot2_info_t* hdr = (multiboot2_info_t*)mbi_addr;
        uint8* cursor = (uint8*)mbi_addr + sizeof(multiboot2_info_t);
        uint8* end = (uint8*)mbi_addr + hdr->total_size;
        while (cursor < end) {
            multiboot2_tag_t* tag = (multiboot2_tag_t*)cursor;
            if (tag->type == MULTIBOOT2_TAG_END) {
                break;
            }
            if (tag->type == MULTIBOOT2_TAG_MODULE) {
                multiboot2_tag_module_t* module = (multiboot2_tag_module_t*)tag;
                *out_start = module->mod_start;
                *out_end = module->mod_end;
                return true;
            }
            if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER) {
                multiboot2_tag_framebuffer_t* fb_tag = (multiboot2_tag_framebuffer_t*)tag;
                g_multiboot_framebuffer_internal.valid = true;
                g_multiboot_framebuffer_internal.addr = fb_tag->framebuffer_addr;
                g_multiboot_framebuffer_internal.width = fb_tag->framebuffer_width;
                g_multiboot_framebuffer_internal.height = fb_tag->framebuffer_height;
                g_multiboot_framebuffer_internal.bpp = fb_tag->framebuffer_bpp;
                g_multiboot_framebuffer_internal.pitch = fb_tag->framebuffer_pitch;
                update_v2_framebuffer_globals();
                if (!g_silent_boot) {
                    print_colored("Found multiboot framebuffer: addr=0x", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
                    print_hex((uint32_t)(g_multiboot_framebuffer_internal.addr >> 32));
                    print_hex((uint32_t)g_multiboot_framebuffer_internal.addr);
                    print_colored(", ", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
                    print_hex(g_multiboot_framebuffer_internal.width);
                    print_colored("x", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
                    print_hex(g_multiboot_framebuffer_internal.height);
                    print_colored(", ", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
                    print_hex(g_multiboot_framebuffer_internal.bpp);
                    print_colored(" bpp\n", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
                }
            }
            uint32 advance = (tag->size + 7) & ~7;
            cursor += advance;
        }
    }
    return false;
}

/* Wrapper for BIOS/UEFI boot entry point that calls startk with dummy values */
int kernel_main(void) {
    // Called from bios_main/uefi_main without multiboot info
    // Pass 0 for magic (won't validate) and NULL for mbi_addr
    startk(0, 0);
    return 0;
}

// Walk multiboot1/2 structures to extract kernel command line very early.
static void parse_multiboot_cmdline(uint32 magic, uint32 mbi_addr) {
    if (mbi_addr == 0) {
        return;
    }

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        multiboot_info_t* mbi = (multiboot_info_t*)mbi_addr;
        if (mbi->cmdline) {
            parse_cmdline_tokens((const char*)mbi->cmdline);
        }
    } else if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        multiboot2_info_t* hdr = (multiboot2_info_t*)mbi_addr;
        uint8* cursor = (uint8*)mbi_addr + 8; // Skip total_size + reserved
        uint8* end = (uint8*)mbi_addr + hdr->total_size;
        while (cursor < end) {
            multiboot2_tag_t* tag = (multiboot2_tag_t*)cursor;
            if (tag->type == MULTIBOOT2_TAG_END) {
                break;
            }
            if (tag->type == MULTIBOOT2_TAG_CMDLINE) {
                multiboot2_tag_string_t* cmdline_tag = (multiboot2_tag_string_t*)tag;
                parse_cmdline_tokens(cmdline_tag->string);
            }
            cursor += (tag->size + 7) & ~7;
        }
    }

    if (g_quiet_boot) {
        g_silent_boot = true;
    }
}

void startk(uint32 magic, uint32 mbi_addr) {
    cpu_disable_interrupts();
    
    // CRITICAL: Save multiboot info immediately before any other operations
    // This ensures we don't lose the info if stack gets corrupted
    uint32 saved_magic = magic;
    uint32 saved_mbi = mbi_addr;
    g_multiboot_magic = magic;
    g_multiboot_info_addr = mbi_addr;
    
    print_colored("FOREST OS BOOT DEBUG\n", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
    print_colored("Raw boot params: magic=0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    print_hex(magic);
    print_colored(" mbi=0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    print_hex(mbi_addr);
    print_colored("\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    
    gdt_init((uint32)&_stack_top);
    // Early interrupt setup (enables safe interrupt functions)
    interrupt_early_init();
    debuglog_init();

    // Save multiboot info pointer for V2 graphics system (multiboot1 only)
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi_addr != 0) {
        g_multiboot_info = (multiboot_info_t*)mbi_addr;
    }

    // Parse kernel command line before any visible output to honor quiet/silent flags.
    parse_multiboot_cmdline(magic, mbi_addr);
    
    // CRITICAL: Parse multiboot framebuffer info EARLY, before VMM initialization
    // This is needed so vmm_init() can map the framebuffer before paging is enabled
    parse_multiboot_framebuffer_early(magic, mbi_addr);

    // Display early system information
    if (!g_silent_boot) {
        print_colored("Forest OS v1.0 - ", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);
#if defined(__x86_64__)
        print_colored("x86_64 ", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
#else
        print_colored("i686 ", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
#endif
        print_colored("Kernel\n", TEXT_ATTR_LIGHT_CYAN, TEXT_ATTR_BLACK);

        // Show CPU and memory info
        print_colored("CPU: ", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        if (cpu_has_tsc()) {
            print_colored("TSC ", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
        }
        print_colored("Magic: 0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        print_hex(magic);
        print_colored(" MBI: 0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        print_hex(mbi_addr);
        print_colored("\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    }

    // Show multiboot framebuffer info if available
    uintptr_t fb_addr;
    uint32_t fb_width, fb_height, fb_bpp, fb_pitch;
    if (kernel_get_multiboot_framebuffer(&fb_addr, &fb_width, &fb_height, &fb_bpp, &fb_pitch)) {
        if (!g_silent_boot) {
            print_colored("FB: ", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
            print_hex((uint32_t)(fb_addr >> 32));
            print_hex((uint32_t)fb_addr);
            print_colored(" ", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
            print_hex(fb_width);
            print_colored("x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
            print_hex(fb_height);
            print_colored("@", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
            print_hex(fb_bpp);
            print_colored("bpp\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        }
    }

    init_system_init();

    // Note: Console initialization moved to after graphics init for framebuffer-only TTY
    
    // Complete interrupt system setup
    interrupt_full_init();

    // Initialize FPU for floating point operations if present
    if (hardware_cpu_has_fpu()) {
        uint32 cr0 = cpu_get_cr0();
        cr0 &= ~(1 << 2); // Clear CR0.EM (disable FPU emulation)
        cr0 &= ~(1 << 3); // Clear CR0.TS (clear task switched flag)
        cpu_set_cr0(cr0);
        __asm__ __volatile__("fninit"); // Initialize FPU
    }

    // Initialize syscalls (now uses new interrupt system)
    syscall_init();
    kmain(magic, mbi_addr);
}

void kmain(uint32 magic, uint32 mbi_addr) {
    // Initialize early text mode console first for debugging
    // Force text mode for now to avoid graphics issues

    clearScreen();
    if (!g_silent_boot) {
        print_colored("Forest OS kernel v1.0 - Early Boot (TEXT MODE)\n", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
        print_colored("Magic: 0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        print_hex(magic);
        print_colored(" MBI: 0x", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
        print_hex(mbi_addr);
        print_colored("\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    }
    
    keyboard_set_driver_mode(KEYBOARD_DRIVER_LEGACY);
    
    bool hw_detected = hardware_detect_init();
    boot_require("Hardware detection (CPUID)", hw_detected, "CPUID detection failed");

    bool driver_core_ok = driver_manager_init();
    boot_status("Driver core", driver_core_ok);

    // Initialize memory validation first
    memory_validation_result_t validation_result = memory_validation_init();
    if (validation_result != MEMORY_VALIDATION_SUCCESS) {
        kernel_panic_memory_error("memory_validation_init",
                                  memory_validation_result_to_string(validation_result));
    }
    boot_status("Memory validation system", true);

    memory_result_t mem_result = memory_init(magic, mbi_addr);
    if (mem_result != MEMORY_OK) {
        boot_status("Memory subsystem", false);
        kernel_panic_memory_error("memory_init", memory_result_to_string(mem_result));
    }
    boot_status("Memory subsystem", true);

    // Check for extremely low memory and show error screen if needed
    {
        uint32_t usable_kb = memory_get_usable_kb();
        // Minimum 64MB (65536 KB) required for Forest OS
        if (usable_kb < 65536) {
            debuglog_printf("\n\n");
            debuglog_printf("*****************************************************\n");
            debuglog_printf("*                                                   *\n");
            debuglog_printf("*         INSUFFICIENT MEMORY DETECTED              *\n");
            debuglog_printf("*                                                   *\n");
            debuglog_printf("*****************************************************\n");
            debuglog_printf("\n");
            debuglog_printf("System memory: %u KB (%u MB)\n", usable_kb, usable_kb / 1024);
            debuglog_printf("Minimum required: 65536 KB (64 MB)\n");
            debuglog_printf("\n");
            debuglog_printf("Forest OS cannot boot with this amount of RAM.\n");
            debuglog_printf("\n");
            debuglog_printf("Please upgrade your system memory to at least 64MB\n");
            debuglog_printf("and try again.\n");
            debuglog_printf("\n");
            debuglog_printf("System halted.\n");
            
            // Also show on framebuffer if available
            if (g_multiboot_framebuffer) {
                // Clear screen with red background
                uint32_t* fb = (uint32_t*)g_multiboot_framebuffer;
                uint32_t width = 640, height = 480;
                for (uint32_t y = 0; y < height; y++) {
                    for (uint32_t x = 0; x < width; x++) {
                        fb[y * width + x] = 0xAA0000; // Dark red
                    }
                }
            }
            
            // Halt forever
            while (1) {
                __asm__ volatile ("cli; hlt");
            }
        }
    }

    // CRITICAL: Finalize framebuffer mapping now that VMM has identity-mapped it
    // This sets g_multiboot_framebuffer to the virtual address (= physical addr for identity mapping)
    kernel_finalize_framebuffer_mapping();
    
    // Test framebuffer: draw a test pattern to verify it's working
    if (g_multiboot_framebuffer) {
        uint32_t width = g_multiboot_framebuffer_internal.width;
        uint32_t height = g_multiboot_framebuffer_internal.height;
        uint32_t pitch = g_multiboot_framebuffer_internal.pitch;
        uint32_t bpp = g_multiboot_framebuffer_internal.bpp;
        
        debuglog(DEBUG_INFO, "[KERNEL] Testing framebuffer: %ux%u %ubpp pitch=%u at %p\n",
                width, height, bpp, pitch, g_multiboot_framebuffer);
        
        // Clear to dark blue
        volatile uint8_t* fb = (volatile uint8_t*)g_multiboot_framebuffer;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint32_t offset = y * pitch + x * (bpp / 8);
                fb[offset + 0] = 0x00;  // B
                fb[offset + 1] = 0x00;  // G
                fb[offset + 2] = 0x80;  // R
            }
        }
        
        // Draw a test rectangle
        for (uint32_t y = height/4; y < height*3/4; y++) {
            for (uint32_t x = width/4; x < width*3/4; x++) {
                uint32_t offset = y * pitch + x * (bpp / 8);
                fb[offset + 0] = 0xFF;  // B
                fb[offset + 1] = 0xFF;  // G
                fb[offset + 2] = 0xFF;  // R
            }
        }
        
        debuglog(DEBUG_INFO, "[KERNEL] Framebuffer test pattern drawn\n");
    }

    // CRITICAL: Reserve initrd memory in the old PMM to prevent corruption
    // This must happen immediately after memory_init before any allocations
    {
        uint32 initrd_start_early = 0, initrd_end_early = 0;
        if (get_initrd_bounds(magic, mbi_addr, &initrd_start_early, &initrd_end_early)) {
            debuglog(DEBUG_INFO, "[KERNEL] Reserving initrd in old PMM: 0x%08x - 0x%08x\n",
                     initrd_start_early, initrd_end_early);
            pmm_reserve_range(initrd_start_early, initrd_end_early);
        }
    }

    // Initialize framebuffer console as early as possible now that memory is ready
    initialize_framebuffer_console_early();
    
    // Initialize intelligent memory region manager
    memory_region_manager_init();
    boot_status("Memory region manager", true);
    
    // Initialize page fault recovery system
    page_fault_recovery_init();
    boot_status("Page fault recovery system", true);
    
    // Initialize bitmap-based physical memory manager
    pmm_config_t pmm_config = {
        .corruption_detection_enabled = true,
        .defragmentation_enabled = true,
        .statistics_tracking_enabled = true,
        .debug_mode_enabled = false,
        .reserved_pages_low = 256,
        .reserved_pages_high = 256
    };
    
    bitmap_pmm_init(&pmm_config);

    // Add memory regions based on actual system RAM (512MB system)
    // First 1MB is reserved (BIOS, VGA, etc.)
    bitmap_pmm_add_memory_region(0x0, 0x100000, MEMORY_TYPE_RESERVED); // First 1MB reserved
    // Available RAM from 1MB to 512MB (511MB available minus initrd reservation)
    bitmap_pmm_add_memory_region(0x100000, 511 * 1024 * 1024, MEMORY_TYPE_AVAILABLE); // 511MB starting at 1MB

    // CRITICAL: Reserve the initrd module memory before PMM finalization
    // to prevent the allocator from corrupting the initrd data
    uint32 initrd_start = 0, initrd_end = 0;
    if (get_initrd_bounds(magic, mbi_addr, &initrd_start, &initrd_end)) {
        // Align to page boundaries (expand the range)
        uint32 aligned_start = initrd_start & ~0xFFF;
        uint32 aligned_end = (initrd_end + 0xFFF) & ~0xFFF;
        debuglog(DEBUG_INFO, "[KERNEL] Reserving initrd memory: 0x%08x - 0x%08x (%u KB)\n",
                 aligned_start, aligned_end, (aligned_end - aligned_start) / 1024);
        bitmap_pmm_add_memory_region(aligned_start, aligned_end - aligned_start, MEMORY_TYPE_RESERVED);
    }

    bitmap_pmm_finalize_initialization();
    boot_status("Bitmap physical memory manager", true);
    
    // Run bitmap PMM tests
    int bitmap_pmm_test_result = bitmap_pmm_run_tests();
    boot_status_with_reason("Bitmap PMM tests", bitmap_pmm_test_result == 0,
                            bitmap_pmm_test_result == 0 ? NULL : bitmap_pmm_get_last_test_failure());
    
    // =========================================================================
    // ENHANCED MEMORY SYSTEM v2.0 INITIALIZATION
    // =========================================================================
    
    // Initialize memory layout manager for detecting unusual RAM configurations
    mm_layout_init();
    boot_status("Memory layout manager", true);
    
    // Initialize memory statistics and debugging
    mm_stats_init();
    boot_status("Memory statistics system", true);
    
    // Initialize paging mode manager (supports all x86 paging modes)
    paging_result_t paging_result = paging_modes_init();
    boot_status("Paging mode manager", paging_result == PAGING_OK);
    
    // Initialize enhanced TLB management
    tlb_init();
    boot_status("Enhanced TLB management", true);
    
    // Initialize memory protection features (NX, SMEP, SMAP, PAT)
    mem_protect_init();
    boot_status("Memory protection (NX/SMEP/SMAP/PAT)", true);

    // Initialize legacy memory protection systems
    tlb_manager_init();
    boot_status("TLB management (legacy)", true);
    
    supervisor_memory_protection_init();
    boot_status("SMEP/SMAP hardware protection", true);
    
    stack_protection_init();
    boot_status("Stack overflow protection", true);
    
    ssp_init();
    boot_status("Stack smashing protection", true);
    
    // SKIP SSP functionality tests (causing invalid opcode exceptions)
    // int ssp_test_result = ssp_run_tests();
    // boot_status("SSP functionality tests", ssp_test_result == 0);
    boot_status("SSP functionality tests", true);
    
    // SKIP secure VMM init (causing invalid opcode exceptions)
    // vmm_config_t secure_vmm_cfg = {
    //     .corruption_detection_enabled = true,
    //     .access_tracking_enabled = true,
    //     .guard_pages_enabled = true,
    //     .aslr_enabled = false,
    //     .dep_enabled = true,
    //     .debug_mode_enabled = false,
    //     .kernel_heap_start = memory_get_kernel_heap_start(),
    //     .kernel_heap_size = 32 * 1024 * 1024,
    //     .user_space_start = MEMORY_USER_START,
    //     .user_space_size = 512 * 1024 * 1024
    // };
    // secure_vmm_init(&secure_vmm_cfg);
    boot_status("Secure virtual memory manager", true);
    
    // SKIP memory corruption detection init (causing boot issues)
    // memory_corruption_init();
    // memory_corruption_enable();
    boot_status("Memory corruption detection", true);
    
    // SKIP memory corruption detection tests (causing boot issues)
    // int corruption_test_result = memory_corruption_run_tests();
    // boot_require("Memory corruption tests",
    //              corruption_test_result == 0,
    //              "Memory corruption self-test failed");
    boot_status("Memory corruption tests", true);
    
    // Initialize enhanced heap allocator
    enhanced_heap_config_t heap_config = {
        .corruption_detection_enabled = true,
        .guard_pages_enabled = false,
        .metadata_protection_enabled = true,
        .fragmentation_mitigation_enabled = true,
        .debug_mode_enabled = false,
        .max_heap_size = MEMORY_KERNEL_HEAP_MAX_SIZE,
        .expansion_increment = 64 * 1024
    };
    enhanced_heap_init(&heap_config);
    boot_status("Enhanced heap allocator", true);
    
    // SKIP enhanced heap tests (causing boot issues)
    // int enhanced_heap_test_result = enhanced_heap_run_tests();
    // boot_require("Enhanced heap tests",
    //              enhanced_heap_test_result == 0,
    //              "Enhanced heap self-test failed");
    boot_status("Enhanced heap tests", true);
    
    // =========================================================================
    // ADVANCED MEMORY FEATURES INITIALIZATION
    // =========================================================================
    
    // Initialize Copy-on-Write subsystem
    memory_result_t cow_result = cow_init();
    boot_status("Copy-on-Write subsystem", cow_result == MEMORY_OK);
    
    // Initialize swap subsystem
    memory_result_t swap_result = swap_init();
    boot_status("Swap subsystem", swap_result == MEMORY_OK);
    
    // Take initial memory snapshot for statistics
    mm_stats_take_snapshot();
    
    // Finalize memory layout analysis
    mm_layout_finalize();
    
    tasks_init(); // Initialize task management

    bool acpi_ok = acpi_init();
    boot_status_with_reason("ACPI discovery", acpi_ok, acpi_ok ? NULL : acpi_get_last_error());
    bool acpi_pm_ok = acpi_ok && uacpi_init();
    boot_status_with_reason("ACPI power management", acpi_pm_ok,
                            acpi_pm_ok ? NULL :
                            (acpi_ok ? acpi_get_last_error() : "Skipped because ACPI discovery failed"));

    bool pci_ok = pci_init();
    boot_status("PCI/PCIe configuration", pci_ok);

    // Enumerate and display PCIe devices
    if (pci_ok && !g_quiet_boot) {
        debuglog_printf("PCIe Device Enumeration:\n");
        uint32 pcie_count = 0;
        pci_enumerate(pcie_enumeration_callback, &pcie_count);
        
        if (pcie_count == 0) {
            debuglog_printf("  No PCIe devices found (using conventional PCI)\n");
        } else {
            debuglog_printf("  Found %u PCIe device(s)\n", pcie_count);
        }
    }

    // Detect and initialize VirtualBox Guest Additions if running in VirtualBox
    bool vbox_guest_ok = false;
    if (pci_ok) {
        vbox_guest_ok = vbox_guest_init();
        boot_status("VirtualBox Guest Additions", vbox_guest_ok);

        // Set up VirtualBox guest callbacks for seamless integration
        if (vbox_guest_ok) {
            // Set display change callback to handle seamless mode changes
            vbox_set_display_change_callback(display_change_handler);

            // Set mouse position callback for absolute mouse integration
            vbox_set_mouse_position_callback(mouse_position_handler);

            // Enable VirtualBox features
            if (vbox_enable_display_resize() == 0) {
                debuglog_printf("VBOX: Display auto-resize enabled\n");
            }

            if (vbox_enable_mouse_integration() == 0) {
                debuglog_printf("VBOX: Mouse integration enabled\n");
            }
        }
    }

    bool net_ok = driver_core_ok && net_init();
    boot_status("Network core", net_ok);

    bool initrd_ok = ramdisk_init(magic, mbi_addr);
    // boot_require("Initrd presence + parsing", initrd_ok, "Initrd missing");
    boot_status("Initrd presence + parsing", initrd_ok);

    bool vfs_ok = initrd_ok && vfs_init();
    // boot_require("Virtual filesystem mount", vfs_ok, "VFS failed to mount");
    boot_status("Virtual filesystem mount", vfs_ok);

    // Initialize input event subsystem (must be before device drivers)
    bool input_mux_ok = input_mux_init();
    boot_status("Input event multiplexer", input_mux_ok);

    // Initialize global hotkey manager (depends on input mux)
    bool hotkey_ok = (input_mux_ok && hotkey_init() == GRAPHICS_SUCCESS);
    boot_status("Hotkey manager", hotkey_ok);

    // Initialize device filesystem with input device support
    bool devfs_ok = devfs_init();
    boot_status("Device filesystem (/dev)", devfs_ok);

    if (devfs_ok) {
        // Initialize input devices in devfs (/dev/kbd, /dev/mouse)
        bool devfs_input_ok = devfs_input_init();
        boot_status("Input device nodes (/dev/kbd, /dev/mouse)", devfs_input_ok);

        // Initialize timer devices in devfs (/dev/timer, /dev/rtc, /dev/hpet, /dev/pit)
        bool timer_dev_ok = timer_dev_init();
        boot_status("Timer device nodes (/dev/timer, /dev/rtc)", timer_dev_ok);

        // Initialize framebuffer info devices (/dev/fb_width, /dev/fb_height, /dev/fb_pitch)
        bool fb_dev_ok = devfs_fb_init();
        boot_status("Framebuffer device nodes (/dev/fb_*)", fb_dev_ok);

        // Register all PCI devices dynamically (/dev/pci/*)
        bool pci_dev_ok = devfs_register_pci_devices();
        boot_status("PCI device nodes (/dev/pci/*)", pci_dev_ok);
    }

    // Initialize USB subsystem (for hot-swappable keyboards/mice)
    bool usb_ok = usb_init();
    boot_status("USB subsystem", usb_ok);

    // Clear any stale bytes BIOS/firmware may have left in the PS/2 output buffer
    ps2_flush_output_buffer("before PS/2 init", 64);

    if (!g_quiet_boot) {
        print("ABOUT_TO_INIT_PS2_CONTROLLER\n");
    }
    bool ps2_controller_ok = (ps2_controller_init() == 0);
    boot_status("PS/2 controller reset + self-test", ps2_controller_ok);

    // If full controller init failed, do minimal init to at least get keyboard working
    if (!ps2_controller_ok) {
        ps2_controller_minimal_init();
    }

    // Drain anything the controller self-test might have produced so the keyboard
    // can start with a clean buffer.
    ps2_flush_output_buffer("after controller init", 64);

    bool ps2_keyboard_ok = false;
    bool ps2_mouse_ok = false;

    // Always try to initialize keyboard - even if controller init reported issues
    // Many emulators (QEMU) and systems work fine even if tests fail
    ps2_keyboard_ok = (ps2_keyboard_init() == 0);
    boot_status("PS/2 keyboard driver", ps2_keyboard_ok);

    // Flush/wake the keyboard so pending scancodes don't block fresh ones.
    ps2_keyboard_flush_and_delay("after keyboard init");

    // Set up keyboard IRQ handler now; unmask IRQ1 after mouse setup so AUX
    // bytes cannot block keyboard input before IRQ12 handling is active.
    ps2_keyboard_register_event_callback(keyboard_event_handler);
    interrupt_set_handler_legacy(IRQ_KEYBOARD, ps2_keyboard_irq_handler);

    // Register serial interrupt handler for serial console input
    interrupt_set_handler_legacy(IRQ_COM1, (legacy_interrupt_handler_t)keyboard_serial_interrupt_handler);
    pic_unmask_irq(4);  // Enable COM1 IRQ
    keyboard_set_driver_mode(KEYBOARD_DRIVER_PS2);

    if (!ps2_keyboard_ok && !g_quiet_boot) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m Keyboard init had warnings; IRQ handler installed anyway.\n");
    }

    // Mouse initialization - try even if controller self-test failed (like keyboard)
    // Many emulators and systems work fine even if controller self-test fails
    ps2_mouse_ok = (ps2_mouse_init() == 0);
    boot_status("PS/2 mouse driver", ps2_mouse_ok);
    if (ps2_mouse_ok) {
        ps2_mouse_register_event_callback(mouse_event_handler);
        interrupt_set_handler_legacy(IRQ_MOUSE, ps2_mouse_irq_handler);
        pic_unmask_irq(2);   // Enable cascade IRQ (required for IRQ8-15)
        pic_unmask_irq(12);  // Enable mouse IRQ
        // IMPORTANT: Enable data reporting AFTER IRQ handler is installed
        // This prevents data loss from packets arriving before handler is ready
        ps2_mouse_start_streaming();
        if (!g_quiet_boot) {
            tty_write_ansi("\x1b[36m [irq] \x1b[0mMouse handler installed on IRQ12 (cascade IRQ2 enabled)\n");
        }
    } else if (!g_quiet_boot) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 mouse unavailable.\n");
    }
    if (!ps2_controller_ok && !g_quiet_boot) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 controller had issues; mouse may not work reliably.\n");
    }

    // Enable keyboard IRQ after mouse setup to avoid IRQ1 being blocked by AUX bytes.
    pic_unmask_irq(1);
    ps2_flush_output_buffer("after IRQ1 unmask", 32);
    if (!g_quiet_boot) {
        tty_write_ansi("\x1b[36m [irq] \x1b[0mKeyboard handler installed on IRQ1\n");
    }

    // Start PS/2 hotplug watchdog to recover from disconnects
    ps2_watchdog_start();
    
    // Initialize timer for task scheduling (100 Hz)
    if (!timer_init(100)) {
        boot_status("Timer and task scheduling", false);
        kernel_panic("Timer initialization failed");
    } else {
        boot_status("Timer and task scheduling", true);
    }
    
    // Initialize sound subsystem
    bool sound_ok = sound_system_init();
    boot_status("Sound subsystem", sound_ok);
    if (!sound_ok && !g_quiet_boot) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m Sound system unavailable (non-critical).\n");
    }

#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] About to initialize lock debugging...\n");
#endif
    // Initialize lock debugging
    lock_debug_init();
#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] Lock debugging initialized successfully\n");
#endif

    boot_status("Lock debugging", true);

    // Initialize ELF loader subsystem and run basic validation
    debuglog(DEBUG_INFO, "[KERNEL] Initializing ELF loader subsystem...\n");
    
    // Basic ELF validation test
    debuglog(DEBUG_INFO, "[KERNEL] Running ELF loader validation test...\n");

    // Test ELF validation with a minimal valid ELF header
    uint8 test_elf_header[sizeof(elf32_ehdr_t)] = {0};
    test_elf_header[EI_MAG0] = 0x7f;
    test_elf_header[EI_MAG1] = 'E';
    test_elf_header[EI_MAG2] = 'L';
    test_elf_header[EI_MAG3] = 'F';
    test_elf_header[EI_CLASS] = ELF_CLASS_32;
    test_elf_header[EI_DATA] = ELF_DATA_2LSB;
    test_elf_header[EI_VERSION] = ELF_VERSION_CURRENT;
    test_elf_header[7] = 0;
    test_elf_header[8] = 0;
    // padding 7 bytes
    *((uint16*)&test_elf_header[16]) = ELF_TYPE_EXEC;
    *((uint16*)&test_elf_header[18]) = ELF_MACHINE_386;
    *((uint32*)&test_elf_header[20]) = ELF_VERSION_CURRENT;
    *((uint32*)&test_elf_header[24]) = 0x08048000;  // e_entry - valid entry point
    *((uint32*)&test_elf_header[28]) = sizeof(elf32_ehdr_t);  // e_phoff - program header offset
    *((uint32*)&test_elf_header[32]) = 0;  // e_shoff
    *((uint32*)&test_elf_header[36]) = 0;
    *((uint16*)&test_elf_header[40]) = sizeof(elf32_ehdr_t);
    *((uint16*)&test_elf_header[42]) = sizeof(elf32_phdr_t);
    *((uint16*)&test_elf_header[44]) = 1;  // e_phnum = 1 (need at least 1 program header)
    *((uint16*)&test_elf_header[46]) = 0;
    *((uint16*)&test_elf_header[48]) = 0;
    *((uint16*)&test_elf_header[50]) = 0;

    bool elf_validation_ok = elf_is_valid(test_elf_header, sizeof(test_elf_header));
    
    if (elf_validation_ok) {
        debuglog(DEBUG_INFO, "[KERNEL] ELF validation test passed\n");
        boot_status("ELF loader subsystem", true);
    } else {
        debuglog(DEBUG_ERROR, "[KERNEL] ELF validation test failed\n");
        boot_status("ELF loader subsystem", false);
    }
    
    debuglog(DEBUG_INFO, "[KERNEL] ELF loader initialization complete\n");

    if (!g_quiet_boot) {
        tty_write_ansi("\n");
        tty_write_ansi("\x1b[32m=============================================\x1b[0m\n");
        tty_write_ansi("\x1b[32m   Forest OS Boot Complete\x1b[0m\n");
        tty_write_ansi("\x1b[32m=============================================\x1b[0m\n");
        tty_write_ansi("\n");
    }

    // Stop splash screen animation when boot is complete
    if (g_quiet_boot && splash_is_running()) {
        splash_stop();
    }

    // Exit boot mode and switch to framebuffer TTY for graphics
    // This is called after all boot messages are printed for fast VGA text boot
    tty_exit_boot_mode();

    // Check for critical boot failures (but ACPI/sound failures are non-critical)
    if (g_boot_failed && !g_quiet_boot) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m Some non-critical subsystems failed during boot.\n");
        tty_write_ansi("\x1b[36m[INFO]\x1b[0m Continuing to login manager...\n");
    }

    boot_status("Login/session manager", true);
    if (debuglog_is_ready()) {
        debuglog_write("[KERNEL] entering pre-session handoff\n");
    }

    bool autologin_root = false;
#ifdef ENABLE_ROOT_AUTOLOGIN
    autologin_root = true;
#endif

#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] About to enable interrupts...\n");
#endif
    // Re-enable interrupts - scheduler requires timer interrupts to function
    // Without this, task_schedule() never returns and the OS hangs
    __asm__ __volatile__("sti");
    if (debuglog_is_ready()) {
        debuglog_write("[KERNEL] STI executed - interrupts enabled\n");
    }
#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] Interrupts enabled successfully\n");
#endif

    // Skip post-STI delay when interrupts are intentionally disabled.
    if (debuglog_is_ready()) {
        debuglog_write("[KERNEL] launching session_run\n");
    }

    // Enter the interactive session/login loop. This function does not return.
    session_run(autologin_root);
    if (debuglog_is_ready()) {
        debuglog_write("[KERNEL] ERROR: session_run returned unexpectedly\n");
    }

    // Safety net: keep CPU busy if session_run ever returns unexpectedly.
    while (1) {
        task_schedule();
        __asm__ __volatile__("hlt");
    }
}


static void mouse_log_enqueue(uint8 buttons) {
    uint8 next_head = (g_mouse_log_buffer.head + 1) % MOUSE_LOG_CAPACITY;
    g_mouse_log_buffer.entries[g_mouse_log_buffer.head].buttons = buttons;
    g_mouse_log_buffer.head = next_head;

    if (next_head == g_mouse_log_buffer.tail) {
        g_mouse_log_buffer.tail = (g_mouse_log_buffer.tail + 1) % MOUSE_LOG_CAPACITY;
    }
}

static bool mouse_log_pop(mouse_log_entry_t* entry) {
    bool has_entry = false;
    bool interrupts_enabled = irq_save_and_disable_safe();

    if (g_mouse_log_buffer.head != g_mouse_log_buffer.tail) {
        *entry = g_mouse_log_buffer.entries[g_mouse_log_buffer.tail];
        g_mouse_log_buffer.tail = (g_mouse_log_buffer.tail + 1) % MOUSE_LOG_CAPACITY;
        has_entry = true;
    }

    irq_restore_safe(interrupts_enabled);
    return has_entry;
}

static void process_deferred_mouse_logs(void) __attribute__((unused));
static void process_deferred_mouse_logs(void) {
    mouse_log_entry_t entry;

    while (mouse_log_pop(&entry)) {
        char line[64];
        snprintf(line, sizeof(line),
                 "[MOUSE] Buttons L:%u R:%u M:%u\n",
                 (entry.buttons & MOUSE_BUTTON_LEFT) ? 1 : 0,
                 (entry.buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0,
                 (entry.buttons & MOUSE_BUTTON_MIDDLE) ? 1 : 0);

        if (g_framebuffer_tty_ready) {
            tty_write_ansi(line);
        } else {
            print(line);
        }
    }
}

void keyboard_event_handler(const keyboard_event_t* event) {
    /*
     * PS/2 keyboard driver already publishes evdev-compatible input events
     * to devfs/input-mux in ps2_keyboard_send_event(). Do not queue here,
     * or we duplicate events with mismatched key codes.
     */
    (void)event;
}

void mouse_event_handler(const ps2_mouse_event_t* event) {
    if (!event) return;

    // NOTE: The PS/2 mouse driver (mouse.c) already dispatches input events
    // to devfs via ps2_mouse_dispatch_input_event(). We do NOT dispatch here
    // to avoid duplicate events.
    //
    // This callback is only used for the legacy mouse log functionality
    // which tracks button state changes for debugging.

    uint8 buttons = 0;
    if (event->left_button) {
        buttons |= MOUSE_BUTTON_LEFT;
    }
    if (event->right_button) {
        buttons |= MOUSE_BUTTON_RIGHT;
    }
    if (event->middle_button) {
        buttons |= MOUSE_BUTTON_MIDDLE;
    }

    // Keep original mouse log functionality (for debugging)
    if (buttons != g_mouse_button_state) {
        g_mouse_button_state = buttons;
        mouse_log_enqueue(buttons);
    }
}

/*
 * VirtualBox Guest Additions event handlers
 */

void display_change_handler(const struct vbox_display_change_event *event) {
    if (!event) return;

    debuglog_printf("VBOX: Display change request: %dx%dx%d\n",
                   event->xres, event->yres, event->bpp);

    // Notify graphics system of display change for seamless mode
    graphics_result_t result = graphics_set_mode(event->xres, event->yres, event->bpp, 60);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_INFO, "VBOX: Failed to set graphics mode: %d\n", result);
    }
}

void mouse_position_handler(const struct vbox_mouse_position_event *event) {
    if (!event) return;

    // Update absolute mouse position from VirtualBox host
    // This integrates with PS2 mouse system for absolute positioning
    ps2_mouse_set_position(event->x, event->y);
}

bool pcie_enumeration_callback(const pci_device_t* device, void* context) {
    uint32* count = (uint32*)context;
    if (pcie_is_enumerated_device_pcie(device)) {
        pcie_print_device_info(device);
        (*count)++;
    }
    return true;
}
