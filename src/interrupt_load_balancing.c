#include "interrupt_load_balancing.h"
#include "smp_interrupt_distribution.h"
#include "interrupt_latency_optimization.h"
#include "io_apic.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_LOAD_BALANCE_VECTORS 256
#define LOAD_BALANCE_INTERVAL_MS 100
#define LOAD_IMBALANCE_THRESHOLD 20
#define MIGRATION_COOLDOWN_MS 500
#define MAX_MIGRATIONS_PER_CYCLE 8

typedef struct {
    uint32_t cpu_id;
    uint64_t interrupt_count;
    uint64_t total_latency_ns;
    uint64_t last_update_time;
    uint32_t load_score;
    uint32_t active_vectors;
    bool online;
} cpu_load_info_t;

typedef struct {
    uint8_t vector;
    uint32_t current_cpu;
    uint64_t interrupt_count;
    uint64_t avg_latency_ns;
    uint64_t last_migration_time;
    uint32_t priority;
    bool real_time;
    bool migratable;
} vector_load_info_t;

typedef struct {
    cpu_load_info_t cpu_loads[MAX_CPU_COUNT];
    vector_load_info_t vector_loads[MAX_LOAD_BALANCE_VECTORS];
    size_t cpu_count;
    size_t vector_count;
    
    load_balance_config_t config;
    load_balance_algorithm_t algorithm;
    
    uint64_t total_migrations;
    uint64_t successful_migrations;
    uint64_t failed_migrations;
    uint64_t load_balance_cycles;
    uint64_t last_balance_time;
    
    timer_handle_t balance_timer;
    bool enabled;
    bool initialized;
} interrupt_load_balance_context_t;

static interrupt_load_balance_context_t lb_ctx = {0};

static uint32_t calculate_cpu_load_score(cpu_load_info_t *cpu) {
    if (!cpu->online) {
        return UINT32_MAX;
    }

    uint32_t base_score = cpu->interrupt_count / 1000;

    /* Compute average latency from total_latency_ns / interrupt_count */
    uint64_t avg_latency = cpu->interrupt_count > 0 ?
        (cpu->total_latency_ns / cpu->interrupt_count) : 0;
    if (avg_latency > lb_ctx.config.latency_threshold_ns) {
        base_score += 50;
    }
    
    base_score += cpu->active_vectors * 2;
    
    if (cpu->cpu_id == 0 && lb_ctx.config.avoid_cpu0) {
        base_score += 30;
    }
    
    return base_score;
}

static void update_cpu_load_metrics(void) {
    uint64_t current_time = rdtsc();
    
    for (size_t i = 0; i < lb_ctx.cpu_count; i++) {
        cpu_load_info_t *cpu = &lb_ctx.cpu_loads[i];
        
        if (!cpu->online) {
            continue;
        }
        
        smp_interrupt_stats_t smp_stats;
        if (smp_interrupt_get_statistics(&smp_stats) == SMP_INT_SUCCESS) {
            
        }
        
        cpu_interrupt_info_t cpu_info;
        if (smp_interrupt_get_cpu_info(cpu->cpu_id, &cpu_info) == SMP_INT_SUCCESS) {
            cpu->interrupt_count = cpu_info.interrupt_count;
            cpu->active_vectors = cpu_info.assigned_vectors;
        }
        
        cpu->load_score = calculate_cpu_load_score(cpu);
        cpu->last_update_time = current_time;
    }
}

static void update_vector_metrics(void) {
    for (size_t i = 0; i < lb_ctx.vector_count; i++) {
        vector_load_info_t *vector = &lb_ctx.vector_loads[i];
        
        interrupt_latency_stats_t latency_stats;
        if (interrupt_latency_get_statistics(vector->vector, &latency_stats) == INT_LATENCY_SUCCESS) {
            vector->avg_latency_ns = latency_stats.avg_latency_ns;
        }
    }
}

static uint32_t find_least_loaded_cpu(uint32_t exclude_cpu) {
    uint32_t best_cpu = UINT32_MAX;
    uint32_t best_score = UINT32_MAX;
    
    for (size_t i = 0; i < lb_ctx.cpu_count; i++) {
        cpu_load_info_t *cpu = &lb_ctx.cpu_loads[i];
        
        if (i == exclude_cpu || !cpu->online) {
            continue;
        }
        
        if (cpu->load_score < best_score) {
            best_score = cpu->load_score;
            best_cpu = i;
        }
    }
    
    return best_cpu;
}

static uint32_t find_most_loaded_cpu(void) {
    uint32_t worst_cpu = UINT32_MAX;
    uint32_t worst_score = 0;
    
    for (size_t i = 0; i < lb_ctx.cpu_count; i++) {
        cpu_load_info_t *cpu = &lb_ctx.cpu_loads[i];
        
        if (!cpu->online) {
            continue;
        }
        
        if (cpu->load_score > worst_score) {
            worst_score = cpu->load_score;
            worst_cpu = i;
        }
    }
    
    return worst_cpu;
}

static bool should_migrate_vector(vector_load_info_t *vector, uint32_t from_cpu, uint32_t to_cpu) {
    if (!vector->migratable || vector->real_time) {
        return false;
    }
    
    uint64_t current_time = rdtsc();
    uint64_t cooldown_cycles = (lb_ctx.config.migration_cooldown_ms * timer_get_frequency()) / 1000;
    
    if (current_time - vector->last_migration_time < cooldown_cycles) {
        return false;
    }
    
    cpu_load_info_t *from = &lb_ctx.cpu_loads[from_cpu];
    cpu_load_info_t *to = &lb_ctx.cpu_loads[to_cpu];
    
    if (from->load_score - to->load_score < lb_ctx.config.load_threshold) {
        return false;
    }
    
    return true;
}

static load_balance_error_t migrate_vector(uint8_t vector, uint32_t from_cpu, uint32_t to_cpu) {
    interrupt_affinity_t affinity = create_cpu_affinity(to_cpu);
    
    if (smp_interrupt_set_affinity(vector, &affinity) != SMP_INT_SUCCESS) {
        lb_ctx.failed_migrations++;
        return LOAD_BALANCE_ERROR_MIGRATION_FAILED;
    }
    
    vector_load_info_t *vector_info = NULL;
    for (size_t i = 0; i < lb_ctx.vector_count; i++) {
        if (lb_ctx.vector_loads[i].vector == vector) {
            vector_info = &lb_ctx.vector_loads[i];
            break;
        }
    }
    
    if (vector_info) {
        vector_info->current_cpu = to_cpu;
        vector_info->last_migration_time = rdtsc();
    }
    
    lb_ctx.cpu_loads[from_cpu].active_vectors--;
    lb_ctx.cpu_loads[to_cpu].active_vectors++;
    
    lb_ctx.total_migrations++;
    lb_ctx.successful_migrations++;
    
    return LOAD_BALANCE_SUCCESS;
}

static void round_robin_balance(void) {
    static uint32_t last_target_cpu = 0;
    
    uint32_t migrations_this_cycle = 0;
    
    for (size_t i = 0; i < lb_ctx.vector_count && migrations_this_cycle < MAX_MIGRATIONS_PER_CYCLE; i++) {
        vector_load_info_t *vector = &lb_ctx.vector_loads[i];
        
        if (!vector->migratable) {
            continue;
        }
        
        uint32_t target_cpu = (last_target_cpu + 1) % lb_ctx.cpu_count;
        while (!lb_ctx.cpu_loads[target_cpu].online) {
            target_cpu = (target_cpu + 1) % lb_ctx.cpu_count;
        }
        
        if (target_cpu != vector->current_cpu) {
            if (migrate_vector(vector->vector, vector->current_cpu, target_cpu) == LOAD_BALANCE_SUCCESS) {
                migrations_this_cycle++;
                last_target_cpu = target_cpu;
            }
        }
    }
}

static void load_based_balance(void) {
    uint32_t migrations_this_cycle = 0;
    
    while (migrations_this_cycle < MAX_MIGRATIONS_PER_CYCLE) {
        uint32_t most_loaded = find_most_loaded_cpu();
        uint32_t least_loaded = find_least_loaded_cpu(most_loaded);
        
        if (most_loaded == UINT32_MAX || least_loaded == UINT32_MAX) {
            break;
        }
        
        cpu_load_info_t *from = &lb_ctx.cpu_loads[most_loaded];
        cpu_load_info_t *to = &lb_ctx.cpu_loads[least_loaded];
        
        if (from->load_score - to->load_score < lb_ctx.config.load_threshold) {
            break;
        }
        
        vector_load_info_t *best_candidate = NULL;
        for (size_t i = 0; i < lb_ctx.vector_count; i++) {
            vector_load_info_t *vector = &lb_ctx.vector_loads[i];
            
            if (vector->current_cpu != most_loaded) {
                continue;
            }
            
            if (should_migrate_vector(vector, most_loaded, least_loaded)) {
                if (!best_candidate || vector->interrupt_count < best_candidate->interrupt_count) {
                    best_candidate = vector;
                }
            }
        }
        
        if (!best_candidate) {
            break;
        }
        
        if (migrate_vector(best_candidate->vector, most_loaded, least_loaded) == LOAD_BALANCE_SUCCESS) {
            migrations_this_cycle++;
            from->load_score = calculate_cpu_load_score(from);
            to->load_score = calculate_cpu_load_score(to);
        } else {
            break;
        }
    }
}

static void latency_aware_balance(void) {
    uint32_t migrations_this_cycle = 0;
    
    for (size_t i = 0; i < lb_ctx.vector_count && migrations_this_cycle < MAX_MIGRATIONS_PER_CYCLE; i++) {
        vector_load_info_t *vector = &lb_ctx.vector_loads[i];
        
        if (!vector->migratable || vector->avg_latency_ns < lb_ctx.config.latency_threshold_ns) {
            continue;
        }
        
        uint32_t current_cpu = vector->current_cpu;
        uint32_t best_cpu = find_least_loaded_cpu(current_cpu);
        
        if (best_cpu != UINT32_MAX) {
            cpu_load_info_t *current = &lb_ctx.cpu_loads[current_cpu];
            cpu_load_info_t *target = &lb_ctx.cpu_loads[best_cpu];
            
            if (target->load_score + 10 < current->load_score) {
                if (migrate_vector(vector->vector, current_cpu, best_cpu) == LOAD_BALANCE_SUCCESS) {
                    migrations_this_cycle++;
                }
            }
        }
    }
}

static void perform_load_balancing(void) {
    if (!lb_ctx.enabled || lb_ctx.cpu_count < 2) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    uint64_t interval_cycles = (lb_ctx.config.balance_interval_ms * timer_get_frequency()) / 1000;
    
    if (current_time - lb_ctx.last_balance_time < interval_cycles) {
        return;
    }
    
    update_cpu_load_metrics();
    update_vector_metrics();
    
    switch (lb_ctx.algorithm) {
        case LOAD_BALANCE_ROUND_ROBIN:
            round_robin_balance();
            break;
            
        case LOAD_BALANCE_LOAD_BASED:
            load_based_balance();
            break;
            
        case LOAD_BALANCE_LATENCY_AWARE:
            latency_aware_balance();
            break;
            
        case LOAD_BALANCE_ADAPTIVE:
            if (lb_ctx.load_balance_cycles % 10 < 7) {
                load_based_balance();
            } else {
                latency_aware_balance();
            }
            break;
    }
    
    lb_ctx.last_balance_time = current_time;
    lb_ctx.load_balance_cycles++;
}

static void load_balance_timer_callback(void *data) {
    perform_load_balancing();
}

load_balance_error_t interrupt_load_balance_init(const load_balance_config_t *config) {
    if (!config) {
        return LOAD_BALANCE_ERROR_INVALID_PARAMS;
    }
    
    memset(&lb_ctx, 0, sizeof(lb_ctx));
    lb_ctx.config = *config;
    lb_ctx.algorithm = config->algorithm;
    lb_ctx.cpu_count = smp_interrupt_get_cpu_count();
    
    if (lb_ctx.cpu_count > MAX_CPU_COUNT) {
        lb_ctx.cpu_count = MAX_CPU_COUNT;
    }
    
    for (size_t i = 0; i < lb_ctx.cpu_count; i++) {
        lb_ctx.cpu_loads[i].cpu_id = i;
        lb_ctx.cpu_loads[i].online = true;
    }
    
    timer_config_t timer_config = {
        .mode = TIMER_MODE_PERIODIC,
        .frequency_hz = 1000 / config->balance_interval_ms,
        .callback = load_balance_timer_callback,
        .callback_data = NULL
    };
    
    if (timer_abstraction_create_timer(&timer_config, &lb_ctx.balance_timer) != TIMER_SUCCESS) {
        return LOAD_BALANCE_ERROR_TIMER_FAILED;
    }
    
    lb_ctx.enabled = config->enabled;
    lb_ctx.initialized = true;
    
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_register_vector(uint8_t vector, 
                                                          const vector_balance_config_t *config) {
    if (!lb_ctx.initialized || !config) {
        return LOAD_BALANCE_ERROR_INVALID_PARAMS;
    }
    
    if (lb_ctx.vector_count >= MAX_LOAD_BALANCE_VECTORS) {
        return LOAD_BALANCE_ERROR_NO_SPACE;
    }
    
    vector_load_info_t *vector_info = &lb_ctx.vector_loads[lb_ctx.vector_count];
    vector_info->vector = vector;
    vector_info->current_cpu = config->initial_cpu;
    vector_info->priority = config->priority;
    vector_info->real_time = config->real_time;
    vector_info->migratable = config->migratable;
    vector_info->last_migration_time = 0;
    vector_info->interrupt_count = 0;
    vector_info->avg_latency_ns = 0;
    
    lb_ctx.vector_count++;
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_set_algorithm(load_balance_algorithm_t algorithm) {
    if (!lb_ctx.initialized) {
        return LOAD_BALANCE_ERROR_NOT_INITIALIZED;
    }
    
    lb_ctx.algorithm = algorithm;
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_enable(bool enable) {
    if (!lb_ctx.initialized) {
        return LOAD_BALANCE_ERROR_NOT_INITIALIZED;
    }
    
    lb_ctx.enabled = enable;
    
    if (enable) {
        timer_abstraction_start_timer(lb_ctx.balance_timer);
    } else {
        timer_abstraction_stop_timer(lb_ctx.balance_timer);
    }
    
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_force_balance(void) {
    if (!lb_ctx.initialized) {
        return LOAD_BALANCE_ERROR_NOT_INITIALIZED;
    }
    
    perform_load_balancing();
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_get_statistics(load_balance_stats_t *stats) {
    if (!lb_ctx.initialized || !stats) {
        return LOAD_BALANCE_ERROR_INVALID_PARAMS;
    }
    
    *stats = (load_balance_stats_t){
        .total_migrations = lb_ctx.total_migrations,
        .successful_migrations = lb_ctx.successful_migrations,
        .failed_migrations = lb_ctx.failed_migrations,
        .load_balance_cycles = lb_ctx.load_balance_cycles,
        .enabled = lb_ctx.enabled,
        .algorithm = lb_ctx.algorithm,
        .managed_vectors = lb_ctx.vector_count,
        .active_cpus = lb_ctx.cpu_count
    };
    
    if (lb_ctx.total_migrations > 0) {
        stats->success_rate = (double)lb_ctx.successful_migrations / (double)lb_ctx.total_migrations;
    } else {
        stats->success_rate = 0.0;
    }
    
    uint32_t total_load = 0;
    uint32_t max_load = 0;
    uint32_t min_load = UINT32_MAX;
    
    for (size_t i = 0; i < lb_ctx.cpu_count; i++) {
        if (lb_ctx.cpu_loads[i].online) {
            uint32_t load = lb_ctx.cpu_loads[i].load_score;
            total_load += load;
            if (load > max_load) max_load = load;
            if (load < min_load) min_load = load;
        }
    }
    
    if (lb_ctx.cpu_count > 0) {
        stats->avg_cpu_load = total_load / lb_ctx.cpu_count;
        stats->load_imbalance_ratio = max_load > 0 ? (double)min_load / (double)max_load : 1.0;
    }
    
    return LOAD_BALANCE_SUCCESS;
}

load_balance_error_t interrupt_load_balance_get_cpu_loads(cpu_load_stats_t *loads, size_t max_cpus) {
    if (!lb_ctx.initialized || !loads) {
        return LOAD_BALANCE_ERROR_INVALID_PARAMS;
    }
    
    size_t copy_count = lb_ctx.cpu_count < max_cpus ? lb_ctx.cpu_count : max_cpus;
    
    for (size_t i = 0; i < copy_count; i++) {
        cpu_load_info_t *cpu = &lb_ctx.cpu_loads[i];
        loads[i] = (cpu_load_stats_t){
            .cpu_id = cpu->cpu_id,
            .load_score = cpu->load_score,
            .interrupt_count = cpu->interrupt_count,
            .active_vectors = cpu->active_vectors,
            .avg_latency_ns = cpu->total_latency_ns / (cpu->interrupt_count > 0 ? cpu->interrupt_count : 1),
            .online = cpu->online
        };
    }
    
    return LOAD_BALANCE_SUCCESS;
}

bool interrupt_load_balance_is_enabled(void) {
    return lb_ctx.enabled;
}

bool interrupt_load_balance_is_initialized(void) {
    return lb_ctx.initialized;
}

size_t interrupt_load_balance_get_managed_vectors(void) {
    return lb_ctx.vector_count;
}

uint64_t interrupt_load_balance_get_total_migrations(void) {
    return lb_ctx.total_migrations;
}