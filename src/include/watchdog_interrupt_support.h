#ifndef WATCHDOG_INTERRUPT_SUPPORT_H
#define WATCHDOG_INTERRUPT_SUPPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t watchdog_id_t;

typedef enum {
    WATCHDOG_SUCCESS = 0,
    WATCHDOG_ERROR_INVALID_PARAMS,
    WATCHDOG_ERROR_NOT_INITIALIZED,
    WATCHDOG_ERROR_NOT_FOUND,
    WATCHDOG_ERROR_NO_SPACE,
    WATCHDOG_ERROR_INVALID_TIMEOUT,
    WATCHDOG_ERROR_INACTIVE,
    WATCHDOG_ERROR_TIMER_FAILED,
    WATCHDOG_ERROR_HARDWARE_FAULT
} watchdog_error_t;

typedef enum {
    WATCHDOG_ACTION_NONE = 0,
    WATCHDOG_ACTION_CALLBACK_ONLY,
    WATCHDOG_ACTION_RESET_SYSTEM,
    WATCHDOG_ACTION_PANIC,
    WATCHDOG_ACTION_NMI
} watchdog_action_t;

typedef enum {
    WATCHDOG_EVENT_CREATED = 0,
    WATCHDOG_EVENT_DESTROYED,
    WATCHDOG_EVENT_KICKED,
    WATCHDOG_EVENT_WARNING,
    WATCHDOG_EVENT_TIMEOUT,
    WATCHDOG_EVENT_SYSTEM_TIMEOUT,
    WATCHDOG_EVENT_SYSTEM_ENABLED,
    WATCHDOG_EVENT_SYSTEM_DISABLED,
    WATCHDOG_EVENT_SYSTEM_KICKED,
    WATCHDOG_EVENT_SYSTEM_RESET,
    WATCHDOG_EVENT_ALL
} watchdog_event_type_t;

typedef struct {
    uint32_t timeout_ms;
    uint32_t warning_timeout_ms;
    watchdog_action_t action;
    bool auto_reset_on_timeout;
    bool enable_warnings;
    char name[64];
} watchdog_config_t;

typedef struct {
    bool enable_system_watchdog;
    bool enable_nmi_watchdog;
    bool enable_system_reset;
    uint32_t system_timeout_ms;
    watchdog_action_t system_action;
    uint32_t max_watchdogs;
} watchdog_global_config_t;

typedef struct {
    watchdog_id_t id;
    bool active;
    bool expired;
    bool warning_fired;
    uint64_t time_since_kick_ms;
    uint64_t timeout_count;
    uint64_t warning_count;
    uint64_t creation_time;
    uint64_t last_kick_time;
} watchdog_status_t;

typedef struct {
    size_t active_watchdogs;
    uint64_t total_timeouts;
    uint64_t total_warnings;
    uint64_t total_kicks;
    uint64_t system_uptime_ms;
    bool system_watchdog_enabled;
    bool nmi_watchdog_enabled;
    size_t registered_handlers;
} watchdog_statistics_t;

typedef struct {
    watchdog_event_type_t type;
    watchdog_id_t watchdog_id;
    uint64_t timestamp;
    void *data;
} watchdog_event_t;

typedef void (*watchdog_callback_t)(watchdog_id_t id, watchdog_event_type_t event, void *data);
typedef void (*watchdog_handler_t)(const watchdog_event_t *event, void *user_data);

watchdog_error_t watchdog_interrupt_init(const watchdog_global_config_t *config);

watchdog_error_t watchdog_create(const watchdog_config_t *config, watchdog_id_t *id);

watchdog_error_t watchdog_destroy(watchdog_id_t id);

watchdog_error_t watchdog_kick(watchdog_id_t id);

watchdog_error_t watchdog_set_callbacks(watchdog_id_t id,
                                       watchdog_callback_t timeout_callback,
                                       watchdog_callback_t warning_callback,
                                       void *callback_data);

watchdog_error_t watchdog_register_handler(watchdog_handler_t handler, 
                                          void *user_data,
                                          watchdog_event_type_t event_filter);

watchdog_error_t watchdog_get_status(watchdog_id_t id, watchdog_status_t *status);

watchdog_error_t watchdog_get_statistics(watchdog_statistics_t *stats);

watchdog_error_t watchdog_enable_system_watchdog(bool enable);

watchdog_error_t watchdog_kick_system(void);

bool watchdog_is_initialized(void);

size_t watchdog_get_active_count(void);

uint64_t watchdog_get_system_uptime_ms(void);

static inline const char* watchdog_error_to_string(watchdog_error_t error) {
    switch (error) {
        case WATCHDOG_SUCCESS:
            return "Success";
        case WATCHDOG_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case WATCHDOG_ERROR_NOT_INITIALIZED:
            return "Watchdog not initialized";
        case WATCHDOG_ERROR_NOT_FOUND:
            return "Watchdog not found";
        case WATCHDOG_ERROR_NO_SPACE:
            return "No space for additional watchdogs";
        case WATCHDOG_ERROR_INVALID_TIMEOUT:
            return "Invalid timeout value";
        case WATCHDOG_ERROR_INACTIVE:
            return "Watchdog is inactive";
        case WATCHDOG_ERROR_TIMER_FAILED:
            return "Timer creation failed";
        case WATCHDOG_ERROR_HARDWARE_FAULT:
            return "Hardware fault detected";
        default:
            return "Unknown watchdog error";
    }
}

static inline const char* watchdog_action_to_string(watchdog_action_t action) {
    switch (action) {
        case WATCHDOG_ACTION_NONE:
            return "None";
        case WATCHDOG_ACTION_CALLBACK_ONLY:
            return "Callback Only";
        case WATCHDOG_ACTION_RESET_SYSTEM:
            return "Reset System";
        case WATCHDOG_ACTION_PANIC:
            return "Panic";
        case WATCHDOG_ACTION_NMI:
            return "NMI";
        default:
            return "Unknown";
    }
}

static inline const char* watchdog_event_to_string(watchdog_event_type_t event) {
    switch (event) {
        case WATCHDOG_EVENT_CREATED:
            return "Created";
        case WATCHDOG_EVENT_DESTROYED:
            return "Destroyed";
        case WATCHDOG_EVENT_KICKED:
            return "Kicked";
        case WATCHDOG_EVENT_WARNING:
            return "Warning";
        case WATCHDOG_EVENT_TIMEOUT:
            return "Timeout";
        case WATCHDOG_EVENT_SYSTEM_TIMEOUT:
            return "System Timeout";
        case WATCHDOG_EVENT_SYSTEM_ENABLED:
            return "System Enabled";
        case WATCHDOG_EVENT_SYSTEM_DISABLED:
            return "System Disabled";
        case WATCHDOG_EVENT_SYSTEM_KICKED:
            return "System Kicked";
        case WATCHDOG_EVENT_SYSTEM_RESET:
            return "System Reset";
        case WATCHDOG_EVENT_ALL:
            return "All Events";
        default:
            return "Unknown";
    }
}

static inline watchdog_config_t watchdog_default_config(uint32_t timeout_ms) {
    return (watchdog_config_t){
        .timeout_ms = timeout_ms,
        .warning_timeout_ms = timeout_ms / 2,
        .action = WATCHDOG_ACTION_CALLBACK_ONLY,
        .auto_reset_on_timeout = false,
        .enable_warnings = true,
        .name = "Default Watchdog"
    };
}

static inline watchdog_config_t watchdog_system_reset_config(uint32_t timeout_ms) {
    return (watchdog_config_t){
        .timeout_ms = timeout_ms,
        .warning_timeout_ms = timeout_ms * 3 / 4,
        .action = WATCHDOG_ACTION_RESET_SYSTEM,
        .auto_reset_on_timeout = false,
        .enable_warnings = true,
        .name = "System Reset Watchdog"
    };
}

static inline watchdog_config_t watchdog_critical_config(uint32_t timeout_ms) {
    return (watchdog_config_t){
        .timeout_ms = timeout_ms,
        .warning_timeout_ms = timeout_ms / 4,
        .action = WATCHDOG_ACTION_PANIC,
        .auto_reset_on_timeout = false,
        .enable_warnings = true,
        .name = "Critical Watchdog"
    };
}

static inline watchdog_global_config_t watchdog_default_global_config(void) {
    return (watchdog_global_config_t){
        .enable_system_watchdog = true,
        .enable_nmi_watchdog = false,
        .enable_system_reset = true,
        .system_timeout_ms = 60000,
        .system_action = WATCHDOG_ACTION_RESET_SYSTEM,
        .max_watchdogs = 16
    };
}

static inline watchdog_global_config_t watchdog_server_global_config(void) {
    return (watchdog_global_config_t){
        .enable_system_watchdog = true,
        .enable_nmi_watchdog = true,
        .enable_system_reset = true,
        .system_timeout_ms = 30000,
        .system_action = WATCHDOG_ACTION_RESET_SYSTEM,
        .max_watchdogs = 32
    };
}

static inline watchdog_global_config_t watchdog_development_global_config(void) {
    return (watchdog_global_config_t){
        .enable_system_watchdog = false,
        .enable_nmi_watchdog = false,
        .enable_system_reset = false,
        .system_timeout_ms = 300000,
        .system_action = WATCHDOG_ACTION_CALLBACK_ONLY,
        .max_watchdogs = 8
    };
}

#ifndef TIMER_ERROR_T_DEFINED
#define TIMER_ERROR_T_DEFINED
typedef enum {
    TIMER_SUCCESS = 0,
    TIMER_ERROR_FAILED
} timer_error_t;
#endif

#ifndef TIMER_MODE_T_DEFINED
#define TIMER_MODE_T_DEFINED
typedef enum {
    TIMER_MODE_PERIODIC = 0,
    TIMER_MODE_ONE_SHOT = 1
} timer_mode_t;
#endif

#ifndef TIMER_HANDLE_T_DEFINED
#define TIMER_HANDLE_T_DEFINED
typedef uint32_t timer_handle_t;
#endif

#ifndef TIMER_CONFIG_T_DEFINED
#define TIMER_CONFIG_T_DEFINED
typedef struct timer_config {
    timer_mode_t mode;
    uint32_t frequency_hz;
    void (*callback)(void *data);
    void *callback_data;
} timer_config_t;
#endif

extern uint64_t rdtsc(void);
extern uint64_t timer_get_frequency(void);
extern void panic(const char *format, ...);
extern void outb(uint16_t port, uint8_t value);
extern void local_apic_send_nmi_all(void);

extern timer_error_t timer_abstraction_create_timer(const timer_config_t *config, timer_handle_t *handle);
extern timer_error_t timer_abstraction_destroy_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_start_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_stop_timer(timer_handle_t handle);
extern timer_error_t timer_abstraction_reset_timer(timer_handle_t handle);

extern void interrupt_register_handler(uint8_t vector, void *handler, void *context);

#define WATCHDOG_KICK_INTERVAL_MS(timeout_ms) ((timeout_ms) / 4)

#define WATCHDOG_AUTO_KICK_TIMER(wd_id) \
    do { \
        static uint64_t last_kick = 0; \
        uint64_t now = rdtsc(); \
        if (now - last_kick > (timer_get_frequency() / 10)) { \
            watchdog_kick(wd_id); \
            last_kick = now; \
        } \
    } while(0)

static inline bool watchdog_should_warn(uint64_t elapsed_ms, uint32_t timeout_ms, uint32_t warning_ms) {
    return warning_ms > 0 && elapsed_ms >= warning_ms && elapsed_ms < timeout_ms;
}

static inline bool watchdog_is_expired(uint64_t elapsed_ms, uint32_t timeout_ms) {
    return elapsed_ms >= timeout_ms;
}

static inline uint32_t watchdog_remaining_time_ms(uint64_t last_kick_time, uint32_t timeout_ms) {
    uint64_t current_time = rdtsc();
    uint64_t elapsed_cycles = current_time - last_kick_time;
    uint64_t elapsed_ms = (elapsed_cycles * 1000) / timer_get_frequency();
    
    if (elapsed_ms >= timeout_ms) {
        return 0;
    }
    
    return timeout_ms - elapsed_ms;
}

#endif // WATCHDOG_INTERRUPT_SUPPORT_H