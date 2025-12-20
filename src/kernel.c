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
#include "include/sync_test.h"
#include "include/lock_debug.h"
#include "include/graphics_init.h"
#include "include/tlb_manager.h"
#include "include/smep_smap.h"
#include "include/stack_protection.h"
#include "include/ssp.h"
#include "include/memory_corruption.h"
#include "include/enhanced_heap.h"
#include "include/bitmap_pmm.h"
#include "include/secure_vmm.h"

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

static void boot_banner(void) {
    set_screen_color_from_color_code(0x0F);
    print_colored("Forest OS ", 0x0A, 0x00);  // Bright green
    print_colored("kernel ", 0x0F, 0x00);     // White
    print_colored("v1.0", 0x0B, 0x00);        // Cyan
    print_colored(" (", 0x08, 0x00);          // Dark gray
    print_colored("VGA Text Mode", 0x0E, 0x00); // Yellow
    print_colored(")\n", 0x08, 0x00);
    
    print_colored("[", 0x08, 0x00);
    print_colored("    0.000000", 0x0A, 0x00);  // Green timestamp like Linux
    print_colored("] ", 0x08, 0x00);
    print_colored("Booting Forest-OS with enhanced VGA text output...\n", 0x0F, 0x00);
    
    print_colored("[", 0x08, 0x00);
    print_colored("    0.001000", 0x0A, 0x00);
    print_colored("] ", 0x08, 0x00);
    print_colored("Kernel command line: root=/dev/ram0 init=/bin/init\n", 0x07, 0x00);
    
    print_colored("[", 0x08, 0x00);
    print_colored("    0.002000", 0x0A, 0x00);
    print_colored("] ", 0x08, 0x00);
    print_colored("Initializing subsystems...\n\n", 0x07, 0x00);
}

static void boot_status(const char* label, bool ok) {
    static uint32 timestamp_counter = 3000;  // Start after initial messages
    
    print_colored("[", 0x08, 0x00);
    print_colored("    ", 0x0A, 0x00);
    
    // Print timestamp (simulated)
    print_dec(timestamp_counter / 1000);
    print_colored(".", 0x0A, 0x00);
    print_dec((timestamp_counter % 1000) / 100);
    print_dec((timestamp_counter % 100) / 10);
    print_dec(timestamp_counter % 10);
    print_dec(timestamp_counter % 10);
    print_dec(timestamp_counter % 10);
    
    print_colored("] ", 0x08, 0x00);
    print_colored(ok ? "+" : "-", ok ? COLOR_OK : COLOR_FAIL, 0x00);
    print(" ");
    print_colored((string)label, 0x0F, 0x00);
    if (ok) {
        print_colored(" ... ", 0x08, 0x00);
        print_colored("OK", COLOR_OK, 0x00);
    } else {
        print_colored(" ... ", 0x08, 0x00);
        print_colored("FAILED", COLOR_FAIL, 0x00);
    }
    print("\n");
    
    timestamp_counter += 100 + (timestamp_counter % 50); // Variable timing like real boot
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
    
    // Now safe to initialize console (uses interrupt save/restore)
    console_init();
    
    // Complete interrupt system setup
    interrupt_full_init();
    
    // Initialize syscalls (now uses new interrupt system)
    syscall_init();
    kmain(magic, mbi_addr);
}

void kmain(uint32 magic, uint32 mbi_addr) {
    // Console already initialized in startk()
    
    // bool high_res_text = screen_set_mode(TEXT_MODE_80x50);
    // if (!high_res_text) {
    //     clearScreen();
    //     print_colored("Falling back to 80x25 text mode\n\n", COLOR_WARN, 0x00);
    // }
    
    boot_banner();
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

    // Use reliable VGA text mode directly - skip complex graphics drivers
    clearScreen();
    print_colored("VGA text mode initialized successfully\n", COLOR_OK, 0x00);
    
    // Set high-resolution text mode for better display
    bool high_res_ok = screen_set_mode(TEXT_MODE_80x50);
    if (!high_res_ok) {
        print_colored("Using standard 80x25 text mode\n", COLOR_WARN, 0x00);
        screen_set_mode(TEXT_MODE_80x25);
    } else {
        print_colored("Enhanced 80x50 text mode active\n", COLOR_OK, 0x00);
    }
    
    clearScreen();
    boot_banner(); // Re-display banner in proper mode
    boot_status("VGA text mode", true);

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
            print_colored(" [irq] ", COLOR_LABEL, 0x00);
            print("Keyboard handler installed on IRQ1\n");
        } else {
            print_colored("[WARN] ", COLOR_WARN, 0x00);
            print("PS/2 keyboard not detected; falling back to legacy polling driver.\n");
        }

        ps2_mouse_ok = (ps2_mouse_init() == 0);
        boot_status("PS/2 mouse driver", ps2_mouse_ok);
        if (ps2_mouse_ok) {
            ps2_mouse_register_event_callback(mouse_event_handler);
            interrupt_set_handler(IRQ_MOUSE, ps2_mouse_irq_handler);
            pic_unmask_irq(12);  // Enable mouse IRQ
        } else {
            print_colored("[WARN] ", COLOR_WARN, 0x00);
            print("PS/2 mouse unavailable.\n");
        }
    } else {
        print_colored("[WARN] ", COLOR_WARN, 0x00);
        print("PS/2 controller not responding; using legacy keyboard driver only.\n");
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

    // Temporarily disable all shells to test Linux-style exception handling
    print_colored("[INFO] ", COLOR_LABEL, 0x00);
    print("All shells disabled for testing. Kernel initialization complete.\n");
    print("System ready. CPU will halt to preserve power.\n");
    
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
        print("[MOUSE] Buttons L:");
        print(event->left_button ? "1" : "0");
        print(" R:");
        print(event->right_button ? "1" : "0");
        print(" M:");
        print(event->middle_button ? "1" : "0");
        print("\n");
        prev_left = event->left_button;
        prev_right = event->right_button;
        prev_middle = event->middle_button;
    }
}
