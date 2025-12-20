#include "include/dks.h"
#include "include/shell_loader.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/kb.h"
#include "include/memory.h"
#include "include/panic.h"
#include "include/hardware.h"
#include "include/power.h"

static void dks_print_help(void) {
    print("Commands:\n");
    print("  help    - Show this message\n");
    print("  mem     - Dump PMM statistics\n");
    print("  cls     - Clear the screen\n");
    print("  tui     - Demo enhanced TUI features\n");
    print("  cpuid   - Show detected CPU information\n");
    print("  panic   - Trigger panic to demo debugging tools\n");
    print("  shell   - Retry launching the userspace shell\n");
    print("  halt    - Halt the CPU (requires reset/power cycle)\n");
    print("  shutdown- Power off via ACPI\n");
    print("  reboot  - Reboot the system via ACPI\n");
}

static void dks_format_uint(char* buffer, uint32 value) {
    if (!buffer) {
        return;
    }
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    char temp[16];
    int idx = 0;
    while (value > 0 && idx < 16) {
        temp[idx++] = '0' + (value % 10);
        value /= 10;
    }
    for (int i = 0; i < idx; i++) {
        buffer[i] = temp[idx - 1 - i];
    }
    buffer[idx] = '\0';
}

static void dks_dump_memory(void) {
    memory_stats_t stats = memory_get_stats();
    print("Total memory: ");
    print_dec(stats.total_memory_kb);
    print(" KB\n");

    print(" Usable: ");
    print_dec(stats.usable_memory_kb);
    print(" KB  Heap size: ");
    print_dec(stats.heap_size_kb);
    print(" KB\n");

    print("Frames used: ");
    print_dec(stats.used_frames);
    print("  Free: ");
    print_dec(stats.free_frames);
    print("  Total: ");
    print_dec(stats.total_frames);
    print("\n");

    print("Kernel frames: ");
    print_dec(stats.kernel_frames);
    print("  Heap used: ");
    print_dec(stats.heap_used_kb);
    print(" KB\n");
}

static void dks_tui_demo(void) {
    clearScreen();
    
    // Demo title
    tui_draw_status_bar(0, "Forest OS - Enhanced TUI Demonstration", "Press any key to continue", 0x0F, 0x01);
    
    // Demo windows with different styles
    tui_draw_window(2, 2, 35, 8, "System Information", 0x0F, 0x02);
    tui_print_at(4, 4, "Forest OS v1.0", 0x0F, 0x02);
    tui_print_at(4, 5, "Architecture: i386", 0x0F, 0x02);
    tui_print_at(4, 6, "Mode: Protected Mode", 0x0F, 0x02);
    
    // Memory progress bars
    memory_stats_t stats = memory_get_stats();
    uint32 total_frames = stats.total_frames ? stats.total_frames : 1;
    uint32 memory_usage = (stats.used_frames * 100) / total_frames;
    tui_draw_progress_bar(4, 7, 25, memory_usage, 100, 0x0F, 0x02);
    tui_print_at(4, 8, "Memory Usage", 0x0E, 0x02);
    
    // Demo different border styles
    tui_draw_border(40, 2, 35, 5, 1, 0x0E, 0x06); // Double border
    tui_center_text(41, 3, 33, "Double Border Style", 0x0F, 0x06);
    
    tui_draw_border(40, 8, 35, 5, 2, 0x0C, 0x04); // Thick border
    tui_center_text(41, 9, 33, "Thick Border Style", 0x0F, 0x04);
    
    // Demo shaded boxes
    tui_draw_shaded_box(2, 12, 15, 6, 1, 0x08, 0x00);
    tui_draw_shaded_box(20, 12, 15, 6, 2, 0x08, 0x00);
    tui_draw_shaded_box(38, 12, 15, 6, 3, 0x08, 0x00);
    
    // Demo menu items
    tui_draw_menu_item(2, 20, 20, "Selected Item", true, 0x0F, 0x03);
    tui_draw_menu_item(2, 21, 20, "Normal Item", false, 0x0F, 0x03);
    tui_draw_menu_item(2, 22, 20, "Another Item", false, 0x0F, 0x03);
    
    // Sample graph data
    uint32 sample_data[] = {10, 25, 15, 30, 22, 35, 28, 40, 33, 20};
    tui_draw_graph(40, 15, 35, 8, sample_data, 10, "Sample Graph", 0x0F, 0x05);
    
    tui_draw_status_bar(24, "TUI Demo Complete", "Forest OS Enhanced Graphics", 0x0F, 0x09);
    
    // Wait for key press
    readStr();
    clearScreen();
}

static void dks_show_cpuid(void) {
    const cpuid_info_t* info = hardware_get_cpuid_info();
    print("[CPUID] Hardware detection:\n");
    print("  Status: ");
    print(info->cpuid_supported ? "supported" : "unsupported");
    print("\n");
    
    print("  Vendor: ");
    print(info->vendor_id);
    print("\n");
    
    print("  Brand:  ");
    print(info->brand_string);
    print("\n");
    
    if (!info->cpuid_supported) {
        print("  Feature list unavailable on this platform.\n");
        return;
    }
    
    char num_buf[32];
    dks_format_uint(num_buf, info->family);
    print("  Family: ");
    print(num_buf);
    print("  Model: ");
    dks_format_uint(num_buf, info->model);
    print(num_buf);
    print("  Stepping: ");
    dks_format_uint(num_buf, info->stepping);
    print(num_buf);
    print("\n");
    
    dks_format_uint(num_buf, info->logical_processor_count);
    print("  Logical processors: ");
    print(num_buf);
    print("\n");
    
    print("  Hypervisor present: ");
    print(info->hypervisor_present ? "yes" : "no");
    print("\n");
    
    print("  Features: ");
    print(hardware_get_feature_summary());
    print("\n");
}

void dks_run(void) {
    print("\n[DKS] Direct Kernel Shell online. Type 'help' for commands.\n");

    while (1) {
        print("dks> ");
        string input = readStr();
        if (!input) {
            print("[DKS] Input error.\n");
            continue;
        }

        if (strcmp(input, "help") == 0) {
            dks_print_help();
        } else if (strcmp(input, "mem") == 0) {
            dks_dump_memory();
        } else if (strcmp(input, "cls") == 0) {
            clearScreen();
        } else if (strcmp(input, "tui") == 0) {
            dks_tui_demo();
        } else if (strcmp(input, "cpuid") == 0) {
            dks_show_cpuid();
        } else if (strcmp(input, "panic") == 0) {
            kernel_panic("User-triggered test panic for debugging demonstration");
        } else if (strcmp(input, "shell") == 0) {
            print("[DKS] Attempting to launch userspace shell again...\n");
            if (shell_launch_embedded()) {
                return; // Userspace shell replaced us.
            }
            print("[DKS] Shell launch failed; still in DKS.\n");
        } else if (strcmp(input, "halt") == 0) {
            print("[DKS] Halting CPU. Reset or power cycle required.\n");
            __asm__ __volatile__("cli; hlt");
        } else if (strcmp(input, "shutdown") == 0) {
            print("[DKS] Initiating ACPI shutdown...\n");
            if (!power_shutdown()) {
                print("[DKS] Shutdown failed; system halted.\n");
            }
        } else if (strcmp(input, "reboot") == 0) {
            print("[DKS] Initiating ACPI reboot...\n");
            if (!power_reboot()) {
                print("[DKS] Reboot failed; system halted.\n");
            }
        } else {
            print("[DKS] Unknown command '");
            print(input);
            print("'. Type 'help'.\n");
        }
    }
}
