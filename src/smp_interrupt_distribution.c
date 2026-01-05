#include "smp_interrupt_distribution.h"
#include "interrupt_management.h"
#include "local_apic.h"
#include "io_apic.h"
#include "acpi_interrupt_routing.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_CPU_COUNT 256
#define MAX_INTERRUPT_VECTORS 256
#define SMP_LOAD_BALANCE_THRESHOLD 10
#define SMP_MIGRATION_COOLDOWN_MS 100

typedef struct {
    uint32_t cpu_id;
    uint32_t local_apic_id;
    bool online;
    bool enabled;
    uint64_t interrupt_count;
    uint64_t last_migration_time;
    uint32_t current_load;
    cpu_feature_set_t features;
} cpu_info_t;

typedef struct {
    uint8_t vector;
    uint32_t assigned_cpu;
    uint32_t io_apic_id;
    uint8_t io_apic_pin;
    interrupt_distribution_mode_t mode;
    uint32_t round_robin_next;
    uint64_t interrupt_count;
    uint32_t priority;
    bool real_time_critical;
} interrupt_routing_entry_t;

typedef struct {
    cpu_info_t cpus[MAX_CPU_COUNT];
    size_t cpu_count;
    size_t online_cpu_count;
    uint32_t bootstrap_processor;
    
    interrupt_routing_entry_t routing_table[MAX_INTERRUPT_VECTORS];
    size_t routing_entry_count;
    
    smp_distribution_config_t config;
    
    uint64_t total_interrupts_distributed;
    uint64_t load_balance_operations;
    uint64_t migration_operations;
    
    bool load_balancing_enabled;
    bool distribution_enabled;
    bool initialized;
} smp_interrupt_context_t;

static smp_interrupt_context_t smp_ctx = {0};

static uint32_t get_cpu_load_score(uint32_t cpu_id) {
    if (cpu_id >= smp_ctx.cpu_count) {
        return UINT32_MAX;
    }
    
    cpu_info_t *cpu = &smp_ctx.cpus[cpu_id];
    if (!cpu->online || !cpu->enabled) {
        return UINT32_MAX;
    }
    
    uint32_t base_score = cpu->current_load;
    
    base_score += cpu->interrupt_count / 1000;
    
    if (cpu->features.supports_hyperthreading && 
        smp_ctx.config.avoid_hyperthreading) {
        base_score += 50;
    }
    
    if (cpu->features.cache_level == CACHE_L1_ONLY) {
        base_score += 20;
    }
    
    return base_score;
}

static uint32_t select_least_loaded_cpu(uint32_t exclude_cpu) {
    uint32_t best_cpu = UINT32_MAX;
    uint32_t best_score = UINT32_MAX;
    
    for (size_t i = 0; i < smp_ctx.cpu_count; i++) {
        if (i == exclude_cpu) continue;
        
        uint32_t score = get_cpu_load_score(i);
        if (score < best_score) {
            best_score = score;
            best_cpu = i;
        }
    }
    
    return best_cpu;
}

static uint32_t select_round_robin_cpu(interrupt_routing_entry_t *entry) {
    size_t attempts = 0;
    uint32_t start_cpu = entry->round_robin_next;
    
    while (attempts < smp_ctx.online_cpu_count) {
        uint32_t candidate = (start_cpu + attempts) % smp_ctx.cpu_count;
        
        if (smp_ctx.cpus[candidate].online && smp_ctx.cpus[candidate].enabled) {
            entry->round_robin_next = (candidate + 1) % smp_ctx.cpu_count;
            return candidate;
        }
        attempts++;
    }
    
    return smp_ctx.bootstrap_processor;
}

static uint32_t select_cpu_for_interrupt(uint8_t vector, 
                                       interrupt_distribution_mode_t mode) {
    interrupt_routing_entry_t *entry = NULL;
    
    for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
        if (smp_ctx.routing_table[i].vector == vector) {
            entry = &smp_ctx.routing_table[i];
            break;
        }
    }
    
    if (!entry) {
        return smp_ctx.bootstrap_processor;
    }
    
    switch (mode) {
        case DIST_MODE_FIXED:
            return entry->assigned_cpu;
            
        case DIST_MODE_ROUND_ROBIN:
            return select_round_robin_cpu(entry);
            
        case DIST_MODE_LOAD_BALANCED:
            return select_least_loaded_cpu(UINT32_MAX);
            
        case DIST_MODE_LOWEST_PRIORITY:
            return select_least_loaded_cpu(UINT32_MAX);
            
        case DIST_MODE_REAL_TIME_DEDICATED:
            if (entry->real_time_critical && entry->assigned_cpu < smp_ctx.cpu_count) {
                return entry->assigned_cpu;
            }
            return select_least_loaded_cpu(UINT32_MAX);
            
        case DIST_MODE_NUMA_AWARE:
            return select_numa_optimal_cpu(vector);
            
        default:
            return smp_ctx.bootstrap_processor;
    }
}

static void update_io_apic_routing(interrupt_routing_entry_t *entry, uint32_t target_cpu) {
    if (target_cpu >= smp_ctx.cpu_count) {
        return;
    }
    
    uint32_t target_apic_id = smp_ctx.cpus[target_cpu].local_apic_id;
    
    io_apic_configure_entry_extended(
        entry->io_apic_id,
        entry->io_apic_pin,
        entry->vector,
        target_apic_id,
        false,  // active_low
        false   // level_triggered
    );
    
    entry->assigned_cpu = target_cpu;
}

static void perform_load_balancing(void) {
    if (!smp_ctx.load_balancing_enabled || smp_ctx.online_cpu_count < 2) {
        return;
    }
    
    uint64_t current_time = rdtsc();
    
    uint32_t max_load = 0;
    uint32_t min_load = UINT32_MAX;
    uint32_t max_cpu = 0;
    uint32_t min_cpu = 0;
    
    for (size_t i = 0; i < smp_ctx.cpu_count; i++) {
        if (!smp_ctx.cpus[i].online) continue;
        
        uint32_t load = get_cpu_load_score(i);
        if (load > max_load) {
            max_load = load;
            max_cpu = i;
        }
        if (load < min_load) {
            min_load = load;
            min_cpu = i;
        }
    }
    
    if (max_load - min_load > SMP_LOAD_BALANCE_THRESHOLD) {
        for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
            interrupt_routing_entry_t *entry = &smp_ctx.routing_table[i];
            
            if (entry->assigned_cpu == max_cpu && 
                entry->mode == DIST_MODE_LOAD_BALANCED &&
                !entry->real_time_critical) {
                
                if (current_time - smp_ctx.cpus[max_cpu].last_migration_time > 
                    (SMP_MIGRATION_COOLDOWN_MS * 1000000ULL)) {
                    
                    update_io_apic_routing(entry, min_cpu);
                    smp_ctx.cpus[max_cpu].last_migration_time = current_time;
                    smp_ctx.migration_operations++;
                    break;
                }
            }
        }
        
        smp_ctx.load_balance_operations++;
    }
}

smp_interrupt_error_t smp_interrupt_init_config(const smp_distribution_config_t *config) {
    if (!config) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    memset(&smp_ctx, 0, sizeof(smp_ctx));
    smp_ctx.config = *config;
    
    acpi_local_apic_list_t apic_list;
    if (acpi_interrupt_get_local_apic_info(&apic_list) != ACPI_INT_SUCCESS) {
        return SMP_INT_ERROR_ACPI_FAILED;
    }
    
    smp_ctx.cpu_count = apic_list.count;
    if (smp_ctx.cpu_count > MAX_CPU_COUNT) {
        smp_ctx.cpu_count = MAX_CPU_COUNT;
    }
    
    for (size_t i = 0; i < smp_ctx.cpu_count; i++) {
        smp_ctx.cpus[i] = (cpu_info_t){
            .cpu_id = i,
            .local_apic_id = apic_list.apics[i].local_apic_id,
            .online = apic_list.apics[i].enabled,
            .enabled = apic_list.apics[i].enabled,
            .interrupt_count = 0,
            .last_migration_time = 0,
            .current_load = 0,
            .features = detect_cpu_features(i)
        };
        
        if (smp_ctx.cpus[i].online) {
            smp_ctx.online_cpu_count++;
        }
        
        if (i == 0) {
            smp_ctx.bootstrap_processor = i;
        }
    }
    
    if (smp_ctx.online_cpu_count == 0) {
        return SMP_INT_ERROR_NO_CPUS;
    }
    
    smp_ctx.load_balancing_enabled = config->enable_load_balancing;
    smp_ctx.distribution_enabled = true;
    smp_ctx.initialized = true;
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_register_vector(
    uint8_t vector, 
    interrupt_distribution_mode_t mode,
    const interrupt_affinity_t *affinity) {
    
    if (!smp_ctx.initialized) {
        return SMP_INT_ERROR_NOT_INITIALIZED;
    }
    
    if (smp_ctx.routing_entry_count >= MAX_INTERRUPT_VECTORS) {
        return SMP_INT_ERROR_NO_SPACE;
    }
    
    acpi_irq_routing_info_t routing_info;
    if (acpi_interrupt_get_irq_routing(0, 0, 1, &routing_info) != ACPI_INT_SUCCESS) {
        return SMP_INT_ERROR_ROUTING_FAILED;
    }
    
    uint32_t target_cpu = smp_ctx.bootstrap_processor;
    
    if (affinity) {
        if (affinity->cpu_mask != 0) {
            for (uint32_t i = 0; i < smp_ctx.cpu_count; i++) {
                if ((affinity->cpu_mask & (1ULL << i)) && smp_ctx.cpus[i].online) {
                    target_cpu = i;
                    break;
                }
            }
        } else if (affinity->preferred_cpu < smp_ctx.cpu_count && 
                  smp_ctx.cpus[affinity->preferred_cpu].online) {
            target_cpu = affinity->preferred_cpu;
        }
    }
    
    if (mode == DIST_MODE_LOAD_BALANCED || mode == DIST_MODE_LOWEST_PRIORITY) {
        target_cpu = select_least_loaded_cpu(UINT32_MAX);
    }
    
    interrupt_routing_entry_t *entry = &smp_ctx.routing_table[smp_ctx.routing_entry_count];
    *entry = (interrupt_routing_entry_t){
        .vector = vector,
        .assigned_cpu = target_cpu,
        .io_apic_id = routing_info.io_apic_id,
        .io_apic_pin = routing_info.io_apic_pin,
        .mode = mode,
        .round_robin_next = target_cpu,
        .interrupt_count = 0,
        .priority = affinity ? affinity->priority : INTERRUPT_PRIORITY_NORMAL,
        .real_time_critical = affinity ? affinity->real_time_critical : false
    };
    
    update_io_apic_routing(entry, target_cpu);
    smp_ctx.routing_entry_count++;
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_set_affinity(uint8_t vector, 
                                                const interrupt_affinity_t *affinity) {
    if (!smp_ctx.initialized || !affinity) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    interrupt_routing_entry_t *entry = NULL;
    for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
        if (smp_ctx.routing_table[i].vector == vector) {
            entry = &smp_ctx.routing_table[i];
            break;
        }
    }
    
    if (!entry) {
        return SMP_INT_ERROR_VECTOR_NOT_FOUND;
    }
    
    uint32_t new_cpu = entry->assigned_cpu;
    
    if (affinity->cpu_mask != 0) {
        for (uint32_t i = 0; i < smp_ctx.cpu_count; i++) {
            if ((affinity->cpu_mask & (1ULL << i)) && smp_ctx.cpus[i].online) {
                new_cpu = i;
                break;
            }
        }
    } else if (affinity->preferred_cpu < smp_ctx.cpu_count && 
              smp_ctx.cpus[affinity->preferred_cpu].online) {
        new_cpu = affinity->preferred_cpu;
    }
    
    if (new_cpu != entry->assigned_cpu) {
        update_io_apic_routing(entry, new_cpu);
        smp_ctx.migration_operations++;
    }
    
    entry->priority = affinity->priority;
    entry->real_time_critical = affinity->real_time_critical;
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_distribute(uint8_t vector) {
    if (!smp_ctx.initialized || !smp_ctx.distribution_enabled) {
        return SMP_INT_ERROR_NOT_INITIALIZED;
    }
    
    interrupt_routing_entry_t *entry = NULL;
    for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
        if (smp_ctx.routing_table[i].vector == vector) {
            entry = &smp_ctx.routing_table[i];
            break;
        }
    }
    
    if (!entry) {
        return SMP_INT_ERROR_VECTOR_NOT_FOUND;
    }
    
    uint32_t target_cpu = select_cpu_for_interrupt(vector, entry->mode);
    
    if (target_cpu != entry->assigned_cpu) {
        update_io_apic_routing(entry, target_cpu);
    }
    
    entry->interrupt_count++;
    smp_ctx.cpus[target_cpu].interrupt_count++;
    smp_ctx.total_interrupts_distributed++;
    
    if (smp_ctx.load_balancing_enabled && 
        (smp_ctx.total_interrupts_distributed % 1000) == 0) {
        perform_load_balancing();
    }
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_enable_cpu(uint32_t cpu_id) {
    if (!smp_ctx.initialized || cpu_id >= smp_ctx.cpu_count) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    cpu_info_t *cpu = &smp_ctx.cpus[cpu_id];
    if (!cpu->online) {
        return SMP_INT_ERROR_CPU_OFFLINE;
    }
    
    if (!cpu->enabled) {
        cpu->enabled = true;
        smp_ctx.online_cpu_count++;

        // Note: local_apic_enable takes no arguments - operates on current CPU
        (void)cpu->local_apic_id;  // TODO: per-CPU APIC enable not implemented
        local_apic_enable();
    }
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_disable_cpu(uint32_t cpu_id) {
    if (!smp_ctx.initialized || cpu_id >= smp_ctx.cpu_count) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    if (cpu_id == smp_ctx.bootstrap_processor) {
        return SMP_INT_ERROR_CANNOT_DISABLE_BSP;
    }
    
    cpu_info_t *cpu = &smp_ctx.cpus[cpu_id];
    if (cpu->enabled) {
        for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
            interrupt_routing_entry_t *entry = &smp_ctx.routing_table[i];
            if (entry->assigned_cpu == cpu_id) {
                uint32_t new_cpu = select_least_loaded_cpu(cpu_id);
                if (new_cpu != UINT32_MAX) {
                    update_io_apic_routing(entry, new_cpu);
                }
            }
        }
        
        cpu->enabled = false;
        smp_ctx.online_cpu_count--;

        // Note: local_apic_disable takes no arguments - operates on current CPU
        (void)cpu->local_apic_id;  // TODO: per-CPU APIC disable not implemented
        local_apic_disable();
    }
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_get_statistics(smp_interrupt_stats_t *stats) {
    if (!smp_ctx.initialized || !stats) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    memset(stats, 0, sizeof(smp_interrupt_stats_t));
    
    stats->total_cpus = smp_ctx.cpu_count;
    stats->online_cpus = smp_ctx.online_cpu_count;
    stats->total_interrupts_distributed = smp_ctx.total_interrupts_distributed;
    stats->load_balance_operations = smp_ctx.load_balance_operations;
    stats->migration_operations = smp_ctx.migration_operations;
    stats->registered_vectors = smp_ctx.routing_entry_count;
    
    uint64_t min_load = UINT64_MAX;
    uint64_t max_load = 0;
    uint64_t total_load = 0;
    
    for (size_t i = 0; i < smp_ctx.cpu_count; i++) {
        if (smp_ctx.cpus[i].online) {
            uint64_t load = smp_ctx.cpus[i].interrupt_count;
            if (load < min_load) min_load = load;
            if (load > max_load) max_load = load;
            total_load += load;
        }
    }
    
    stats->min_cpu_load = min_load;
    stats->max_cpu_load = max_load;
    stats->avg_cpu_load = smp_ctx.online_cpu_count > 0 ? 
                         total_load / smp_ctx.online_cpu_count : 0;
    
    stats->load_balance_ratio = max_load > 0 ? 
                               (double)min_load / (double)max_load : 1.0;
    
    return SMP_INT_SUCCESS;
}

smp_interrupt_error_t smp_interrupt_get_cpu_info(uint32_t cpu_id, 
                                                cpu_interrupt_info_t *info) {
    if (!smp_ctx.initialized || !info || cpu_id >= smp_ctx.cpu_count) {
        return SMP_INT_ERROR_INVALID_PARAMS;
    }
    
    cpu_info_t *cpu = &smp_ctx.cpus[cpu_id];
    
    *info = (cpu_interrupt_info_t){
        .cpu_id = cpu->cpu_id,
        .local_apic_id = cpu->local_apic_id,
        .online = cpu->online,
        .enabled = cpu->enabled,
        .interrupt_count = cpu->interrupt_count,
        .current_load = cpu->current_load,
        .features = cpu->features
    };
    
    info->assigned_vectors = 0;
    for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
        if (smp_ctx.routing_table[i].assigned_cpu == cpu_id) {
            info->assigned_vectors++;
        }
    }
    
    return SMP_INT_SUCCESS;
}

void smp_interrupt_enable_load_balancing(bool enable) {
    smp_ctx.load_balancing_enabled = enable;
}

void smp_interrupt_reset_statistics(void) {
    if (!smp_ctx.initialized) {
        return;
    }
    
    for (size_t i = 0; i < smp_ctx.cpu_count; i++) {
        smp_ctx.cpus[i].interrupt_count = 0;
        smp_ctx.cpus[i].current_load = 0;
    }
    
    for (size_t i = 0; i < smp_ctx.routing_entry_count; i++) {
        smp_ctx.routing_table[i].interrupt_count = 0;
    }
    
    smp_ctx.total_interrupts_distributed = 0;
    smp_ctx.load_balance_operations = 0;
    smp_ctx.migration_operations = 0;
}

bool smp_interrupt_is_initialized(void) {
    return smp_ctx.initialized;
}

bool smp_interrupt_is_load_balancing_enabled(void) {
    return smp_ctx.load_balancing_enabled;
}

uint32_t smp_interrupt_get_cpu_count(void) {
    return smp_ctx.cpu_count;
}

uint32_t smp_interrupt_get_online_cpu_count(void) {
    return smp_ctx.online_cpu_count;
}