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
    print_colored("Forest OS ", COLOR_OK, 0x00);
    print_colored("kernel v1.0\n", COLOR_LABEL, 0x00);
    print_colored("=====================================\n", 0x08, 0x00);
    print_colored("Booting with PS/2 input, VFS, and initrd support.\n\n", 0x0F, 0x00);
}

static void boot_status(const char* label, bool ok) {
    print_colored("[", 0x08, 0x00);
    print_colored(ok ? " OK " : "FAIL", ok ? COLOR_OK : COLOR_FAIL, 0x00);
    print_colored("] ", 0x08, 0x00);
    print((string)label);
    print("\n");
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
    
    tasks_init(); // Initialize task management

    bool acpi_ok = acpi_init();
    boot_status("ACPI discovery", acpi_ok);

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

    // Enable interrupts
    irq_enable_safe();

    if (!shell_launch_embedded()) {
        print_colored("[WARN] ", COLOR_WARN, 0x00);
        print("Userspace shell unavailable; entering Direct Kernel Shell.\n");
        dks_run();
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
