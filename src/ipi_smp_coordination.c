#include "ipi_smp_coordination.h"
#include "smp_interrupt_distribution.h"
#include "local_apic.h"
#include "interrupt_management.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define IPI_TIMEOUT_CYCLES 1000000
#define MAX_IPI_PENDING 64
#define IPI_RETRY_COUNT 3
#define IPI_COMPLETION_TIMEOUT_US 10000

typedef struct {
    ipi_type_t type;
    uint32_t sender_cpu;
    uint32_t target_cpu;
    uint64_t data;
    uint64_t timestamp;
    ipi_completion_callback_t callback;
    void *callback_data;
    volatile bool completed;
    ipi_error_t result;
} ipi_message_t;

typedef struct {
    uint32_t cpu_id;
    volatile bool tlb_flush_pending;
    volatile bool function_call_pending;
    volatile bool reschedule_pending;
    volatile bool shutdown_pending;
    ipi_message_t pending_ipis[MAX_IPI_PENDING];
    size_t pending_count;
    uint64_t ipis_sent;
    uint64_t ipis_received;
    uint64_t ipis_failed;
    uint64_t last_activity_time;
} cpu_ipi_state_t;

typedef struct {
    cpu_ipi_state_t cpu_states[MAX_CPU_COUNT];
    size_t cpu_count;
    
    volatile uint32_t global_barrier_count;
    volatile uint32_t global_barrier_generation;
    volatile bool global_shutdown_requested;
    
    ipi_coordination_config_t config;
    
    uint64_t total_ipis_sent;
    uint64_t total_ipis_received;
    uint64_t total_barrier_operations;
    uint64_t total_broadcast_operations;
    
    bool initialized;
} ipi_coordination_context_t;

static ipi_coordination_context_t ipi_ctx = {0};
static __thread volatile ipi_function_call_t pending_function_call = {0};

static void send_raw_ipi(uint32_t target_apic_id, uint8_t vector, 
                        ipi_delivery_mode_t delivery_mode) {
    uint32_t icr_high = target_apic_id << 24;
    uint32_t icr_low = vector;
    
    switch (delivery_mode) {
        case IPI_DELIVERY_FIXED:
            icr_low |= (0 << 8);
            break;
        case IPI_DELIVERY_LOWEST_PRIORITY:
            icr_low |= (1 << 8);
            break;
        case IPI_DELIVERY_SMI:
            icr_low |= (2 << 8);
            break;
        case IPI_DELIVERY_NMI:
            icr_low |= (4 << 8);
            break;
        case IPI_DELIVERY_INIT:
            icr_low |= (5 << 8);
            break;
        case IPI_DELIVERY_STARTUP:
            icr_low |= (6 << 8);
            break;
    }
    
    local_apic_write(APIC_ICR_HIGH, icr_high);
    local_apic_write(APIC_ICR_LOW, icr_low);
    
    uint64_t timeout = rdtsc() + IPI_TIMEOUT_CYCLES;
    while ((local_apic_read(APIC_ICR_LOW) & (1 << 12)) != 0) {
        if (rdtsc() > timeout) {
            break;
        }
        __asm__ volatile ("pause");
    }
}

static void handle_tlb_flush_ipi(void) {
    uint32_t cpu_id = get_current_cpu_id();
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    if (state->tlb_flush_pending) {
#if ARCH_64BIT
        __asm__ volatile (
            "mov %%cr3, %%rax\n\t"
            "mov %%rax, %%cr3"
            :
            :
            : "rax", "memory"
        );
#else
        __asm__ volatile (
            "mov %%cr3, %%eax\n\t"
            "mov %%eax, %%cr3"
            :
            :
            : "eax", "memory"
        );
#endif

        state->tlb_flush_pending = false;
    }
    
    state->ipis_received++;
    ipi_ctx.total_ipis_received++;
}

static void handle_function_call_ipi(void) {
    uint32_t cpu_id = get_current_cpu_id();
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    if (state->function_call_pending && pending_function_call.function) {
        pending_function_call.function(pending_function_call.data);
        pending_function_call.function = NULL;
        state->function_call_pending = false;
    }
    
    state->ipis_received++;
    ipi_ctx.total_ipis_received++;
}

void handle_reschedule_ipi(void) {
    uint32_t cpu_id = get_current_cpu_id();
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    if (state->reschedule_pending) {
        schedule();
        state->reschedule_pending = false;
    }
    
    state->ipis_received++;
    ipi_ctx.total_ipis_received++;
}

static void handle_shutdown_ipi(void) {
    uint32_t cpu_id = get_current_cpu_id();
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    state->shutdown_pending = true;
    state->ipis_received++;
    ipi_ctx.total_ipis_received++;
    
    __asm__ volatile ("cli");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static void handle_barrier_ipi(void) {
    uint32_t cpu_id = get_current_cpu_id();
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    __sync_fetch_and_add(&ipi_ctx.global_barrier_count, 1);
    
    state->ipis_received++;
    ipi_ctx.total_ipis_received++;
}

ipi_error_t ipi_coordination_init(const ipi_coordination_config_t *config) {
    if (!config) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    memset(&ipi_ctx, 0, sizeof(ipi_ctx));
    ipi_ctx.config = *config;
    ipi_ctx.cpu_count = smp_interrupt_get_cpu_count();
    
    if (ipi_ctx.cpu_count > MAX_CPU_COUNT) {
        ipi_ctx.cpu_count = MAX_CPU_COUNT;
    }
    
    for (size_t i = 0; i < ipi_ctx.cpu_count; i++) {
        ipi_ctx.cpu_states[i].cpu_id = i;
    }
    
    interrupt_register_handler(IPI_VECTOR_TLB_FLUSH, handle_tlb_flush_ipi, NULL);
    interrupt_register_handler(IPI_VECTOR_FUNCTION_CALL, handle_function_call_ipi, NULL);
    interrupt_register_handler(IPI_VECTOR_RESCHEDULE, handle_reschedule_ipi, NULL);
    interrupt_register_handler(IPI_VECTOR_SHUTDOWN, handle_shutdown_ipi, NULL);
    interrupt_register_handler(IPI_VECTOR_BARRIER, handle_barrier_ipi, NULL);
    
    ipi_ctx.initialized = true;
    return IPI_SUCCESS;
}

ipi_error_t ipi_send_tlb_flush(uint32_t target_cpu) {
    if (!ipi_ctx.initialized || target_cpu >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    if (target_cpu == get_current_cpu_id()) {
        handle_tlb_flush_ipi();
        return IPI_SUCCESS;
    }
    
    cpu_interrupt_info_t cpu_info;
    if (smp_interrupt_get_cpu_info(target_cpu, &cpu_info) != SMP_INT_SUCCESS ||
        !cpu_info.online) {
        return IPI_ERROR_CPU_OFFLINE;
    }
    
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[target_cpu];
    state->tlb_flush_pending = true;
    
    send_raw_ipi(cpu_info.local_apic_id, IPI_VECTOR_TLB_FLUSH, IPI_DELIVERY_FIXED);
    
    state->ipis_sent++;
    ipi_ctx.total_ipis_sent++;
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_send_function_call(uint32_t target_cpu, ipi_function_t function, 
                                 void *data, bool wait_for_completion) {
    if (!ipi_ctx.initialized || !function || target_cpu >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    if (target_cpu == get_current_cpu_id()) {
        function(data);
        return IPI_SUCCESS;
    }
    
    cpu_interrupt_info_t cpu_info;
    if (smp_interrupt_get_cpu_info(target_cpu, &cpu_info) != SMP_INT_SUCCESS ||
        !cpu_info.online) {
        return IPI_ERROR_CPU_OFFLINE;
    }
    
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[target_cpu];
    
    if (state->function_call_pending) {
        return IPI_ERROR_BUSY;
    }
    
    pending_function_call.function = function;
    pending_function_call.data = data;
    state->function_call_pending = true;
    
    send_raw_ipi(cpu_info.local_apic_id, IPI_VECTOR_FUNCTION_CALL, IPI_DELIVERY_FIXED);
    
    if (wait_for_completion) {
        uint64_t timeout = rdtsc() + (IPI_COMPLETION_TIMEOUT_US * tsc_frequency_hz) / 1000000;
        while (state->function_call_pending && rdtsc() < timeout) {
            __asm__ volatile ("pause");
        }
        
        if (state->function_call_pending) {
            return IPI_ERROR_TIMEOUT;
        }
    }
    
    state->ipis_sent++;
    ipi_ctx.total_ipis_sent++;
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_send_reschedule(uint32_t target_cpu) {
    if (!ipi_ctx.initialized || target_cpu >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    if (target_cpu == get_current_cpu_id()) {
        schedule();
        return IPI_SUCCESS;
    }
    
    cpu_interrupt_info_t cpu_info;
    if (smp_interrupt_get_cpu_info(target_cpu, &cpu_info) != SMP_INT_SUCCESS ||
        !cpu_info.online) {
        return IPI_ERROR_CPU_OFFLINE;
    }
    
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[target_cpu];
    state->reschedule_pending = true;
    
    send_raw_ipi(cpu_info.local_apic_id, IPI_VECTOR_RESCHEDULE, IPI_DELIVERY_FIXED);
    
    state->ipis_sent++;
    ipi_ctx.total_ipis_sent++;
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_broadcast_tlb_flush(uint64_t cpu_mask) {
    if (!ipi_ctx.initialized) {
        return IPI_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t current_cpu = get_current_cpu_id();
    ipi_error_t result = IPI_SUCCESS;
    
    for (uint32_t cpu = 0; cpu < ipi_ctx.cpu_count; cpu++) {
        if (cpu == current_cpu) continue;
        if ((cpu_mask & (1ULL << cpu)) == 0) continue;
        
        ipi_error_t send_result = ipi_send_tlb_flush(cpu);
        if (send_result != IPI_SUCCESS && result == IPI_SUCCESS) {
            result = send_result;
        }
    }
    
    handle_tlb_flush_ipi();
    
    ipi_ctx.total_broadcast_operations++;
    return result;
}

ipi_error_t ipi_broadcast_function_call(uint64_t cpu_mask, ipi_function_t function,
                                      void *data, bool wait_for_completion) {
    if (!ipi_ctx.initialized || !function) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    uint32_t current_cpu = get_current_cpu_id();
    ipi_error_t result = IPI_SUCCESS;
    
    for (uint32_t cpu = 0; cpu < ipi_ctx.cpu_count; cpu++) {
        if (cpu == current_cpu) continue;
        if ((cpu_mask & (1ULL << cpu)) == 0) continue;
        
        ipi_error_t send_result = ipi_send_function_call(cpu, function, data, 
                                                       wait_for_completion);
        if (send_result != IPI_SUCCESS && result == IPI_SUCCESS) {
            result = send_result;
        }
    }
    
    function(data);
    
    ipi_ctx.total_broadcast_operations++;
    return result;
}

ipi_error_t ipi_barrier_synchronize(uint64_t cpu_mask, uint32_t timeout_ms) {
    if (!ipi_ctx.initialized) {
        return IPI_ERROR_NOT_INITIALIZED;
    }
    
    uint32_t expected_cpus = __builtin_popcountll(cpu_mask);
    uint32_t current_generation = ipi_ctx.global_barrier_generation;
    
    ipi_ctx.global_barrier_count = 1; // Current CPU
    
    for (uint32_t cpu = 0; cpu < ipi_ctx.cpu_count; cpu++) {
        if (cpu == get_current_cpu_id()) continue;
        if ((cpu_mask & (1ULL << cpu)) == 0) continue;
        
        cpu_interrupt_info_t cpu_info;
        if (smp_interrupt_get_cpu_info(cpu, &cpu_info) != SMP_INT_SUCCESS ||
            !cpu_info.online) {
            continue;
        }
        
        send_raw_ipi(cpu_info.local_apic_id, IPI_VECTOR_BARRIER, IPI_DELIVERY_FIXED);
    }
    
    uint64_t timeout_cycles = (timeout_ms * tsc_frequency_hz) / 1000;
    uint64_t start_time = rdtsc();
    
    while (ipi_ctx.global_barrier_count < expected_cpus) {
        if (rdtsc() - start_time > timeout_cycles) {
            return IPI_ERROR_TIMEOUT;
        }
        __asm__ volatile ("pause");
    }
    
    ipi_ctx.global_barrier_generation++;
    ipi_ctx.total_barrier_operations++;
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_startup_cpu(uint32_t target_cpu, uint32_t startup_vector) {
    if (!ipi_ctx.initialized || target_cpu >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    cpu_interrupt_info_t cpu_info;
    if (smp_interrupt_get_cpu_info(target_cpu, &cpu_info) != SMP_INT_SUCCESS) {
        return IPI_ERROR_CPU_OFFLINE;
    }
    
    send_raw_ipi(cpu_info.local_apic_id, 0, IPI_DELIVERY_INIT);
    
    for (volatile int delay = 0; delay < 10000; delay++);
    
    send_raw_ipi(cpu_info.local_apic_id, startup_vector, IPI_DELIVERY_STARTUP);
    
    for (volatile int delay = 0; delay < 1000; delay++);
    
    send_raw_ipi(cpu_info.local_apic_id, startup_vector, IPI_DELIVERY_STARTUP);
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_shutdown_cpu(uint32_t target_cpu) {
    if (!ipi_ctx.initialized || target_cpu >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    if (target_cpu == get_current_cpu_id()) {
        handle_shutdown_ipi();
        return IPI_SUCCESS;
    }
    
    cpu_interrupt_info_t cpu_info;
    if (smp_interrupt_get_cpu_info(target_cpu, &cpu_info) != SMP_INT_SUCCESS ||
        !cpu_info.online) {
        return IPI_ERROR_CPU_OFFLINE;
    }
    
    send_raw_ipi(cpu_info.local_apic_id, IPI_VECTOR_SHUTDOWN, IPI_DELIVERY_FIXED);
    
    ipi_ctx.cpu_states[target_cpu].ipis_sent++;
    ipi_ctx.total_ipis_sent++;
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_shutdown_all_cpus(void) {
    if (!ipi_ctx.initialized) {
        return IPI_ERROR_NOT_INITIALIZED;
    }
    
    ipi_ctx.global_shutdown_requested = true;
    
    uint32_t current_cpu = get_current_cpu_id();
    for (uint32_t cpu = 0; cpu < ipi_ctx.cpu_count; cpu++) {
        if (cpu == current_cpu) continue;
        
        ipi_shutdown_cpu(cpu);
    }
    
    for (volatile int delay = 0; delay < 1000000; delay++);
    
    handle_shutdown_ipi();
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_get_statistics(ipi_statistics_t *stats) {
    if (!ipi_ctx.initialized || !stats) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    memset(stats, 0, sizeof(ipi_statistics_t));
    
    stats->total_ipis_sent = ipi_ctx.total_ipis_sent;
    stats->total_ipis_received = ipi_ctx.total_ipis_received;
    stats->total_barrier_operations = ipi_ctx.total_barrier_operations;
    stats->total_broadcast_operations = ipi_ctx.total_broadcast_operations;
    
    for (size_t i = 0; i < ipi_ctx.cpu_count; i++) {
        cpu_ipi_state_t *state = &ipi_ctx.cpu_states[i];
        stats->total_ipis_failed += state->ipis_failed;
        
        if (state->pending_count > 0) {
            stats->pending_ipis += state->pending_count;
        }
    }
    
    return IPI_SUCCESS;
}

ipi_error_t ipi_get_cpu_statistics(uint32_t cpu_id, cpu_ipi_statistics_t *stats) {
    if (!ipi_ctx.initialized || !stats || cpu_id >= ipi_ctx.cpu_count) {
        return IPI_ERROR_INVALID_PARAMS;
    }
    
    cpu_ipi_state_t *state = &ipi_ctx.cpu_states[cpu_id];
    
    *stats = (cpu_ipi_statistics_t){
        .cpu_id = cpu_id,
        .ipis_sent = state->ipis_sent,
        .ipis_received = state->ipis_received,
        .ipis_failed = state->ipis_failed,
        .pending_count = state->pending_count,
        .tlb_flush_pending = state->tlb_flush_pending,
        .function_call_pending = state->function_call_pending,
        .reschedule_pending = state->reschedule_pending,
        .shutdown_pending = state->shutdown_pending,
        .last_activity_time = state->last_activity_time
    };
    
    return IPI_SUCCESS;
}

void ipi_reset_statistics(void) {
    if (!ipi_ctx.initialized) {
        return;
    }
    
    for (size_t i = 0; i < ipi_ctx.cpu_count; i++) {
        cpu_ipi_state_t *state = &ipi_ctx.cpu_states[i];
        state->ipis_sent = 0;
        state->ipis_received = 0;
        state->ipis_failed = 0;
        state->pending_count = 0;
    }
    
    ipi_ctx.total_ipis_sent = 0;
    ipi_ctx.total_ipis_received = 0;
    ipi_ctx.total_barrier_operations = 0;
    ipi_ctx.total_broadcast_operations = 0;
}

bool ipi_coordination_is_initialized(void) {
    return ipi_ctx.initialized;
}

bool ipi_is_shutdown_requested(void) {
    return ipi_ctx.global_shutdown_requested;
}

uint32_t ipi_get_barrier_generation(void) {
    return ipi_ctx.global_barrier_generation;
}