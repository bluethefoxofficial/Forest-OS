#ifndef IPI_SMP_COORDINATION_H
#define IPI_SMP_COORDINATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IPI_VECTOR_TLB_FLUSH    0xF0
#define IPI_VECTOR_FUNCTION_CALL 0xF1
#define IPI_VECTOR_RESCHEDULE   0xF2
#define IPI_VECTOR_SHUTDOWN     0xF3
#define IPI_VECTOR_BARRIER      0xF4
#define IPI_VECTOR_NMI          0xF5

#define IPI_ALL_CPUS            0xFFFFFFFFFFFFFFFFULL

typedef enum {
    IPI_SUCCESS = 0,
    IPI_ERROR_INVALID_PARAMS,
    IPI_ERROR_NOT_INITIALIZED,
    IPI_ERROR_CPU_OFFLINE,
    IPI_ERROR_TIMEOUT,
    IPI_ERROR_BUSY,
    IPI_ERROR_NO_MEMORY,
    IPI_ERROR_DELIVERY_FAILED
} ipi_error_t;

typedef enum {
    IPI_TYPE_TLB_FLUSH = 0,
    IPI_TYPE_FUNCTION_CALL,
    IPI_TYPE_RESCHEDULE,
    IPI_TYPE_SHUTDOWN,
    IPI_TYPE_BARRIER,
    IPI_TYPE_NMI,
    IPI_TYPE_CUSTOM
} ipi_type_t;

typedef enum {
    IPI_DELIVERY_FIXED = 0,
    IPI_DELIVERY_LOWEST_PRIORITY = 1,
    IPI_DELIVERY_SMI = 2,
    IPI_DELIVERY_NMI = 4,
    IPI_DELIVERY_INIT = 5,
    IPI_DELIVERY_STARTUP = 6
} ipi_delivery_mode_t;

typedef void (*ipi_function_t)(void *data);
typedef void (*ipi_completion_callback_t)(void *data, ipi_error_t result);

typedef struct {
    ipi_function_t function;
    void *data;
} ipi_function_call_t;

typedef struct {
    bool enable_broadcast_optimization;
    bool enable_barrier_timeout;
    bool enable_completion_callbacks;
    uint32_t default_timeout_ms;
    uint32_t max_pending_ipis;
} ipi_coordination_config_t;

typedef struct {
    uint64_t total_ipis_sent;
    uint64_t total_ipis_received;
    uint64_t total_ipis_failed;
    uint64_t total_barrier_operations;
    uint64_t total_broadcast_operations;
    uint32_t pending_ipis;
    uint32_t active_cpus;
} ipi_statistics_t;

typedef struct {
    uint32_t cpu_id;
    uint64_t ipis_sent;
    uint64_t ipis_received;
    uint64_t ipis_failed;
    uint32_t pending_count;
    bool tlb_flush_pending;
    bool function_call_pending;
    bool reschedule_pending;
    bool shutdown_pending;
    uint64_t last_activity_time;
} cpu_ipi_statistics_t;

ipi_error_t ipi_coordination_init(const ipi_coordination_config_t *config);

ipi_error_t ipi_send_tlb_flush(uint32_t target_cpu);

ipi_error_t ipi_send_function_call(uint32_t target_cpu, ipi_function_t function, 
                                 void *data, bool wait_for_completion);

ipi_error_t ipi_send_reschedule(uint32_t target_cpu);

ipi_error_t ipi_broadcast_tlb_flush(uint64_t cpu_mask);

ipi_error_t ipi_broadcast_function_call(uint64_t cpu_mask, ipi_function_t function,
                                      void *data, bool wait_for_completion);

ipi_error_t ipi_barrier_synchronize(uint64_t cpu_mask, uint32_t timeout_ms);

ipi_error_t ipi_startup_cpu(uint32_t target_cpu, uint32_t startup_vector);

ipi_error_t ipi_shutdown_cpu(uint32_t target_cpu);

ipi_error_t ipi_shutdown_all_cpus(void);

ipi_error_t ipi_get_statistics(ipi_statistics_t *stats);

ipi_error_t ipi_get_cpu_statistics(uint32_t cpu_id, cpu_ipi_statistics_t *stats);

void ipi_reset_statistics(void);

bool ipi_coordination_is_initialized(void);

bool ipi_is_shutdown_requested(void);

uint32_t ipi_get_barrier_generation(void);

static inline const char* ipi_error_to_string(ipi_error_t error) {
    switch (error) {
        case IPI_SUCCESS:
            return "Success";
        case IPI_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case IPI_ERROR_NOT_INITIALIZED:
            return "IPI coordination not initialized";
        case IPI_ERROR_CPU_OFFLINE:
            return "Target CPU is offline";
        case IPI_ERROR_TIMEOUT:
            return "IPI operation timed out";
        case IPI_ERROR_BUSY:
            return "Target CPU is busy";
        case IPI_ERROR_NO_MEMORY:
            return "Insufficient memory for IPI operation";
        case IPI_ERROR_DELIVERY_FAILED:
            return "IPI delivery failed";
        default:
            return "Unknown IPI error";
    }
}

static inline const char* ipi_type_to_string(ipi_type_t type) {
    switch (type) {
        case IPI_TYPE_TLB_FLUSH:
            return "TLB Flush";
        case IPI_TYPE_FUNCTION_CALL:
            return "Function Call";
        case IPI_TYPE_RESCHEDULE:
            return "Reschedule";
        case IPI_TYPE_SHUTDOWN:
            return "Shutdown";
        case IPI_TYPE_BARRIER:
            return "Barrier";
        case IPI_TYPE_NMI:
            return "NMI";
        case IPI_TYPE_CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
}

static inline ipi_coordination_config_t ipi_coordination_default_config(void) {
    return (ipi_coordination_config_t){
        .enable_broadcast_optimization = true,
        .enable_barrier_timeout = true,
        .enable_completion_callbacks = false,
        .default_timeout_ms = 1000,
        .max_pending_ipis = 64
    };
}

static inline ipi_coordination_config_t ipi_coordination_realtime_config(void) {
    return (ipi_coordination_config_t){
        .enable_broadcast_optimization = false,
        .enable_barrier_timeout = true,
        .enable_completion_callbacks = true,
        .default_timeout_ms = 10,
        .max_pending_ipis = 16
    };
}

static inline ipi_coordination_config_t ipi_coordination_performance_config(void) {
    return (ipi_coordination_config_t){
        .enable_broadcast_optimization = true,
        .enable_barrier_timeout = false,
        .enable_completion_callbacks = false,
        .default_timeout_ms = 5000,
        .max_pending_ipis = 128
    };
}

#define IPI_SEND_TO_ALL_BUT_SELF(function, data, wait) \
    ipi_broadcast_function_call(IPI_ALL_CPUS & ~(1ULL << get_current_cpu_id()), \
                               (function), (data), (wait))

#define IPI_BARRIER_ALL_CPUS(timeout_ms) \
    ipi_barrier_synchronize(IPI_ALL_CPUS, (timeout_ms))

#define IPI_FLUSH_TLB_ALL() \
    ipi_broadcast_tlb_flush(IPI_ALL_CPUS)

typedef struct {
    void (*atomic_inc)(volatile uint32_t *value);
    void (*atomic_dec)(volatile uint32_t *value);
    void (*memory_barrier)(void);
    void (*cpu_relax)(void);
} ipi_synchronization_primitives_t;

static inline void ipi_atomic_inc(volatile uint32_t *value) {
    __sync_fetch_and_add(value, 1);
}

static inline void ipi_atomic_dec(volatile uint32_t *value) {
    __sync_fetch_and_sub(value, 1);
}

static inline void ipi_memory_barrier(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

static inline void ipi_cpu_relax(void) {
    __asm__ volatile ("pause" ::: "memory");
}

static inline uint64_t ipi_create_cpu_mask(const uint32_t *cpu_list, size_t count) {
    uint64_t mask = 0;
    for (size_t i = 0; i < count; i++) {
        if (cpu_list[i] < 64) {
            mask |= (1ULL << cpu_list[i]);
        }
    }
    return mask;
}

static inline uint64_t ipi_create_range_mask(uint32_t start_cpu, uint32_t end_cpu) {
    if (start_cpu >= 64 || end_cpu >= 64 || start_cpu > end_cpu) {
        return 0;
    }
    
    uint64_t mask = 0;
    for (uint32_t cpu = start_cpu; cpu <= end_cpu; cpu++) {
        mask |= (1ULL << cpu);
    }
    return mask;
}

extern uint64_t rdtsc(void);
extern uint32_t get_current_cpu_id(void);
extern uint64_t tsc_frequency_hz;
extern void schedule(void);

extern uint32_t local_apic_read(uint32_t reg);
extern void local_apic_write(uint32_t reg, uint32_t value);

#define APIC_ICR_LOW    0x300
#define APIC_ICR_HIGH   0x310

#endif // IPI_SMP_COORDINATION_H