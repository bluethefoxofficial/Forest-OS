#include "include/kb.h"
#include "include/cpu_ops.h"
#include "include/interrupt.h"  // New unified interrupt management system
#include "include/timer.h"      // Timer management
#include "include/ps2_controller.h"
#include "include/ps2_keyboard.h"
#include "include/ps2_mouse.h"
#include "include/util.h"
#include "include/screen.h"

#include "include/memory_safe.h"
#include "include/memory_region_manager.h"
#include "include/page_fault_recovery.h"
#include "include/acpi.h"
#include "include/multiboot.h"
#include "include/panic.h"
#include "include/shell_loader.h"
#include "include/dks.h"
#include "include/ramdisk.h"
#include "include/vfs.h"
#include "include/task.h" // Added for task management
#include "include/syscall.h"
#include "include/hardware.h"
#include "include/string.h"
#include "include/pci.h"
#include "include/sound.h"
#include "include/driver.h"
#include "include/net.h"
#include "include/debuglog.h"
#include "include/gdt.h"
#include "include/libc/stdio.h"
#include "include/sync_test.h"
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
void keyboard_event_handler(keyboard_event_t* event);
void mouse_event_handler(ps2_mouse_event_t* event);

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

static bool g_graphics_ready = false;
static bool g_framebuffer_tty_ready = false;

static void boot_banner(void) {
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
    static uint32 timestamp_counter = 3000;  // Start after initial messages
    boot_log_event(label, ok);
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
            boot_banner();
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
    clearScreen();
    print_colored("Forest OS kernel v1.0 - Early Boot\n", TEXT_ATTR_LIGHT_GREEN, TEXT_ATTR_BLACK);
    print_colored("Text mode console active\n\n", TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK);
    
    // We'll initialize graphics much later in the boot process
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
    
    // Run SSP functionality tests
    int ssp_test_result = ssp_run_tests();
    boot_status("SSP functionality tests", ssp_test_result == 0);
    
    vmm_config_t secure_vmm_cfg = {
        .corruption_detection_enabled = true,
        .access_tracking_enabled = true,
        .guard_pages_enabled = true,
        .aslr_enabled = false,
        .dep_enabled = true,
        .debug_mode_enabled = false,
        .kernel_heap_start = MEMORY_KERNEL_HEAP_START,
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
    boot_status("Memory corruption tests", corruption_test_result == 0);
    
    // Initialize enhanced heap allocator
    enhanced_heap_config_t heap_config = {
        .corruption_detection_enabled = true,
        .guard_pages_enabled = false,
        .metadata_protection_enabled = true,
        .fragmentation_mitigation_enabled = true,
        .debug_mode_enabled = false,
        .max_heap_size = 16 * 1024 * 1024,
        .expansion_increment = 64 * 1024
    };
    
    enhanced_heap_init(&heap_config);
    boot_status("Enhanced heap allocator", true);
    
    // Run enhanced heap tests
    int enhanced_heap_test_result = enhanced_heap_run_tests();
    boot_status("Enhanced heap tests", enhanced_heap_test_result == 0);
    
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

    bool ps2_controller_ok = (ps2_controller_init() == 0);
    boot_status("PS/2 controller reset + self-test", ps2_controller_ok);
    
    bool ps2_keyboard_ok = false;
    bool ps2_mouse_ok = false;
    
    if (ps2_controller_ok) {
        ps2_keyboard_ok = (ps2_keyboard_init() == 0);
        boot_status("PS/2 keyboard driver", ps2_keyboard_ok);
        
        if (ps2_keyboard_ok) {
            ps2_keyboard_register_event_callback(keyboard_event_handler);
            interrupt_set_handler(IRQ_KEYBOARD, ps2_keyboard_irq_handler);
            pic_unmask_irq(1);  // Enable keyboard IRQ
            keyboard_set_driver_mode(KEYBOARD_DRIVER_PS2);
            tty_write_ansi("\x1b[36m [irq] \x1b[0mKeyboard handler installed on IRQ1\n");
        } else {
            tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 keyboard not detected; falling back to legacy polling driver.\n");
        }

        ps2_mouse_ok = (ps2_mouse_init() == 0);
        boot_status("PS/2 mouse driver", ps2_mouse_ok);
        if (ps2_mouse_ok) {
            ps2_mouse_register_event_callback(mouse_event_handler);
            interrupt_set_handler(IRQ_MOUSE, ps2_mouse_irq_handler);
            pic_unmask_irq(12);  // Enable mouse IRQ
        } else {
            tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 mouse unavailable.\n");
        }
    } else {
        tty_write_ansi("\x1b[33m[WARN]\x1b[0m PS/2 controller not responding; using legacy keyboard driver only.\n");
        boot_status("PS/2 keyboard driver", false);
        boot_status("PS/2 mouse driver", false);
    }
    
    // Initialize timer for task scheduling (100 Hz)
    if (!timer_init(100)) {
        boot_status("Timer and task scheduling", false);
        kernel_panic("Timer initialization failed");
    } else {
        boot_status("Timer and task scheduling", true);
    }
    
    if (!sound_system_init()) {
        boot_status("Sound subsystem", false);
    } else {
        boot_status("Sound subsystem", true);
    }

    // Initialize lock debugging
    lock_debug_init();
    
    // TODO: Run synchronization tests after task system is fully initialized
    // sync_test_run_all();

    // Enable interrupts
    irq_enable_safe();

    // Demonstrate enhanced TTY capabilities
    tty_write_ansi("\x1b[36m[INFO]\x1b[0m Demonstrating enhanced TTY with full ANSI support:\n\n");
    
    // Test basic 16 colors
    tty_write_ansi("Standard 16 colors: ");
    for (int i = 30; i <= 37; i++) {
        char color_test[32];
        sprintf(color_test, "\x1b[%dm█\x1b[0m", i);
        tty_write_ansi(color_test);
    }
    for (int i = 90; i <= 97; i++) {
        char color_test[32];
        sprintf(color_test, "\x1b[%dm█\x1b[0m", i);
        tty_write_ansi(color_test);
    }
    tty_write_ansi("\n");
    
    // Test 256-color mode
    tty_write_ansi("256-color palette sample: ");
    for (int i = 16; i < 32; i++) {
        char color_test[32];
        sprintf(color_test, "\x1b[38;5;%dm█\x1b[0m", i);
        tty_write_ansi(color_test);
    }
    tty_write_ansi("\n");
    
    // Test truecolor
    tty_write_ansi("Truecolor RGB gradient: ");
    for (int r = 0; r < 256; r += 32) {
        char color_test[32];
        sprintf(color_test, "\x1b[38;2;%d;0;%dm█\x1b[0m", r, 255-r);
        tty_write_ansi(color_test);
    }
    tty_write_ansi("\n");
    
    // Test text attributes
    tty_write_ansi("Text attributes: ");
    tty_write_ansi("\x1b[1mBold\x1b[22m ");
    tty_write_ansi("\x1b[2mFaint\x1b[22m ");
    tty_write_ansi("\x1b[3mItalic\x1b[23m ");
    tty_write_ansi("\x1b[4mUnderline\x1b[24m ");
    tty_write_ansi("\x1b[9mStrikethrough\x1b[29m ");
    tty_write_ansi("\x1b[7mInverse\x1b[27m");
    tty_write_ansi("\n");
    
    // Test cursor control
    tty_write_ansi("Cursor control: ");
    tty_write_ansi("Moving");
    tty_write_ansi("\x1b[3D\x1b[1C←→");
    tty_write_ansi("\x1b[2C test\n");
    
    tty_write_ansi("\n\x1b[32mEnhanced TTY demonstration complete!\x1b[0m\n");
    tty_write_ansi("\x1b[36m[INFO]\x1b[0m System ready with enhanced terminal capabilities.\n");
    tty_write_ansi("\x1b[36m[INFO]\x1b[0m CPU will halt to preserve power.\n");
    
    // Just halt - this tests that our exception handling works when no errors occur
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void keyboard_event_handler(keyboard_event_t* event) {
    (void)event;
    // Event hook kept for future extensions; input is handled via readStr().
}

void mouse_event_handler(ps2_mouse_event_t* event) {
    static bool prev_left = false;
    static bool prev_right = false;
    static bool prev_middle = false;

    bool changed = (event->left_button != prev_left) ||
                   (event->right_button != prev_right) ||
                   (event->middle_button != prev_middle);

    if (changed) {
        tty_write_ansi("[MOUSE] Buttons L:");
        tty_write_ansi(event->left_button ? "1" : "0");
        tty_write_ansi(" R:");
        tty_write_ansi(event->right_button ? "1" : "0");
        tty_write_ansi(" M:");
        tty_write_ansi(event->middle_button ? "1" : "0");
        tty_write_ansi("\n");
        prev_left = event->left_button;
        prev_right = event->right_button;
        prev_middle = event->middle_button;
    }
}
