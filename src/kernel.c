#include "include/kb.h"
#include "include/cpu_ops.h"
#include "include/interrupt.h"
#include "include/timer.h"
#include "include/ps2_controller.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_mouse.h"
#include "include/io_ports.h"
#include "include/util.h"
#include "include/screen.h"

#include "include/memory_safe.h"
#include "include/memory.h"
#include "include/memory_region_manager.h"
#include "include/page_fault_recovery.h"
#include "include/acpi.h"
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
#include "include/libc/stdio.h"
#include "include/lock_debug.h"
#include "include/graphics_init.h"
#include "include/graphics/graphics_manager.h"
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
#include "include/dks.h"
#include "include/session.h"
#include "include/sound.h"

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

void kmain(uint32 magic, uint32 mbi_addr);
void keyboard_event_handler(const keyboard_event_t* event);
void mouse_event_handler(const ps2_mouse_event_t* event);

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
static bool g_graphics_ready = false;
static bool g_framebuffer_tty_ready = false;
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
            graphics_clear_screen(COLOR_BLACK);

            //primative

            graphics_rect_t main_rect = {100, 100, 600, 200};
            graphics_draw_rect(&main_rect, COLOR_GREEN, true);

            graphics_rect_t inner_rect = {150, 120, 500, 160};
            graphics_draw_rect(&inner_rect, COLOR_WHITE, true);

        } else {
            // Fallback for silent mode if graphics isn't ready
            print_colored("Forest OS (Silent Mode)\n", TEXT_ATTR_GREEN, TEXT_ATTR_BLACK);
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
        graphics_result_t graphics_init_result = graphics_init();
        g_graphics_ready = (graphics_init_result == GRAPHICS_SUCCESS);
        boot_status("Graphics subsystem", g_graphics_ready);
        if (!g_graphics_ready) {
            print_colored("ERROR: Graphics subsystem required for modern TTY\n",
                          TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
            print_colored("System will continue with legacy console only\n",
                          TEXT_ATTR_YELLOW, TEXT_ATTR_BLACK);
            return;
        }
    }

    if (!g_framebuffer_tty_ready) {
        bool tty_success = tty_init();
        if (tty_success) {
            tty_clear();
            if (!g_silent_boot) {
                boot_banner();
            }
            boot_status("Framebuffer TTY with truecolor support", true);
            g_framebuffer_tty_ready = true;
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

void startk(uint32 magic, uint32 mbi_addr) {
    cpu_disable_interrupts();
    gdt_init((uint32)&_stack_top);
    // Early interrupt setup (enables safe interrupt functions)
    interrupt_early_init();
    debuglog_init();

    init_system_init();

    // Note: Console initialization moved to after graphics init for framebuffer-only TTY
    
    // Complete interrupt system setup
    interrupt_full_init();
    
    // Initialize syscalls (now uses new interrupt system)
    syscall_init();
    kmain(magic, mbi_addr);
}

void kmain(uint32 magic, uint32 mbi_addr) {
    // Initialize early text mode console first for debugging
    if (!g_silent_boot) {
        clearScreen();
        print_colored("Forest OS kernel v1.0 - Early Boot\n", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
        print_colored("Text mode console active\n\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    }

    // Parse kernel command line for bootmode=silent
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mbi_addr != 0) {
        multiboot_info_t* mbi = (multiboot_info_t*)mbi_addr;
        if (mbi->cmdline) {
            char* cmdline = (char*)mbi->cmdline;
            if (strstr(cmdline, "bootmode=silent")) {
                g_silent_boot = true;
            }
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
            if (tag->type == MULTIBOOT2_TAG_CMDLINE) {
                multiboot2_tag_string_t* cmdline_tag = (multiboot2_tag_string_t*)tag;
                if (strstr(cmdline_tag->string, "bootmode=silent")) {
                    g_silent_boot = true;
                }
            }
            uint32 advance = (tag->size + 7) & ~7;
            cursor += advance;
        }
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

    // Add some example memory regions (in real system, this would come from multiboot/ACPI)
    bitmap_pmm_add_memory_region(0x100000, 32 * 1024 * 1024, MEMORY_TYPE_AVAILABLE); // 32MB starting at 1MB
    bitmap_pmm_add_memory_region(0x0, 0x100000, MEMORY_TYPE_RESERVED); // First 1MB reserved

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
    boot_status("Bitmap PMM tests", bitmap_pmm_test_result == 0);
    
    // Initialize memory protection systems
    tlb_manager_init();
    boot_status("TLB management", true);
    
    supervisor_memory_protection_init();
    boot_status("SMEP/SMAP hardware protection", true);
    
    stack_protection_init();
    boot_status("Stack overflow protection", true);
    
    ssp_init();
    boot_status("Stack smashing protection", true);
    
    // Run SSP functionality tests (safe mode)
    int ssp_test_result = ssp_run_tests();
    boot_status("SSP functionality tests", ssp_test_result == 0);
    
    vmm_config_t secure_vmm_cfg = {
        .corruption_detection_enabled = true,
        .access_tracking_enabled = true,
        .guard_pages_enabled = true,
        .aslr_enabled = false,
        .dep_enabled = true,
        .debug_mode_enabled = false,
        .kernel_heap_start = memory_get_kernel_heap_start(),
        .kernel_heap_size = 32 * 1024 * 1024,
        .user_space_start = MEMORY_USER_START,
        .user_space_size = 512 * 1024 * 1024
    };
    secure_vmm_init(&secure_vmm_cfg);
    boot_status("Secure virtual memory manager", true);
    
    // Initialize comprehensive memory corruption detection
    memory_corruption_init();
    memory_corruption_enable();
    boot_status("Memory corruption detection", true);
    
    // Run memory corruption detection tests
    int corruption_test_result = memory_corruption_run_tests();
    boot_require("Memory corruption tests",
                 corruption_test_result == 0,
                 "Memory corruption self-test failed");
    
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
    
    // Run enhanced heap tests
    int enhanced_heap_test_result = enhanced_heap_run_tests();
    boot_require("Enhanced heap tests",
                 enhanced_heap_test_result == 0,
                 "Enhanced heap self-test failed");
    
    tasks_init(); // Initialize task management

    bool acpi_ok = acpi_init();
    boot_status("ACPI discovery", acpi_ok);
    bool acpi_pm_ok = acpi_ok && uacpi_init();
    boot_status("ACPI power management", acpi_pm_ok);

    bool pci_ok = pci_init();
    boot_status("PCI/PCIe configuration", pci_ok);

    bool net_ok = driver_core_ok && net_init();
    boot_status("Network core", net_ok);

    bool initrd_ok = ramdisk_init(magic, mbi_addr);
    boot_require("Initrd presence + parsing", initrd_ok, "Initrd missing");
    
    bool vfs_ok = initrd_ok && vfs_init();
    boot_require("Virtual filesystem mount", vfs_ok, "VFS failed to mount");

    // Clear any stale bytes BIOS/firmware may have left in the PS/2 output buffer
    ps2_flush_output_buffer("before PS/2 init", 64);

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

    // ALWAYS set up keyboard IRQ handler and unmask IRQ1
    // This ensures keyboard works even if initialization had warnings
    ps2_keyboard_register_event_callback(keyboard_event_handler);
    interrupt_set_handler_legacy(IRQ_KEYBOARD, ps2_keyboard_irq_handler);
    pic_unmask_irq(1);  // Enable keyboard IRQ
    // Read any latched output so the first interrupt doesn't get stuck behind firmware data
    ps2_flush_output_buffer("after IRQ1 unmask", 32);
    keyboard_set_driver_mode(KEYBOARD_DRIVER_PS2);
    tty_write_ansi("\x1b[36m [irq] \x1b[0mKeyboard handler installed on IRQ1\n");

    if (!ps2_keyboard_ok) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m Keyboard init had warnings; IRQ handler installed anyway.\n");
    }

    // Mouse initialization - only if controller is working
    if (ps2_controller_ok) {
        ps2_mouse_ok = (ps2_mouse_init() == 0);
        boot_status("PS/2 mouse driver", ps2_mouse_ok);
        if (ps2_mouse_ok) {
            ps2_mouse_register_event_callback(mouse_event_handler);
            interrupt_set_handler_legacy(IRQ_MOUSE, ps2_mouse_irq_handler);
            pic_unmask_irq(12);  // Enable mouse IRQ
        } else {
            tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 mouse unavailable.\n");
        }
    } else {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 controller had issues; mouse disabled.\n");
        boot_status("PS/2 mouse driver", false);
    }
    
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
    if (!sound_ok) {
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

    tty_write_ansi("\n");
    tty_write_ansi("\x1b[32m=============================================\x1b[0m\n");
    tty_write_ansi("\x1b[32m   Forest OS Boot Complete\x1b[0m\n");
    tty_write_ansi("\x1b[32m=============================================\x1b[0m\n");
    tty_write_ansi("\n");

    // Check for critical boot failures (but ACPI/sound failures are non-critical)
    if (g_boot_failed) {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m Some non-critical subsystems failed during boot.\n");
        tty_write_ansi("\x1b[36m[INFO]\x1b[0m Continuing to login manager...\n");
    }

    boot_status("Login/session manager", true);

    bool autologin_root = false;
#ifdef ENABLE_ROOT_AUTOLOGIN
    autologin_root = true;
#endif

#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] About to enable interrupts...\n");
#endif
    // Enable interrupts before entering the session loop
    irq_enable_safe();
#if CONFIG_DEBUG_BOOT
    KBOOT_DEBUG("[KERNEL] Interrupts enabled successfully\n");
#endif

    // Brief delay for interrupt system to stabilize
    for (volatile int i = 0; i < 50000; i++) { /* delay */ }

    // Enter the interactive session/login loop. This function does not return.
    session_run(autologin_root);

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
    (void)event;
    // Event hook kept for future extensions; input is handled via readStr().
}

void mouse_event_handler(const ps2_mouse_event_t* event) {
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

    if (buttons != g_mouse_button_state) {
        g_mouse_button_state = buttons;
        mouse_log_enqueue(buttons);
    }
}
