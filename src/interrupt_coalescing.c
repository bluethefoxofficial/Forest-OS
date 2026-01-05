#include "interrupt_coalescing.h"
#include "interrupt_management.h"
#include "interrupt_latency_optimization.h"
#include "timer_abstraction.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_COALESCED_DEVICES 64
#define MAX_PENDING_INTERRUPTS 512
#define COALESCING_TIMER_VECTOR 0xE0
#define DEFAULT_COALESCING_TIMEOUT_US 100
#define MIN_COALESCING_TIMEOUT_US 10
#define MAX_COALESCING_TIMEOUT_US 10000

typedef struct {
    uint8_t vector;
    uint64_t timestamp;
    void *context;
    coalescing_handler_t original_handler;
    coalescing_stats_t stats;
} pending_interrupt_t;

typedef struct {
    uint8_t vector;
    coalescing_handler_t original_handler;
    void *handler_context;
    
    coalescing_config_t config;
    coalescing_mode_t mode;
    
    pending_interrupt_t pending_interrupts[MAX_PENDING_INTERRUPTS];
    size_t pending_count;
    
    uint64_t last_delivery_time;
    uint64_t coalescing_timer_deadline;
    bool timer_active;
    
    coalescing_stats_t stats;
    bool enabled;
} coalesced_device_t;

typedef struct {
    coalesced_device_t devices[MAX_COALESCED_DEVICES];
    size_t device_count;
    
    timer_handle_t coalescing_timer;
    uint64_t timer_frequency;
    
    uint64_t total_interrupts_received;
    uint64_t total_interrupts_delivered;
    uint64_t total_interrupts_coalesced;
    uint64_t total_timer_expirations;
    
    bool initialized;
} interrupt_coalescing_context_t;

static interrupt_coalescing_context_t coal_ctx = {0};

static coalesced_device_t* find_device_by_vector(uint8_t vector) {
    for (size_t i = 0; i < coal_ctx.device_count; i++) {
        if (coal_ctx.devices[i].vector == vector) {
            return &coal_ctx.devices[i];
        }
    }
    return NULL;
}

static void deliver_coalesced_interrupts(coalesced_device_t *device) {
    if (device->pending_count == 0) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    device->last_delivery_time = current_time;
    
    switch (device->mode) {
        case COALESCING_MODE_COUNT: {
            size_t batch_size = device->config.max_batch_size;
            if (batch_size > device->pending_count) {
                batch_size = device->pending_count;
            }
            
            for (size_t i = 0; i < batch_size; i++) {
                pending_interrupt_t *pending = &device->pending_interrupts[i];
                if (device->original_handler) {
                    device->original_handler(pending->context);
                }
                
                uint64_t latency_ns = (current_time - pending->timestamp) * 1000000000ULL / 
                                     coal_ctx.timer_frequency;
                device->stats.avg_coalescing_latency_ns = 
                    (device->stats.avg_coalescing_latency_ns + latency_ns) / 2;
            }
            
            memmove(&device->pending_interrupts[0], 
                   &device->pending_interrupts[batch_size],
                   (device->pending_count - batch_size) * sizeof(pending_interrupt_t));
            device->pending_count -= batch_size;
            
            device->stats.interrupts_delivered += batch_size;
            coal_ctx.total_interrupts_delivered += batch_size;
            break;
        }
        
        case COALESCING_MODE_TIME: {
            for (size_t i = 0; i < device->pending_count; i++) {
                pending_interrupt_t *pending = &device->pending_interrupts[i];
                if (device->original_handler) {
                    device->original_handler(pending->context);
                }
                
                uint64_t latency_ns = (current_time - pending->timestamp) * 1000000000ULL / 
                                     coal_ctx.timer_frequency;
                device->stats.avg_coalescing_latency_ns = 
                    (device->stats.avg_coalescing_latency_ns + latency_ns) / 2;
            }
            
            device->stats.interrupts_delivered += device->pending_count;
            coal_ctx.total_interrupts_delivered += device->pending_count;
            device->pending_count = 0;
            break;
        }
        
        case COALESCING_MODE_ADAPTIVE: {
            uint64_t time_since_last = current_time - device->last_delivery_time;
            uint64_t adaptive_threshold = device->config.timeout_us * 
                                         coal_ctx.timer_frequency / 1000000;
            
            if (device->pending_count >= device->config.max_batch_size ||
                time_since_last >= adaptive_threshold) {
                
                size_t deliver_count = device->pending_count;
                if (device->config.adaptive_reduce_batch && 
                    time_since_last < adaptive_threshold / 2) {
                    deliver_count = device->pending_count / 2;
                    if (deliver_count == 0) deliver_count = 1;
                }
                
                for (size_t i = 0; i < deliver_count; i++) {
                    pending_interrupt_t *pending = &device->pending_interrupts[i];
                    if (device->original_handler) {
                        device->original_handler(pending->context);
                    }
                }
                
                memmove(&device->pending_interrupts[0], 
                       &device->pending_interrupts[deliver_count],
                       (device->pending_count - deliver_count) * sizeof(pending_interrupt_t));
                device->pending_count -= deliver_count;
                
                device->stats.interrupts_delivered += deliver_count;
                coal_ctx.total_interrupts_delivered += deliver_count;
            }
            break;
        }
        
        case COALESCING_MODE_RATE_LIMITED: {
            uint64_t time_since_last = current_time - device->last_delivery_time;
            uint64_t min_interval = coal_ctx.timer_frequency / device->config.max_rate_hz;
            
            if (time_since_last >= min_interval) {
                size_t deliver_count = 1;
                if (device->pending_count >= device->config.max_batch_size) {
                    deliver_count = device->config.max_batch_size;
                }
                
                for (size_t i = 0; i < deliver_count; i++) {
                    pending_interrupt_t *pending = &device->pending_interrupts[i];
                    if (device->original_handler) {
                        device->original_handler(pending->context);
                    }
                }
                
                memmove(&device->pending_interrupts[0], 
                       &device->pending_interrupts[deliver_count],
                       (device->pending_count - deliver_count) * sizeof(pending_interrupt_t));
                device->pending_count -= deliver_count;
                
                device->stats.interrupts_delivered += deliver_count;
                coal_ctx.total_interrupts_delivered += deliver_count;
            }
            break;
        }
    }
    
    if (device->pending_count == 0) {
        device->timer_active = false;
    } else {
        device->coalescing_timer_deadline = current_time + 
            (device->config.timeout_us * coal_ctx.timer_frequency) / 1000000;
        device->timer_active = true;
    }
}

static void coalesced_interrupt_handler(void *context) {
    uint8_t vector = (uintptr_t)context;
    coalesced_device_t *device = find_device_by_vector(vector);
    
    if (!device || !device->enabled) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    device->stats.interrupts_received++;
    coal_ctx.total_interrupts_received++;
    
    if (device->pending_count >= MAX_PENDING_INTERRUPTS) {
        device->stats.interrupts_dropped++;
        return;
    }
    
    pending_interrupt_t *pending = &device->pending_interrupts[device->pending_count];
    pending->vector = vector;
    pending->timestamp = current_time;
    pending->context = device->handler_context;
    pending->original_handler = device->original_handler;
    device->pending_count++;
    
    bool should_deliver = false;
    
    switch (device->mode) {
        case COALESCING_MODE_COUNT:
            should_deliver = (device->pending_count >= device->config.max_batch_size);
            break;
            
        case COALESCING_MODE_TIME:
            if (!device->timer_active) {
                device->coalescing_timer_deadline = current_time + 
                    (device->config.timeout_us * coal_ctx.timer_frequency) / 1000000;
                device->timer_active = true;
            }
            break;
            
        case COALESCING_MODE_ADAPTIVE:
            should_deliver = (device->pending_count >= device->config.max_batch_size) ||
                           (current_time - device->last_delivery_time >= 
                            (device->config.timeout_us * coal_ctx.timer_frequency) / 1000000);
            break;
            
        case COALESCING_MODE_RATE_LIMITED:
            should_deliver = (current_time - device->last_delivery_time >= 
                            coal_ctx.timer_frequency / device->config.max_rate_hz);
            break;
    }
    
    if (should_deliver) {
        deliver_coalesced_interrupts(device);
    } else if (device->mode != COALESCING_MODE_TIME && !device->timer_active) {
        device->coalescing_timer_deadline = current_time + 
            (device->config.timeout_us * coal_ctx.timer_frequency) / 1000000;
        device->timer_active = true;
    }
}

static void coalescing_timer_handler(void *context) {
    uint64_t current_time = rdtsc();
    coal_ctx.total_timer_expirations++;
    
    for (size_t i = 0; i < coal_ctx.device_count; i++) {
        coalesced_device_t *device = &coal_ctx.devices[i];
        
        if (!device->enabled || !device->timer_active) {
            continue;
        }
        
        if (current_time >= device->coalescing_timer_deadline) {
            deliver_coalesced_interrupts(device);
        }
    }
}

interrupt_coalescing_error_t interrupt_coalescing_init(void) {
    memset(&coal_ctx, 0, sizeof(coal_ctx));
    
    if (timer_abstraction_get_frequency(&coal_ctx.timer_frequency) != TIMER_SUCCESS) {
        coal_ctx.timer_frequency = 1000000000ULL; // Default 1GHz
    }
    
    timer_config_t timer_config = {
        .mode = TIMER_MODE_PERIODIC,
        .frequency_hz = 10000, // 10kHz for 100μs resolution
        .callback = coalescing_timer_handler,
        .callback_data = NULL
    };
    
    if (timer_abstraction_create_timer(&timer_config, &coal_ctx.coalescing_timer) != TIMER_SUCCESS) {
        return INT_COALESCING_ERROR_TIMER_SETUP_FAILED;
    }
    
    interrupt_register_handler_coalescing(COALESCING_TIMER_VECTOR, coalescing_timer_handler, NULL);
    
    coal_ctx.initialized = true;
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_enable_device(
    uint8_t vector, 
    const coalescing_config_t *config) {
    
    if (!coal_ctx.initialized || !config) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    if (coal_ctx.device_count >= MAX_COALESCED_DEVICES) {
        return INT_COALESCING_ERROR_NO_SPACE;
    }
    
    if (config->timeout_us < MIN_COALESCING_TIMEOUT_US || 
        config->timeout_us > MAX_COALESCING_TIMEOUT_US) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    coalesced_device_t *device = find_device_by_vector(vector);
    if (device) {
        return INT_COALESCING_ERROR_ALREADY_ENABLED;
    }
    
    coalescing_handler_t original_handler;
    void *handler_context;
    if (interrupt_get_handler_coalescing(vector, &original_handler, &handler_context) != INT_SUCCESS) {
        return INT_COALESCING_ERROR_HANDLER_NOT_FOUND;
    }
    
    device = &coal_ctx.devices[coal_ctx.device_count];
    memset(device, 0, sizeof(coalesced_device_t));
    
    device->vector = vector;
    device->original_handler = original_handler;
    device->handler_context = handler_context;
    device->config = *config;
    device->mode = config->mode;
    device->enabled = true;
    
    interrupt_register_handler(vector, coalesced_interrupt_handler, (void*)(uintptr_t)vector);
    
    coal_ctx.device_count++;
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_disable_device(uint8_t vector) {
    if (!coal_ctx.initialized) {
        return INT_COALESCING_ERROR_NOT_INITIALIZED;
    }
    
    coalesced_device_t *device = find_device_by_vector(vector);
    if (!device) {
        return INT_COALESCING_ERROR_DEVICE_NOT_FOUND;
    }
    
    device->enabled = false;
    
    deliver_coalesced_interrupts(device);
    
    interrupt_register_handler(vector, device->original_handler, device->handler_context);
    
    for (size_t i = 0; i < coal_ctx.device_count; i++) {
        if (coal_ctx.devices[i].vector == vector) {
            memmove(&coal_ctx.devices[i], &coal_ctx.devices[i + 1],
                   (coal_ctx.device_count - i - 1) * sizeof(coalesced_device_t));
            coal_ctx.device_count--;
            break;
        }
    }
    
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_configure_device(
    uint8_t vector, 
    const coalescing_config_t *config) {
    
    if (!coal_ctx.initialized || !config) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    coalesced_device_t *device = find_device_by_vector(vector);
    if (!device) {
        return INT_COALESCING_ERROR_DEVICE_NOT_FOUND;
    }
    
    if (config->timeout_us < MIN_COALESCING_TIMEOUT_US || 
        config->timeout_us > MAX_COALESCING_TIMEOUT_US) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    device->config = *config;
    device->mode = config->mode;
    
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_force_delivery(uint8_t vector) {
    if (!coal_ctx.initialized) {
        return INT_COALESCING_ERROR_NOT_INITIALIZED;
    }
    
    coalesced_device_t *device = find_device_by_vector(vector);
    if (!device) {
        return INT_COALESCING_ERROR_DEVICE_NOT_FOUND;
    }
    
    deliver_coalesced_interrupts(device);
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_get_device_stats(
    uint8_t vector, 
    coalescing_device_stats_t *stats) {
    
    if (!coal_ctx.initialized || !stats) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    coalesced_device_t *device = find_device_by_vector(vector);
    if (!device) {
        return INT_COALESCING_ERROR_DEVICE_NOT_FOUND;
    }
    
    *stats = (coalescing_device_stats_t){
        .vector = device->vector,
        .mode = device->mode,
        .enabled = device->enabled,
        .pending_count = device->pending_count,
        .timer_active = device->timer_active,
        .stats = device->stats
    };
    
    if (device->stats.interrupts_received > 0) {
        stats->coalescing_ratio = (double)(device->stats.interrupts_received - 
                                         device->stats.interrupts_delivered) / 
                                (double)device->stats.interrupts_received;
    } else {
        stats->coalescing_ratio = 0.0;
    }
    
    return INT_COALESCING_SUCCESS;
}

interrupt_coalescing_error_t interrupt_coalescing_get_global_stats(
    coalescing_global_stats_t *stats) {
    
    if (!coal_ctx.initialized || !stats) {
        return INT_COALESCING_ERROR_INVALID_PARAMS;
    }
    
    *stats = (coalescing_global_stats_t){
        .enabled_devices = coal_ctx.device_count,
        .total_interrupts_received = coal_ctx.total_interrupts_received,
        .total_interrupts_delivered = coal_ctx.total_interrupts_delivered,
        .total_interrupts_coalesced = coal_ctx.total_interrupts_coalesced,
        .total_timer_expirations = coal_ctx.total_timer_expirations
    };
    
    if (coal_ctx.total_interrupts_received > 0) {
        stats->global_coalescing_ratio = 
            (double)(coal_ctx.total_interrupts_received - coal_ctx.total_interrupts_delivered) / 
            (double)coal_ctx.total_interrupts_received;
    } else {
        stats->global_coalescing_ratio = 0.0;
    }
    
    stats->average_batch_size = coal_ctx.total_interrupts_delivered > 0 ?
        (double)coal_ctx.total_interrupts_received / (double)coal_ctx.total_timer_expirations : 0.0;
    
    return INT_COALESCING_SUCCESS;
}

void interrupt_coalescing_process_timeouts(void) {
    if (!coal_ctx.initialized) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    
    for (size_t i = 0; i < coal_ctx.device_count; i++) {
        coalesced_device_t *device = &coal_ctx.devices[i];
        
        if (!device->enabled || !device->timer_active) {
            continue;
        }
        
        if (current_time >= device->coalescing_timer_deadline) {
            deliver_coalesced_interrupts(device);
        }
    }
}

bool interrupt_coalescing_is_enabled(uint8_t vector) {
    coalesced_device_t *device = find_device_by_vector(vector);
    return device && device->enabled;
}

bool interrupt_coalescing_is_initialized(void) {
    return coal_ctx.initialized;
}

size_t interrupt_coalescing_get_device_count(void) {
    return coal_ctx.device_count;
}

uint64_t interrupt_coalescing_get_total_savings(void) {
    return coal_ctx.total_interrupts_received - coal_ctx.total_interrupts_delivered;
}