#include "graphics/unified_driver.h"
#include <graphics/graphics_types.h>
#include <debuglog.h>
#include <timer.h>
#include <memory.h>
#include <mm.h>
#include <string.h>

// Track subsystem initialization time for uptime calculation
static uint32_t unified_subsystem_init_ticks = 0;

// Global list of registered unified drivers
static unified_driver_t* unified_driver_list = NULL;
static uint32_t unified_driver_count = 0;

// Helper function to find a unified driver by device
static unified_driver_t* find_unified_driver_by_device(graphics_device_t* device) {
    unified_driver_t* current = unified_driver_list;
    
    while (current != NULL) {
        if (current->base_driver.ops && 
            find_driver_for_device(device) == &current->base_driver) {
            return current;
        }
        current = (unified_driver_t*)current->base_driver.next;
    }
    
    return NULL;
}

// Register a unified driver with the graphics subsystem
graphics_result_t register_unified_driver(unified_driver_t* driver) {
    if (!driver || !driver->unified_ops) {
        debuglog(DEBUG_ERROR, "Invalid unified driver registration attempt\n");
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Register the base driver first
    graphics_result_t result = register_display_driver(&driver->base_driver);
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to register base driver for unified driver %s\n",
                 driver->unified_ops->name ? driver->unified_ops->name : "Unknown");
        return result;
    }
    
    // Add to unified driver list
    driver->base_driver.next = (display_driver_t*)unified_driver_list;
    unified_driver_list = driver;
    unified_driver_count++;
    
    // Initialize statistics
    driver->operations_count = 0;
    driver->errors_count = 0;
    driver->last_error_code = GRAPHICS_SUCCESS;
    
    debuglog(DEBUG_INFO, "Registered unified driver: %s v%d.%d (API level %d)\n",
             driver->unified_ops->name,
             driver->unified_ops->version_major,
             driver->unified_ops->version_minor,
             driver->api_level);
    
    return GRAPHICS_SUCCESS;
}

// Unregister a unified driver
graphics_result_t unregister_unified_driver(unified_driver_t* driver) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Remove from unified driver list
    unified_driver_t* current = unified_driver_list;
    unified_driver_t* previous = NULL;
    
    while (current != NULL) {
        if (current == driver) {
            if (previous != NULL) {
                previous->base_driver.next = current->base_driver.next;
            } else {
                unified_driver_list = (unified_driver_t*)current->base_driver.next;
            }
            unified_driver_count--;
            break;
        }
        previous = current;
        current = (unified_driver_t*)current->base_driver.next;
    }
    
    // Unregister the base driver
    graphics_result_t result = unregister_display_driver(&driver->base_driver);
    
    debuglog(DEBUG_INFO, "Unregistered unified driver: %s\n",
             driver->unified_ops->name ? driver->unified_ops->name : "Unknown");
    
    return result;
}

// Get the unified driver for a specific device
unified_driver_t* get_unified_driver_for_device(graphics_device_t* device) {
    if (!device) {
        return NULL;
    }
    
    return find_unified_driver_by_device(device);
}

// Create a unified driver from a base driver and unified operations
graphics_result_t create_unified_driver_from_base(display_driver_t* base,
                                                 unified_driver_interface_t* unified_ops,
                                                 unified_driver_t** result) {
    if (!base || !unified_ops || !result) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Allocate memory for the unified driver
    unified_driver_t* driver = *result;
    if (!driver) {
        driver = kmalloc(sizeof(unified_driver_t));
        if (!driver) {
            debuglog(DEBUG_ERROR, "Failed to allocate memory for unified driver\n");
            return GRAPHICS_ERROR_OUT_OF_MEMORY;
        }
        *result = driver;
    }
    
    // Copy base driver
    memcpy(&driver->base_driver, base, sizeof(display_driver_t));
    
    // Set unified operations
    driver->unified_ops = unified_ops;
    driver->api_level = 1;
    driver->feature_flags = 0;
    driver->driver_private_data = NULL;
    driver->unified_interface_enabled = true;
    driver->active_features = 0;
    driver->operations_count = 0;
    driver->errors_count = 0;
    driver->last_error_code = GRAPHICS_SUCCESS;
    
    debuglog(DEBUG_INFO, "Created unified driver from base driver\n");
    
    return GRAPHICS_SUCCESS;
}

// Enable specific unified features for a driver
graphics_result_t enable_unified_features(unified_driver_t* driver,
                                         uint32_t feature_mask) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!driver->unified_interface_enabled) {
        debuglog(DEBUG_WARN, "Unified interface disabled for driver %s\n",
                 driver->unified_ops->name ? driver->unified_ops->name : "Unknown");
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    uint32_t old_features = driver->active_features;
    driver->active_features |= feature_mask;
    driver->feature_flags |= feature_mask;
    
    debuglog(DEBUG_INFO, "Enabled features 0x%08x for driver %s (was 0x%08x, now 0x%08x)\n",
             feature_mask,
             driver->unified_ops->name ? driver->unified_ops->name : "Unknown",
             old_features, driver->active_features);
    
    return GRAPHICS_SUCCESS;
}

// Disable specific unified features for a driver
graphics_result_t disable_unified_features(unified_driver_t* driver,
                                          uint32_t feature_mask) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    uint32_t old_features = driver->active_features;
    driver->active_features &= ~feature_mask;
    driver->feature_flags &= ~feature_mask;
    
    debuglog(DEBUG_INFO, "Disabled features 0x%08x for driver %s (was 0x%08x, now 0x%08x)\n",
             feature_mask,
             driver->unified_ops->name ? driver->unified_ops->name : "Unknown",
             old_features, driver->active_features);
    
    return GRAPHICS_SUCCESS;
}

// Get driver statistics
graphics_result_t get_unified_driver_stats(unified_driver_t* driver,
                                          unified_driver_stats_t* stats) {
    if (!driver || !stats) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }

    stats->total_operations = driver->operations_count;
    stats->successful_operations = driver->operations_count - driver->errors_count;
    stats->failed_operations = driver->errors_count;
    stats->active_features = driver->active_features;
    stats->last_error = driver->last_error_code;

    // Calculate uptime based on timer ticks since subsystem initialization
    uint32_t current_ticks = timer_get_ticks();
    uint64_t timer_freq = timer_get_frequency();
    if (timer_freq > 0 && unified_subsystem_init_ticks > 0) {
        uint32_t elapsed_ticks = current_ticks - unified_subsystem_init_ticks;
        stats->uptime_ms = ((uint64_t)elapsed_ticks * 1000) / timer_freq;
    } else {
        stats->uptime_ms = 0;
    }

    return GRAPHICS_SUCCESS;
}

// Reset driver statistics
graphics_result_t reset_unified_driver_stats(unified_driver_t* driver) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    driver->operations_count = 0;
    driver->errors_count = 0;
    driver->last_error_code = GRAPHICS_SUCCESS;
    
    debuglog(DEBUG_INFO, "Reset statistics for unified driver %s\n",
             driver->unified_ops->name ? driver->unified_ops->name : "Unknown");
    
    return GRAPHICS_SUCCESS;
}

// Helper function to increment operation counter and handle errors
static void update_driver_stats(unified_driver_t* driver, graphics_result_t result) {
    if (!driver) return;
    
    driver->operations_count++;
    if (result != GRAPHICS_SUCCESS) {
        driver->errors_count++;
        driver->last_error_code = result;
    }
}

// Wrapper functions that provide unified interface with statistics tracking
graphics_result_t unified_get_extended_capabilities(graphics_device_t* device,
                                                   graphics_capabilities_t* caps) {
    unified_driver_t* driver = get_unified_driver_for_device(device);
    if (!driver || !driver->unified_ops || !driver->unified_ops->get_extended_capabilities) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->unified_ops->get_extended_capabilities(device, caps);
    update_driver_stats(driver, result);
    return result;
}

graphics_result_t unified_query_feature_support(graphics_device_t* device,
                                               uint32_t feature_id,
                                               bool* supported) {
    unified_driver_t* driver = get_unified_driver_for_device(device);
    if (!driver || !driver->unified_ops || !driver->unified_ops->query_feature_support) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->unified_ops->query_feature_support(device, feature_id, supported);
    update_driver_stats(driver, result);
    return result;
}

graphics_result_t unified_create_virtual_display(graphics_device_t* device,
                                                uint32_t width, uint32_t height,
                                                uint32_t* display_id) {
    unified_driver_t* driver = get_unified_driver_for_device(device);
    if (!driver || !driver->unified_ops || !driver->unified_ops->create_virtual_display) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    if (!(driver->active_features & UNIFIED_FEATURE_VIRTUAL_DISPLAYS)) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->unified_ops->create_virtual_display(device, width, height, display_id);
    update_driver_stats(driver, result);
    return result;
}

graphics_result_t unified_get_gpu_temperature(graphics_device_t* device,
                                             uint32_t* temperature_celsius) {
    unified_driver_t* driver = get_unified_driver_for_device(device);
    if (!driver || !driver->unified_ops || !driver->unified_ops->get_gpu_temperature) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    if (!(driver->active_features & UNIFIED_FEATURE_GPU_MONITORING)) {
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    graphics_result_t result = driver->unified_ops->get_gpu_temperature(device, temperature_celsius);
    update_driver_stats(driver, result);
    return result;
}

// Initialize the unified driver subsystem
graphics_result_t initialize_unified_driver_subsystem(void) {
    debuglog(DEBUG_INFO, "Initializing unified graphics driver subsystem\n");

    unified_driver_list = NULL;
    unified_driver_count = 0;

    // Record initialization time for uptime tracking
    unified_subsystem_init_ticks = timer_get_ticks();

    return GRAPHICS_SUCCESS;
}

// Shutdown the unified driver subsystem
graphics_result_t shutdown_unified_driver_subsystem(void) {
    debuglog(DEBUG_INFO, "Shutting down unified graphics driver subsystem\n");
    
    // Unregister all unified drivers
    while (unified_driver_list != NULL) {
        unified_driver_t* current = unified_driver_list;
        unified_driver_list = (unified_driver_t*)current->base_driver.next;
        unregister_unified_driver(current);
    }
    
    unified_driver_count = 0;
    
    return GRAPHICS_SUCCESS;
}
