#ifndef INTERRUPT_COALESCING_H
#define INTERRUPT_COALESCING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "interrupt_common_types.h"

typedef enum {
    INT_COALESCING_SUCCESS = 0,
    INT_COALESCING_ERROR_INVALID_PARAMS,
    INT_COALESCING_ERROR_NOT_INITIALIZED,
    INT_COALESCING_ERROR_NO_SPACE,
    INT_COALESCING_ERROR_DEVICE_NOT_FOUND,
    INT_COALESCING_ERROR_ALREADY_ENABLED,
    INT_COALESCING_ERROR_HANDLER_NOT_FOUND,
    INT_COALESCING_ERROR_TIMER_SETUP_FAILED,
    INT_COALESCING_ERROR_CONFIGURATION_INVALID
} interrupt_coalescing_error_t;

typedef enum {
    COALESCING_MODE_COUNT = 0,      // Coalesce based on interrupt count
    COALESCING_MODE_TIME = 1,       // Coalesce based on time interval
    COALESCING_MODE_ADAPTIVE = 2,   // Adaptive coalescing based on load
    COALESCING_MODE_RATE_LIMITED = 3 // Rate-limited delivery
} coalescing_mode_t;

typedef struct {
    coalescing_mode_t mode;
    uint32_t max_batch_size;        // Maximum interrupts to batch
    uint32_t timeout_us;            // Timeout in microseconds
    uint32_t max_rate_hz;           // Maximum delivery rate (for rate-limited mode)
    bool adaptive_reduce_batch;     // Reduce batch size under light load
    bool enable_statistics;         // Enable detailed statistics collection
} coalescing_config_t;

typedef struct {
    uint64_t interrupts_received;
    uint64_t interrupts_delivered;
    uint64_t interrupts_dropped;
    uint64_t batches_delivered;
    uint64_t avg_batch_size;
    uint64_t avg_coalescing_latency_ns;
    uint64_t max_coalescing_latency_ns;
} coalescing_stats_t;

typedef struct {
    uint8_t vector;
    coalescing_mode_t mode;
    bool enabled;
    size_t pending_count;
    bool timer_active;
    double coalescing_ratio;
    coalescing_stats_t stats;
} coalescing_device_stats_t;

typedef struct {
    size_t enabled_devices;
    uint64_t total_interrupts_received;
    uint64_t total_interrupts_delivered;
    uint64_t total_interrupts_coalesced;
    uint64_t total_timer_expirations;
    double global_coalescing_ratio;
    double average_batch_size;
} coalescing_global_stats_t;

typedef void (*coalescing_handler_t)(void *context);

interrupt_coalescing_error_t interrupt_coalescing_init(void);

interrupt_coalescing_error_t interrupt_coalescing_enable_device(
    uint8_t vector, 
    const coalescing_config_t *config);

interrupt_coalescing_error_t interrupt_coalescing_disable_device(uint8_t vector);

interrupt_coalescing_error_t interrupt_coalescing_configure_device(
    uint8_t vector, 
    const coalescing_config_t *config);

interrupt_coalescing_error_t interrupt_coalescing_force_delivery(uint8_t vector);

interrupt_coalescing_error_t interrupt_coalescing_get_device_stats(
    uint8_t vector, 
    coalescing_device_stats_t *stats);

interrupt_coalescing_error_t interrupt_coalescing_get_global_stats(
    coalescing_global_stats_t *stats);

void interrupt_coalescing_process_timeouts(void);

bool interrupt_coalescing_is_enabled(uint8_t vector);

bool interrupt_coalescing_is_initialized(void);

size_t interrupt_coalescing_get_device_count(void);

uint64_t interrupt_coalescing_get_total_savings(void);

static inline const char* interrupt_coalescing_error_to_string(interrupt_coalescing_error_t error) {
    switch (error) {
        case INT_COALESCING_SUCCESS:
            return "Success";
        case INT_COALESCING_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case INT_COALESCING_ERROR_NOT_INITIALIZED:
            return "Interrupt coalescing not initialized";
        case INT_COALESCING_ERROR_NO_SPACE:
            return "No space for additional devices";
        case INT_COALESCING_ERROR_DEVICE_NOT_FOUND:
            return "Device not found";
        case INT_COALESCING_ERROR_ALREADY_ENABLED:
            return "Coalescing already enabled for device";
        case INT_COALESCING_ERROR_HANDLER_NOT_FOUND:
            return "Original interrupt handler not found";
        case INT_COALESCING_ERROR_TIMER_SETUP_FAILED:
            return "Failed to setup coalescing timer";
        case INT_COALESCING_ERROR_CONFIGURATION_INVALID:
            return "Invalid coalescing configuration";
        default:
            return "Unknown interrupt coalescing error";
    }
}

static inline const char* coalescing_mode_to_string(coalescing_mode_t mode) {
    switch (mode) {
        case COALESCING_MODE_COUNT:
            return "Count-based";
        case COALESCING_MODE_TIME:
            return "Time-based";
        case COALESCING_MODE_ADAPTIVE:
            return "Adaptive";
        case COALESCING_MODE_RATE_LIMITED:
            return "Rate-limited";
        default:
            return "Unknown";
    }
}

static inline coalescing_config_t interrupt_coalescing_default_config(void) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_ADAPTIVE,
        .max_batch_size = 16,
        .timeout_us = 100,
        .max_rate_hz = 1000,
        .adaptive_reduce_batch = true,
        .enable_statistics = true
    };
}

static inline coalescing_config_t interrupt_coalescing_network_config(void) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_ADAPTIVE,
        .max_batch_size = 32,
        .timeout_us = 50,
        .max_rate_hz = 10000,
        .adaptive_reduce_batch = true,
        .enable_statistics = true
    };
}

static inline coalescing_config_t interrupt_coalescing_storage_config(void) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_COUNT,
        .max_batch_size = 8,
        .timeout_us = 200,
        .max_rate_hz = 5000,
        .adaptive_reduce_batch = false,
        .enable_statistics = true
    };
}

static inline coalescing_config_t interrupt_coalescing_realtime_config(void) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_TIME,
        .max_batch_size = 4,
        .timeout_us = 10,
        .max_rate_hz = 100000,
        .adaptive_reduce_batch = false,
        .enable_statistics = false
    };
}

static inline coalescing_config_t interrupt_coalescing_high_throughput_config(void) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_COUNT,
        .max_batch_size = 64,
        .timeout_us = 500,
        .max_rate_hz = 2000,
        .adaptive_reduce_batch = false,
        .enable_statistics = true
    };
}

static inline coalescing_config_t create_count_based_config(uint32_t max_batch_size, 
                                                           uint32_t timeout_us) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_COUNT,
        .max_batch_size = max_batch_size,
        .timeout_us = timeout_us,
        .max_rate_hz = 0,
        .adaptive_reduce_batch = false,
        .enable_statistics = true
    };
}

static inline coalescing_config_t create_time_based_config(uint32_t timeout_us) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_TIME,
        .max_batch_size = 0,
        .timeout_us = timeout_us,
        .max_rate_hz = 0,
        .adaptive_reduce_batch = false,
        .enable_statistics = true
    };
}

static inline coalescing_config_t create_rate_limited_config(uint32_t max_rate_hz, 
                                                           uint32_t max_batch_size) {
    return (coalescing_config_t){
        .mode = COALESCING_MODE_RATE_LIMITED,
        .max_batch_size = max_batch_size,
        .timeout_us = 0,
        .max_rate_hz = max_rate_hz,
        .adaptive_reduce_batch = false,
        .enable_statistics = true
    };
}

/* Timer types and coalescing_handler_t are now in interrupt_common_types.h */

extern void interrupt_register_handler_coalescing(uint8_t vector, coalescing_handler_t handler, void *context);

extern interrupt_error_t interrupt_get_handler_coalescing(uint8_t vector, coalescing_handler_t *handler,
                                                         void **context);

#define COALESCING_AUTO_TUNE_BATCH_SIZE(load_factor) \
    ((uint32_t)(16 * (1.0 + (load_factor))))

#define COALESCING_AUTO_TUNE_TIMEOUT(latency_requirement_us) \
    ((uint32_t)((latency_requirement_us) / 2))

#define COALESCING_CALCULATE_EFFICIENCY(received, delivered) \
    ((delivered) > 0 ? ((double)(received) / (double)(delivered)) : 0.0)

static inline bool coalescing_should_enable_for_device(uint64_t interrupt_rate_hz) {
    return interrupt_rate_hz > 1000;
}

static inline uint32_t coalescing_recommend_batch_size(uint64_t interrupt_rate_hz) {
    if (interrupt_rate_hz > 50000) return 64;
    if (interrupt_rate_hz > 10000) return 32;
    if (interrupt_rate_hz > 5000) return 16;
    if (interrupt_rate_hz > 1000) return 8;
    return 4;
}

static inline uint32_t coalescing_recommend_timeout_us(uint64_t latency_requirement_us) {
    if (latency_requirement_us < 50) return 10;
    if (latency_requirement_us < 200) return 50;
    if (latency_requirement_us < 1000) return 100;
    return 500;
}

#endif // INTERRUPT_COALESCING_H