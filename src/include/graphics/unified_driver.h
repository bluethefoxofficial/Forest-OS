#ifndef UNIFIED_DRIVER_H
#define UNIFIED_DRIVER_H

#include "graphics_types.h"
#include "display_driver.h"

// Unified graphics driver interface for advanced features
typedef struct unified_driver_interface {
    // Basic driver identification
    const char* name;
    const char* vendor;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t api_version;
    
    // Advanced capabilities query
    graphics_result_t (*get_extended_capabilities)(graphics_device_t* device,
                                                 graphics_capabilities_t* caps);
    graphics_result_t (*query_feature_support)(graphics_device_t* device,
                                              uint32_t feature_id,
                                              bool* supported);
    
    // Virtual display management (for VirtualBox, VMware, Bochs)
    graphics_result_t (*create_virtual_display)(graphics_device_t* device,
                                              uint32_t width, uint32_t height,
                                              uint32_t* display_id);
    graphics_result_t (*destroy_virtual_display)(graphics_device_t* device,
                                                uint32_t display_id);
    graphics_result_t (*set_virtual_display_offset)(graphics_device_t* device,
                                                   uint32_t display_id,
                                                   int32_t x_offset, int32_t y_offset);
    
    // Advanced memory management
    graphics_result_t (*allocate_video_memory)(graphics_device_t* device,
                                             size_t size, uint32_t alignment,
                                             void** virtual_addr,
                                             uintptr_t* physical_addr);
    graphics_result_t (*free_video_memory)(graphics_device_t* device,
                                         void* virtual_addr);
    graphics_result_t (*map_video_memory)(graphics_device_t* device,
                                        uintptr_t physical_addr,
                                        size_t size,
                                        void** virtual_addr);
    graphics_result_t (*unmap_video_memory)(graphics_device_t* device,
                                          void* virtual_addr, size_t size);
    
    // GPU-specific features
    graphics_result_t (*get_gpu_temperature)(graphics_device_t* device,
                                           uint32_t* temperature_celsius);
    graphics_result_t (*get_gpu_utilization)(graphics_device_t* device,
                                           uint32_t* utilization_percent);
    graphics_result_t (*get_memory_info)(graphics_device_t* device,
                                       size_t* total_memory,
                                       size_t* available_memory,
                                       size_t* used_memory);
    
    // Performance and power management
    graphics_result_t (*set_performance_profile)(graphics_device_t* device,
                                                uint32_t profile);
    graphics_result_t (*set_power_limit)(graphics_device_t* device,
                                       uint32_t power_limit_watts);
    graphics_result_t (*set_clock_frequencies)(graphics_device_t* device,
                                             uint32_t core_clock_mhz,
                                             uint32_t memory_clock_mhz);
    
    // Multi-head display support
    graphics_result_t (*enumerate_outputs)(graphics_device_t* device,
                                         uint32_t* output_count,
                                         uint32_t* output_ids);
    graphics_result_t (*set_output_mode)(graphics_device_t* device,
                                       uint32_t output_id,
                                       const video_mode_t* mode);
    graphics_result_t (*get_output_edid)(graphics_device_t* device,
                                       uint32_t output_id,
                                       uint8_t* edid_data,
                                       size_t* size);
    
    // Hardware acceleration interface
    graphics_result_t (*create_acceleration_context)(graphics_device_t* device,
                                                    void** context);
    graphics_result_t (*destroy_acceleration_context)(graphics_device_t* device,
                                                     void* context);
    graphics_result_t (*submit_command_buffer)(graphics_device_t* device,
                                             void* context,
                                             const void* commands,
                                             size_t size);
    
    // Guest additions / hypervisor integration
    graphics_result_t (*detect_hypervisor)(graphics_device_t* device,
                                          char* hypervisor_name,
                                          size_t name_size);
    graphics_result_t (*enable_guest_integration)(graphics_device_t* device);
    graphics_result_t (*disable_guest_integration)(graphics_device_t* device);
    
    // Advanced IOCTL interface
    graphics_result_t (*extended_ioctl)(graphics_device_t* device,
                                      uint32_t cmd,
                                      const void* input_data,
                                      size_t input_size,
                                      void* output_data,
                                      size_t output_size,
                                      size_t* bytes_returned);
} unified_driver_interface_t;

// Unified driver structure extending the basic display driver
typedef struct unified_driver {
    display_driver_t base_driver;           // Base display driver functionality
    unified_driver_interface_t* unified_ops; // Extended operations
    
    // Driver metadata
    uint32_t api_level;
    uint32_t feature_flags;
    void* driver_private_data;
    
    // Runtime state
    bool unified_interface_enabled;
    uint32_t active_features;
    
    // Statistics
    uint64_t operations_count;
    uint64_t errors_count;
    uint32_t last_error_code;
} unified_driver_t;

// Feature flags for unified drivers
#define UNIFIED_FEATURE_VIRTUAL_DISPLAYS    (1 << 0)
#define UNIFIED_FEATURE_GPU_MONITORING      (1 << 1)
#define UNIFIED_FEATURE_POWER_MANAGEMENT    (1 << 2)
#define UNIFIED_FEATURE_MULTI_HEAD          (1 << 3)
#define UNIFIED_FEATURE_HW_ACCELERATION     (1 << 4)
#define UNIFIED_FEATURE_GUEST_INTEGRATION   (1 << 5)
#define UNIFIED_FEATURE_ADVANCED_MEMORY     (1 << 6)
#define UNIFIED_FEATURE_PERFORMANCE_TUNING  (1 << 7)

// Performance profiles
#define PERF_PROFILE_POWER_SAVER    0
#define PERF_PROFILE_BALANCED       1
#define PERF_PROFILE_HIGH_PERFORMANCE 2
#define PERF_PROFILE_MAXIMUM        3

// Specific feature IDs for query_feature_support
#define FEATURE_ID_VIRTUAL_DISPLAYS     0x1000
#define FEATURE_ID_GPU_TEMP_MONITORING  0x1001
#define FEATURE_ID_DYNAMIC_CLOCKING     0x1002
#define FEATURE_ID_MULTIPLE_OUTPUTS     0x1003
#define FEATURE_ID_HW_CURSOR_ALPHA      0x1004
#define FEATURE_ID_VBE_EXTENSIONS       0x1005
#define FEATURE_ID_GUEST_ADDITIONS      0x1006
#define FEATURE_ID_RTX_FEATURES         0x2000
#define FEATURE_ID_DLSS_SUPPORT         0x2001
#define FEATURE_ID_RAY_TRACING          0x2002
#define FEATURE_ID_RDNA_FEATURES        0x3000
#define FEATURE_ID_INFINITY_CACHE       0x3001
#define FEATURE_ID_INTEL_QSV            0x4000
#define FEATURE_ID_INTEL_XE             0x4001

// Extended IOCTL commands
#define UNIFIED_IOCTL_BASE              0x8000
#define UNIFIED_IOCTL_SET_VIRTUAL_RES   (UNIFIED_IOCTL_BASE + 1)
#define UNIFIED_IOCTL_GET_GPU_STATS     (UNIFIED_IOCTL_BASE + 2)
#define UNIFIED_IOCTL_SET_POWER_PROFILE (UNIFIED_IOCTL_BASE + 3)
#define UNIFIED_IOCTL_ENABLE_FEATURES   (UNIFIED_IOCTL_BASE + 4)
#define UNIFIED_IOCTL_GUEST_HANDSHAKE   (UNIFIED_IOCTL_BASE + 5)

// Registration and management functions
graphics_result_t register_unified_driver(unified_driver_t* driver);
graphics_result_t unregister_unified_driver(unified_driver_t* driver);
unified_driver_t* get_unified_driver_for_device(graphics_device_t* device);

// Utility functions for driver developers
graphics_result_t create_unified_driver_from_base(display_driver_t* base,
                                                 unified_driver_interface_t* unified_ops,
                                                 unified_driver_t** result);
graphics_result_t enable_unified_features(unified_driver_t* driver,
                                         uint32_t feature_mask);
graphics_result_t disable_unified_features(unified_driver_t* driver,
                                          uint32_t feature_mask);

// Common helper macros
#define DECLARE_UNIFIED_DRIVER(name, base_ops, unified_ops_param) \
    unified_driver_t name##_unified_driver = { \
        .base_driver = { \
            .ops = &base_ops, \
            .private_data = NULL, \
            .flags = 0, \
            .next = NULL, \
            .is_loaded = false \
        }, \
        .unified_ops = &unified_ops_param, \
        .api_level = 1, \
        .feature_flags = 0, \
        .driver_private_data = NULL, \
        .unified_interface_enabled = true, \
        .active_features = 0, \
        .operations_count = 0, \
        .errors_count = 0, \
        .last_error_code = GRAPHICS_SUCCESS \
    }

#define UNIFIED_DRIVER_INIT_FUNCTION(driver_name) \
    graphics_result_t driver_name##_unified_init(void)

#define UNIFIED_DRIVER_EXIT_FUNCTION(driver_name) \
    void driver_name##_unified_exit(void)

// Debugging and diagnostics
typedef struct unified_driver_stats {
    uint64_t total_operations;
    uint64_t successful_operations;
    uint64_t failed_operations;
    uint32_t active_features;
    uint32_t last_error;
    uint64_t uptime_ms;
} unified_driver_stats_t;

graphics_result_t get_unified_driver_stats(unified_driver_t* driver,
                                          unified_driver_stats_t* stats);
graphics_result_t reset_unified_driver_stats(unified_driver_t* driver);

#endif // UNIFIED_DRIVER_H