#include "watchdog_interrupt_support.h"
#include "interrupt_management.h"
#include "timer_abstraction.h"
#include "apic.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define WATCHDOG_VECTOR 0xDF
#define MAX_WATCHDOG_HANDLERS 32
#define MAX_WATCHDOG_TIMERS 16
#define WATCHDOG_MIN_TIMEOUT_MS 100
#define WATCHDOG_MAX_TIMEOUT_MS 300000
#define WATCHDOG_NMI_VECTOR 0x02

typedef struct {
    watchdog_id_t id;
    watchdog_config_t config;
    timer_handle_t timer_handle;
    
    watchdog_callback_t timeout_callback;
    watchdog_callback_t warning_callback;
    void *callback_data;
    
    uint64_t last_kick_time;
    uint64_t timeout_count;
    uint64_t warning_count;
    uint64_t creation_time;
    
    bool active;
    bool expired;
    bool warning_fired;
} watchdog_timer_t;

typedef struct {
    watchdog_handler_t handler;
    void *user_data;
    watchdog_event_type_t event_filter;
    bool enabled;
} watchdog_handler_entry_t;

typedef struct {
    watchdog_timer_t timers[MAX_WATCHDOG_TIMERS];
    size_t timer_count;
    watchdog_id_t next_id;
    
    watchdog_handler_entry_t handlers[MAX_WATCHDOG_HANDLERS];
    size_t handler_count;
    
    watchdog_global_config_t global_config;
    
    uint64_t total_timeouts;
    uint64_t total_warnings;
    uint64_t total_kicks;
    uint64_t system_boot_time;
    
    timer_handle_t system_watchdog;
    bool system_watchdog_enabled;
    bool nmi_watchdog_enabled;
    
    bool initialized;
} watchdog_interrupt_context_t;

static watchdog_interrupt_context_t wd_ctx = {0};

static watchdog_timer_t* find_watchdog_by_id(watchdog_id_t id) {
    for (size_t i = 0; i < wd_ctx.timer_count; i++) {
        if (wd_ctx.timers[i].id == id && wd_ctx.timers[i].active) {
            return &wd_ctx.timers[i];
        }
    }
    return NULL;
}

static void notify_watchdog_handlers(watchdog_event_type_t event_type, 
                                   watchdog_id_t id, void *event_data) {
    for (size_t i = 0; i < wd_ctx.handler_count; i++) {
        watchdog_handler_entry_t *handler = &wd_ctx.handlers[i];
        if (handler->enabled && 
            (handler->event_filter == WATCHDOG_EVENT_ALL || 
             handler->event_filter == event_type)) {
            
            watchdog_event_t event = {
                .type = event_type,
                .watchdog_id = id,
                .timestamp = rdtsc(),
                .data = event_data
            };
            
            handler->handler(&event, handler->user_data);
        }
    }
}

static void watchdog_timer_callback(void *data) {
    watchdog_id_t id = (watchdog_id_t)(uintptr_t)data;
    watchdog_timer_t *watchdog = find_watchdog_by_id(id);
    
    if (!watchdog || !watchdog->active) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    uint64_t elapsed_cycles = current_time - watchdog->last_kick_time;
    uint64_t elapsed_ms = (elapsed_cycles * 1000) / timer_get_frequency();
    
    if (watchdog->config.warning_timeout_ms > 0 && 
        !watchdog->warning_fired && 
        elapsed_ms >= watchdog->config.warning_timeout_ms) {
        
        watchdog->warning_fired = true;
        watchdog->warning_count++;
        wd_ctx.total_warnings++;
        
        if (watchdog->warning_callback) {
            watchdog->warning_callback(id, WATCHDOG_EVENT_WARNING, watchdog->callback_data);
        }
        
        notify_watchdog_handlers(WATCHDOG_EVENT_WARNING, id, &elapsed_ms);
    }
    
    if (elapsed_ms >= watchdog->config.timeout_ms) {
        watchdog->expired = true;
        watchdog->timeout_count++;
        wd_ctx.total_timeouts++;
        
        watchdog_action_t action = watchdog->config.action;
        
        if (watchdog->timeout_callback) {
            watchdog->timeout_callback(id, WATCHDOG_EVENT_TIMEOUT, watchdog->callback_data);
        }
        
        notify_watchdog_handlers(WATCHDOG_EVENT_TIMEOUT, id, &elapsed_ms);
        
        switch (action) {
            case WATCHDOG_ACTION_NONE:
                break;
                
            case WATCHDOG_ACTION_CALLBACK_ONLY:
                break;
                
            case WATCHDOG_ACTION_RESET_SYSTEM:
                if (wd_ctx.global_config.enable_system_reset) {
                    notify_watchdog_handlers(WATCHDOG_EVENT_SYSTEM_RESET, id, NULL);
                    
                    outb(0x64, 0xFE);
                    
                    outb(0xCF9, 0x02);
                    outb(0xCF9, 0x06);
                    
                    while (1) {
                        __asm__ volatile ("cli; hlt");
                    }
                }
                break;
                
            case WATCHDOG_ACTION_PANIC:
                panic("Watchdog timeout: ID %u, elapsed %lu ms", id, elapsed_ms);
                break;
                
            case WATCHDOG_ACTION_NMI:
                if (wd_ctx.nmi_watchdog_enabled) {
                    local_apic_send_nmi_all();
                }
                break;
        }
        
        if (watchdog->config.auto_reset_on_timeout) {
            watchdog->last_kick_time = current_time;
            watchdog->warning_fired = false;
            watchdog->expired = false;
        } else {
            watchdog->active = false;
        }
    }
}

static void system_watchdog_callback(void *data) {
    if (!wd_ctx.system_watchdog_enabled) {
        return;
    }
    
    notify_watchdog_handlers(WATCHDOG_EVENT_SYSTEM_TIMEOUT, 0, NULL);
    
    if (wd_ctx.global_config.system_action == WATCHDOG_ACTION_RESET_SYSTEM) {
        outb(0x64, 0xFE);
        outb(0xCF9, 0x06);
    } else if (wd_ctx.global_config.system_action == WATCHDOG_ACTION_PANIC) {
        panic("System watchdog timeout");
    }
}

watchdog_error_t watchdog_interrupt_init(const watchdog_global_config_t *config) {
    if (!config) {
        return WATCHDOG_ERROR_INVALID_PARAMS;
    }
    
    memset(&wd_ctx, 0, sizeof(wd_ctx));
    wd_ctx.global_config = *config;
    wd_ctx.next_id = 1;
    wd_ctx.system_boot_time = rdtsc();
    
    interrupt_register_handler(WATCHDOG_VECTOR, NULL, NULL);
    
    if (config->enable_system_watchdog) {
        timer_config_t timer_config = {
            .mode = TIMER_MODE_PERIODIC,
            .frequency_hz = 1000 / config->system_timeout_ms,
            .callback = system_watchdog_callback,
            .callback_data = NULL
        };
        
        if (timer_abstraction_create_timer(&timer_config, &wd_ctx.system_watchdog) == TIMER_SUCCESS) {
            wd_ctx.system_watchdog_enabled = true;
        }
    }
    
    if (config->enable_nmi_watchdog) {
        wd_ctx.nmi_watchdog_enabled = true;
    }
    
    wd_ctx.initialized = true;
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_create(const watchdog_config_t *config, watchdog_id_t *id) {
    if (!wd_ctx.initialized || !config || !id) {
        return WATCHDOG_ERROR_INVALID_PARAMS;
    }
    
    if (config->timeout_ms < WATCHDOG_MIN_TIMEOUT_MS || 
        config->timeout_ms > WATCHDOG_MAX_TIMEOUT_MS) {
        return WATCHDOG_ERROR_INVALID_TIMEOUT;
    }
    
    if (wd_ctx.timer_count >= MAX_WATCHDOG_TIMERS) {
        return WATCHDOG_ERROR_NO_SPACE;
    }
    
    watchdog_timer_t *watchdog = &wd_ctx.timers[wd_ctx.timer_count];
    memset(watchdog, 0, sizeof(watchdog_timer_t));
    
    watchdog->id = wd_ctx.next_id++;
    watchdog->config = *config;
    watchdog->creation_time = rdtsc();
    watchdog->last_kick_time = watchdog->creation_time;
    watchdog->active = true;
    
    timer_config_t timer_config = {
        .mode = TIMER_MODE_PERIODIC,
        .frequency_hz = 100,
        .callback = watchdog_timer_callback,
        .callback_data = (void*)(uintptr_t)watchdog->id
    };
    
    if (timer_abstraction_create_timer(&timer_config, &watchdog->timer_handle) != TIMER_SUCCESS) {
        return WATCHDOG_ERROR_TIMER_FAILED;
    }
    
    *id = watchdog->id;
    wd_ctx.timer_count++;
    
    notify_watchdog_handlers(WATCHDOG_EVENT_CREATED, watchdog->id, NULL);
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_destroy(watchdog_id_t id) {
    if (!wd_ctx.initialized) {
        return WATCHDOG_ERROR_NOT_INITIALIZED;
    }
    
    watchdog_timer_t *watchdog = find_watchdog_by_id(id);
    if (!watchdog) {
        return WATCHDOG_ERROR_NOT_FOUND;
    }
    
    watchdog->active = false;
    timer_abstraction_destroy_timer(watchdog->timer_handle);
    
    notify_watchdog_handlers(WATCHDOG_EVENT_DESTROYED, id, NULL);
    
    for (size_t i = 0; i < wd_ctx.timer_count; i++) {
        if (wd_ctx.timers[i].id == id) {
            memmove(&wd_ctx.timers[i], &wd_ctx.timers[i + 1],
                   (wd_ctx.timer_count - i - 1) * sizeof(watchdog_timer_t));
            wd_ctx.timer_count--;
            break;
        }
    }
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_kick(watchdog_id_t id) {
    if (!wd_ctx.initialized) {
        return WATCHDOG_ERROR_NOT_INITIALIZED;
    }
    
    watchdog_timer_t *watchdog = find_watchdog_by_id(id);
    if (!watchdog) {
        return WATCHDOG_ERROR_NOT_FOUND;
    }
    
    if (!watchdog->active) {
        return WATCHDOG_ERROR_INACTIVE;
    }
    
    watchdog->last_kick_time = rdtsc();
    watchdog->warning_fired = false;
    watchdog->expired = false;
    wd_ctx.total_kicks++;
    
    notify_watchdog_handlers(WATCHDOG_EVENT_KICKED, id, NULL);
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_set_callbacks(watchdog_id_t id,
                                       watchdog_callback_t timeout_callback,
                                       watchdog_callback_t warning_callback,
                                       void *callback_data) {
    if (!wd_ctx.initialized) {
        return WATCHDOG_ERROR_NOT_INITIALIZED;
    }
    
    watchdog_timer_t *watchdog = find_watchdog_by_id(id);
    if (!watchdog) {
        return WATCHDOG_ERROR_NOT_FOUND;
    }
    
    watchdog->timeout_callback = timeout_callback;
    watchdog->warning_callback = warning_callback;
    watchdog->callback_data = callback_data;
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_register_handler(watchdog_handler_t handler, 
                                          void *user_data,
                                          watchdog_event_type_t event_filter) {
    if (!wd_ctx.initialized || !handler) {
        return WATCHDOG_ERROR_INVALID_PARAMS;
    }
    
    if (wd_ctx.handler_count >= MAX_WATCHDOG_HANDLERS) {
        return WATCHDOG_ERROR_NO_SPACE;
    }
    
    watchdog_handler_entry_t *entry = &wd_ctx.handlers[wd_ctx.handler_count];
    entry->handler = handler;
    entry->user_data = user_data;
    entry->event_filter = event_filter;
    entry->enabled = true;
    
    wd_ctx.handler_count++;
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_get_status(watchdog_id_t id, watchdog_status_t *status) {
    if (!wd_ctx.initialized || !status) {
        return WATCHDOG_ERROR_INVALID_PARAMS;
    }
    
    watchdog_timer_t *watchdog = find_watchdog_by_id(id);
    if (!watchdog) {
        return WATCHDOG_ERROR_NOT_FOUND;
    }
    
    uint64_t current_time = rdtsc();
    uint64_t elapsed_cycles = current_time - watchdog->last_kick_time;
    uint64_t elapsed_ms = (elapsed_cycles * 1000) / timer_get_frequency();
    
    *status = (watchdog_status_t){
        .id = watchdog->id,
        .active = watchdog->active,
        .expired = watchdog->expired,
        .warning_fired = watchdog->warning_fired,
        .time_since_kick_ms = elapsed_ms,
        .timeout_count = watchdog->timeout_count,
        .warning_count = watchdog->warning_count,
        .creation_time = watchdog->creation_time,
        .last_kick_time = watchdog->last_kick_time
    };
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_get_statistics(watchdog_statistics_t *stats) {
    if (!wd_ctx.initialized || !stats) {
        return WATCHDOG_ERROR_INVALID_PARAMS;
    }
    
    *stats = (watchdog_statistics_t){
        .active_watchdogs = wd_ctx.timer_count,
        .total_timeouts = wd_ctx.total_timeouts,
        .total_warnings = wd_ctx.total_warnings,
        .total_kicks = wd_ctx.total_kicks,
        .system_uptime_ms = (rdtsc() - wd_ctx.system_boot_time) * 1000 / timer_get_frequency(),
        .system_watchdog_enabled = wd_ctx.system_watchdog_enabled,
        .nmi_watchdog_enabled = wd_ctx.nmi_watchdog_enabled,
        .registered_handlers = wd_ctx.handler_count
    };
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_enable_system_watchdog(bool enable) {
    if (!wd_ctx.initialized) {
        return WATCHDOG_ERROR_NOT_INITIALIZED;
    }
    
    wd_ctx.system_watchdog_enabled = enable;
    
    if (enable && wd_ctx.system_watchdog != 0) {
        timer_abstraction_start_timer(wd_ctx.system_watchdog);
    } else if (!enable && wd_ctx.system_watchdog != 0) {
        timer_abstraction_stop_timer(wd_ctx.system_watchdog);
    }
    
    notify_watchdog_handlers(enable ? WATCHDOG_EVENT_SYSTEM_ENABLED : WATCHDOG_EVENT_SYSTEM_DISABLED, 
                           0, NULL);
    
    return WATCHDOG_SUCCESS;
}

watchdog_error_t watchdog_kick_system(void) {
    if (!wd_ctx.initialized || !wd_ctx.system_watchdog_enabled) {
        return WATCHDOG_ERROR_NOT_INITIALIZED;
    }
    
    timer_abstraction_reset_timer(wd_ctx.system_watchdog);
    notify_watchdog_handlers(WATCHDOG_EVENT_SYSTEM_KICKED, 0, NULL);
    
    return WATCHDOG_SUCCESS;
}

bool watchdog_is_initialized(void) {
    return wd_ctx.initialized;
}

size_t watchdog_get_active_count(void) {
    return wd_ctx.timer_count;
}

uint64_t watchdog_get_system_uptime_ms(void) {
    if (!wd_ctx.initialized) {
        return 0;
    }
    
    return (rdtsc() - wd_ctx.system_boot_time) * 1000 / timer_get_frequency();
}