#include "uacpi/kernel_api.h"
#include "uacpi/uacpi.h"
#include "include/debuglog.h"
#include "include/acpi.h"
#include "include/memory.h"
#include "include/timer.h"
#include "include/io_ports.h"
#include "include/pci.h"
#include "include/task.h"
#include "include/spinlock.h"
#include "include/semaphore.h"
#include <stdint.h>


uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out_rsdp_address) {
    const acpi_rsdp_t* rsdp = acpi_find_rsdp();
    if (!rsdp) {
        return UACPI_STATUS_NOT_FOUND;
    }
    // uACPI wants a physical address. The pointer from acpi_find_rsdp is a virtual
    // address in the identity-mapped region, so it's the same value.
    *out_rsdp_address = (uacpi_phys_addr)rsdp;
    return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
    // HACK: Assume all ACPI tables are in the identity-mapped low memory region.
    // A proper implementation would use the VMM to map this memory.
    (void)len;
    return (void*)addr;
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
    // HACK: Corresponds to the identity-mapping hack above. Nothing to do.
    (void)addr;
    (void)len;
}

#ifndef UACPI_FORMATTED_LOGGING
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *msg) {
    const char* prefix;
    switch (level) {
        case UACPI_LOG_TRACE: prefix = "[uACPI TRACE] "; break;
        case UACPI_LOG_INFO:  prefix = "[uACPI INFO] ";  break;
        case UACPI_LOG_WARN:  prefix = "[uACPI WARN] ";  break;
        case UACPI_LOG_ERROR: prefix = "[uACPI ERROR] "; break;
        default:              prefix = "[uACPI] "; break;
    }
    debuglog_write(prefix);
    debuglog_write(msg);
    debuglog_write_char('\n');
}
#else
void uacpi_kernel_vlog(uacpi_log_level level, const uacpi_char *format, uacpi_va_list args) {
    // This OS doesn't have vsprintf, so we can't implement this easily.
    // For now, just log that the function was called.
    (void)level;
    (void)format;
    (void)args;
    debuglog_write("[uACPI] uacpi_kernel_vlog not implemented\n");
}
#endif

#ifndef UACPI_BAREBONES_MODE

// ============================================================================ 
// PCI
// ============================================================================ 

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
    uacpi_pci_address* addr = kmalloc(sizeof(uacpi_pci_address));
    if (!addr) return UACPI_STATUS_OUT_OF_MEMORY;
    *addr = address;
    *out_handle = (uacpi_handle)addr;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {
    kfree(handle);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    *value = pci_config_read8(addr->segment, addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    *value = pci_config_read16(addr->segment, addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    *value = pci_config_read32(addr->segment, addr->bus, addr->device, addr->function, offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    pci_config_write8(addr->segment, addr->bus, addr->device, addr->function, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    pci_config_write16(addr->segment, addr->bus, addr->device, addr->function, offset, value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
    uacpi_pci_address* addr = (uacpi_pci_address*)device;
    pci_config_write32(addr->segment, addr->bus, addr->device, addr->function, offset, value);
    return UACPI_STATUS_OK;
}

// ============================================================================ 
// I/O
// ============================================================================ 

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
    (void)len;
    // For x86, the handle can just be the base address.
    *out_handle = (uacpi_handle)base;
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {
    // Nothing to do for x86 I/O ports.
    (void)handle;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
    *out_value = inportb((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
    *out_value = inportw((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
    *out_value = inportd((uacpi_io_addr)handle + offset);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    outportb((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    outportw((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    outportd((uacpi_io_addr)handle + offset, in_value);
    return UACPI_STATUS_OK;
}

// ============================================================================ 
// Memory Allocation
// ============================================================================ 

void *uacpi_kernel_alloc(uacpi_size size) {
    return kmalloc(size);
}

void uacpi_kernel_free(void *mem) {
    kfree(mem);
}

// ============================================================================ 
// Timing & Delays
// ============================================================================ 

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void) {
    // Timer is 100Hz -> 1 tick = 10ms
    return (uacpi_u64)timer_get_ticks() * 10 * 1000 * 1000;
}

void uacpi_kernel_stall(uacpi_u8 usec) {
    sleep_busy(usec);
}

void uacpi_kernel_sleep(uacpi_u64 msec) {
    sleep_interruptible(msec);
}

// ============================================================================ 
// Concurrency & Synchronization
// ============================================================================ 

uacpi_thread_id uacpi_kernel_get_thread_id(void) {
    if (current_task) {
        return (uacpi_thread_id)(uintptr_t)current_task->id;
    }
    // Before tasking is initialized
    return (uacpi_thread_id)(uintptr_t)1;
}

uacpi_handle uacpi_kernel_create_spinlock(void) {
    spinlock_t* lock = kmalloc(sizeof(spinlock_t));
    if (!lock) return NULL;
    spinlock_init(lock, "uacpi_lock");
    return (uacpi_handle)lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
    kfree(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
    return spinlock_irq_save();
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags) {
    spinlock_irq_restore(flags);
}


uacpi_handle uacpi_kernel_create_mutex(void) {
    mutex_t* mutex = kmalloc(sizeof(mutex_t));
    if (!mutex) return NULL;
    mutex_init(mutex, "uacpi_mutex");
    return (uacpi_handle)mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
    kfree(handle);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
    mutex_t* mutex = (mutex_t*)handle;
    if (timeout == 0) {
        if (mutex_try_lock(mutex)) {
            return UACPI_STATUS_OK;
        }
        return UACPI_STATUS_TIMEOUT;
    }

    // This implementation doesn't support timed waits, so we treat any
    // timeout > 0 as an infinite wait.
    mutex_lock(mutex);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
    mutex_unlock((mutex_t*)handle);
}


uacpi_handle uacpi_kernel_create_event(void) {
    semaphore_t* sem = kmalloc(sizeof(semaphore_t));
    if (!sem) return NULL;
    semaphore_init(sem, 0, 1, "uacpi_event");
    return (uacpi_handle)sem;
}

void uacpi_kernel_free_event(uacpi_handle handle) {
    kfree(handle);
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
    semaphore_t* sem = (semaphore_t*)handle;
    if (timeout == 0) {
        return semaphore_try_wait(sem);
    }

    // This implementation doesn't support timed waits.
    semaphore_wait(sem);
    return true; // Assume success
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
    semaphore_post((semaphore_t*)handle);
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
    semaphore_t* sem = (semaphore_t*)handle;
    atomic_store32(&sem->count, 0);
}


// ============================================================================ 
// Stubs for currently unimplemented features
// ============================================================================ 

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *req) {
    (void)req;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_install_interrupt_handler(
    uacpi_u32 irq, uacpi_interrupt_handler handler, uacpi_handle ctx,
    uacpi_handle *out_irq_handle
) {
    (void)irq;
    (void)handler;
    (void)ctx;
    (void)out_irq_handle;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(
    uacpi_interrupt_handler handler, uacpi_handle irq_handle
) {
    (void)handler;
    (void)irq_handle;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_schedule_work(
    uacpi_work_type type, uacpi_work_handler handler, uacpi_handle ctx
) {
    (void)type;
    (void)handler;
    (void)ctx;
    return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_wait_for_work_completion(void) {
    return UACPI_STATUS_UNIMPLEMENTED;
}

#endif // !UACPI_BAREBONES_MODE