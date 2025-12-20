#include "include/power.h"

#include "include/acpi.h"
#include "include/driver.h"
#include "include/interrupt.h"
#include "include/screen.h"
#include "include/sound.h"
#include "include/system.h"
#include "include/task.h"
#include "include/timer.h"
#include "include/util.h"
#include "include/debuglog.h"

static void power_log(const char* msg) {
    if (!msg) {
        return;
    }
    print("[POWER] ");
    print(msg);
    print("\n");
    debuglog_write("[POWER] ");
    debuglog_write(msg);
    debuglog_write("\n");
}

static void cleanup_subsystems(void) {
    power_log("Stopping scheduler and timer");
    timer_shutdown();

    power_log("Terminating user tasks");
    task_shutdown_all();

    power_log("Shutting down active drivers");
    sound_shutdown();
    driver_shutdown_all();
}

static void fallback_shutdown_ports(void) {
    outportw(0x604, 0x2000);
    outportw(0xB004, 0x2000);
    outportw(0x4004, 0x3400);
    outportw(0x600, 0x34);
}

static void fallback_shutdown_halt(void) {
    power_log("System halted - safe to power off manually");
    while (1) {
        __asm__ __volatile__("cli; hlt");
    }
}

static void fallback_keyboard_reset(void) {
    const int timeout = 100000;
    for (int i = 0; i < timeout; i++) {
        if ((inportb(0x64) & 0x02) == 0) {
            break;
        }
    }
    outportb(0x64, 0xFE);
}

static void fallback_triple_fault(void) {
    struct {
        uint16 limit;
        uint32 base;
    } __attribute__((packed)) null_idt = {0, 0};

    __asm__ __volatile__("cli");
    __asm__ __volatile__("lidt %0" : : "m"(null_idt));
    __asm__ __volatile__("int $3");
}

static void fallback_shutdown(void) {
    fallback_shutdown_ports();
    fallback_shutdown_halt();
}

static void fallback_reboot(void) {
    fallback_keyboard_reset();
    fallback_triple_fault();
}

bool power_request(power_action_t action) {
    if (action == POWER_ACTION_REBOOT) {
        power_log("Reboot requested");
    } else {
        power_log("Shutdown requested");
    }

    irq_disable_safe();
    cleanup_subsystems();

    if (action == POWER_ACTION_SHUTDOWN) {
        if (acpi_shutdown()) {
            fallback_shutdown_halt();
        }
        power_log("ACPI shutdown failed, trying legacy ports");
        fallback_shutdown();
    } else {
        if (acpi_reboot()) {
            fallback_triple_fault();
        }
        power_log("ACPI reboot failed, falling back to keyboard controller");
        fallback_reboot();
    }

    return false;
}

bool power_shutdown(void) {
    return power_request(POWER_ACTION_SHUTDOWN);
}

bool power_reboot(void) {
    return power_request(POWER_ACTION_REBOOT);
}
