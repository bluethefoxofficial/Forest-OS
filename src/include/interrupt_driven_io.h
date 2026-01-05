#ifndef INTERRUPT_DRIVEN_IO_H
#define INTERRUPT_DRIVEN_IO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint32_t io_device_handle_t;
typedef uint32_t io_operation_id_t;

typedef enum {
    IO_SUCCESS = 0,
    IO_ERROR_INVALID_PARAMS,
    IO_ERROR_NOT_INITIALIZED,
    IO_ERROR_NO_SPACE,
    IO_ERROR_NO_MEMORY,
    IO_ERROR_DEVICE_NOT_FOUND,
    IO_ERROR_DEVICE_BUSY,
    IO_ERROR_OPERATION_NOT_FOUND,
    IO_ERROR_NOT_SUPPORTED,
    IO_ERROR_INVALID_OPERATION,
    IO_ERROR_TIMEOUT,
    IO_ERROR_CANCELLED,
    IO_ERROR_HARDWARE_ERROR,
    IO_ERROR_DATA_CORRUPTION,
    IO_ERROR_ACCESS_DENIED
} io_error_t;

typedef enum {
    IO_STATUS_AVAILABLE = 0,
    IO_STATUS_ALLOCATED,
    IO_STATUS_PENDING,
    IO_STATUS_COMPLETED,
    IO_STATUS_ERROR,
    IO_STATUS_TIMEOUT,
    IO_STATUS_CANCELLED
} io_operation_status_t;

typedef enum {
    IO_OPERATION_READ = 0,
    IO_OPERATION_WRITE,
    IO_OPERATION_CONTROL,
    IO_OPERATION_FLUSH,
    IO_OPERATION_RESET
} io_operation_type_t;

typedef enum {
    IO_DEVICE_STORAGE = 0,
    IO_DEVICE_NETWORK,
    IO_DEVICE_INPUT,
    IO_DEVICE_OUTPUT,
    IO_DEVICE_COMMUNICATION,
    IO_DEVICE_SENSOR,
    IO_DEVICE_CUSTOM
} io_device_type_t;

typedef enum {
    IO_PRIORITY_LOW = 0,
    IO_PRIORITY_NORMAL = 1,
    IO_PRIORITY_HIGH = 2,
    IO_PRIORITY_CRITICAL = 3
} io_priority_level_t;

typedef struct {
    bool supports_read;
    bool supports_write;
    bool supports_async;
    bool supports_dma;
    bool supports_scatter_gather;
    bool supports_priority;
    uint32_t max_transfer_size;
    uint32_t alignment_requirement;
} io_device_capabilities_t;

typedef struct {
    io_operation_status_t status;
    io_error_t error;
    size_t bytes_transferred;
} io_operation_result_t;

typedef void (*io_completion_callback_t)(uint32_t operation_id, io_error_t result, 
                                        size_t bytes_transferred, void *data);

typedef io_operation_result_t (*io_device_initialize_t)(void *device_data);
typedef io_operation_result_t (*io_device_shutdown_t)(void *device_data);
typedef io_operation_result_t (*io_device_read_t)(void *device_data, void *buffer, 
                                                 size_t size, uint64_t offset);
typedef io_operation_result_t (*io_device_write_t)(void *device_data, const void *buffer, 
                                                  size_t size, uint64_t offset);
typedef io_operation_result_t (*io_device_control_t)(void *device_data, uint64_t command, 
                                                    void *buffer, size_t buffer_size);
typedef io_operation_result_t (*io_device_interrupt_handler_t)(void *device_data);
typedef void (*io_device_enable_interrupts_t)(void *device_data, bool enable);
typedef void (*io_device_cancel_t)(void *device_data);

typedef struct {
    io_device_initialize_t initialize;
    io_device_shutdown_t shutdown;
    io_device_read_t read;
    io_device_write_t write;
    io_device_control_t control;
    io_device_interrupt_handler_t interrupt_handler;
    io_device_enable_interrupts_t enable_interrupts;
    io_device_cancel_t cancel;
} io_device_operations_t;

typedef struct {
    io_device_type_t type;
    char name[64];
    uint8_t interrupt_vector;
    uint32_t base_address;
    size_t memory_size;
    io_device_capabilities_t capabilities;
    io_device_operations_t operations;
    void *device_data;
} io_device_descriptor_t;

typedef struct {
    io_device_handle_t device_handle;
    io_operation_type_t type;
    void *buffer;
    size_t buffer_size;
    uint64_t offset;
    io_priority_level_t priority;
    uint32_t timeout_ms;
    io_completion_callback_t completion_callback;
    void *callback_data;
} io_operation_request_t;

typedef struct {
    bool enable_smp_distribution;
    bool enable_timeout_checking;
    bool enable_priority_scheduling;
    bool enable_statistics_collection;
    uint32_t default_timeout_ms;
    uint32_t completion_processing_interval_ms;
} interrupt_driven_io_config_t;

typedef struct {
    io_device_handle_t device_handle;
    uint64_t total_operations;
    uint64_t completed_operations;
    uint64_t failed_operations;
    uint64_t bytes_transferred;
    uint64_t average_latency_ns;
    bool current_operation_pending;
    bool interrupt_pending;
} io_device_statistics_t;

typedef struct {
    uint32_t registered_devices;
    uint64_t total_interrupts_handled;
    uint64_t total_operations_completed;
    uint64_t total_bytes_transferred;
    uint32_t active_operations;
    uint32_t pending_completions;
} io_global_statistics_t;

io_error_t interrupt_driven_io_init(const interrupt_driven_io_config_t *config);

io_error_t io_register_device(const io_device_descriptor_t *descriptor, 
                            io_device_handle_t *handle);

io_error_t io_unregister_device(io_device_handle_t handle);

io_error_t io_submit_operation(const io_operation_request_t *request, 
                             uint32_t *operation_id);

io_error_t io_cancel_operation(uint32_t operation_id);

io_error_t io_get_operation_status(uint32_t operation_id, io_operation_status_t *status);

io_error_t io_get_device_statistics(io_device_handle_t handle, io_device_statistics_t *stats);

io_error_t io_get_global_statistics(io_global_statistics_t *stats);

void io_process_completions(void);

void io_enable_device_interrupts(io_device_handle_t handle, bool enable);

bool io_is_initialized(void);

size_t io_get_device_count(void);

uint64_t io_get_total_operations(void);

static inline const char* io_error_to_string(io_error_t error) {
    switch (error) {
        case IO_SUCCESS:
            return "Success";
        case IO_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case IO_ERROR_NOT_INITIALIZED:
            return "I/O framework not initialized";
        case IO_ERROR_NO_SPACE:
            return "No space available";
        case IO_ERROR_NO_MEMORY:
            return "Insufficient memory";
        case IO_ERROR_DEVICE_NOT_FOUND:
            return "Device not found";
        case IO_ERROR_DEVICE_BUSY:
            return "Device is busy";
        case IO_ERROR_OPERATION_NOT_FOUND:
            return "Operation not found";
        case IO_ERROR_NOT_SUPPORTED:
            return "Operation not supported";
        case IO_ERROR_INVALID_OPERATION:
            return "Invalid operation";
        case IO_ERROR_TIMEOUT:
            return "Operation timed out";
        case IO_ERROR_CANCELLED:
            return "Operation cancelled";
        case IO_ERROR_HARDWARE_ERROR:
            return "Hardware error";
        case IO_ERROR_DATA_CORRUPTION:
            return "Data corruption detected";
        case IO_ERROR_ACCESS_DENIED:
            return "Access denied";
        default:
            return "Unknown I/O error";
    }
}

static inline const char* io_operation_status_to_string(io_operation_status_t status) {
    switch (status) {
        case IO_STATUS_AVAILABLE:
            return "Available";
        case IO_STATUS_ALLOCATED:
            return "Allocated";
        case IO_STATUS_PENDING:
            return "Pending";
        case IO_STATUS_COMPLETED:
            return "Completed";
        case IO_STATUS_ERROR:
            return "Error";
        case IO_STATUS_TIMEOUT:
            return "Timeout";
        case IO_STATUS_CANCELLED:
            return "Cancelled";
        default:
            return "Unknown";
    }
}

static inline const char* io_device_type_to_string(io_device_type_t type) {
    switch (type) {
        case IO_DEVICE_STORAGE:
            return "Storage";
        case IO_DEVICE_NETWORK:
            return "Network";
        case IO_DEVICE_INPUT:
            return "Input";
        case IO_DEVICE_OUTPUT:
            return "Output";
        case IO_DEVICE_COMMUNICATION:
            return "Communication";
        case IO_DEVICE_SENSOR:
            return "Sensor";
        case IO_DEVICE_CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
}

static inline interrupt_driven_io_config_t io_default_config(void) {
    return (interrupt_driven_io_config_t){
        .enable_smp_distribution = true,
        .enable_timeout_checking = true,
        .enable_priority_scheduling = false,
        .enable_statistics_collection = true,
        .default_timeout_ms = 5000,
        .completion_processing_interval_ms = 10
    };
}

static inline interrupt_driven_io_config_t io_realtime_config(void) {
    return (interrupt_driven_io_config_t){
        .enable_smp_distribution = false,
        .enable_timeout_checking = true,
        .enable_priority_scheduling = true,
        .enable_statistics_collection = false,
        .default_timeout_ms = 100,
        .completion_processing_interval_ms = 1
    };
}

static inline interrupt_driven_io_config_t io_performance_config(void) {
    return (interrupt_driven_io_config_t){
        .enable_smp_distribution = true,
        .enable_timeout_checking = false,
        .enable_priority_scheduling = false,
        .enable_statistics_collection = true,
        .default_timeout_ms = 30000,
        .completion_processing_interval_ms = 50
    };
}

static inline io_operation_request_t create_read_request(io_device_handle_t device, 
                                                       void *buffer, size_t size, 
                                                       uint64_t offset) {
    return (io_operation_request_t){
        .device_handle = device,
        .type = IO_OPERATION_READ,
        .buffer = buffer,
        .buffer_size = size,
        .offset = offset,
        .priority = IO_PRIORITY_NORMAL,
        .timeout_ms = 0,
        .completion_callback = NULL,
        .callback_data = NULL
    };
}

static inline io_operation_request_t create_write_request(io_device_handle_t device, 
                                                        const void *buffer, size_t size, 
                                                        uint64_t offset) {
    return (io_operation_request_t){
        .device_handle = device,
        .type = IO_OPERATION_WRITE,
        .buffer = (void*)buffer,
        .buffer_size = size,
        .offset = offset,
        .priority = IO_PRIORITY_NORMAL,
        .timeout_ms = 0,
        .completion_callback = NULL,
        .callback_data = NULL
    };
}

static inline io_operation_request_t create_async_request(io_device_handle_t device, 
                                                        io_operation_type_t type,
                                                        void *buffer, size_t size, 
                                                        uint64_t offset,
                                                        io_completion_callback_t callback,
                                                        void *callback_data) {
    return (io_operation_request_t){
        .device_handle = device,
        .type = type,
        .buffer = buffer,
        .buffer_size = size,
        .offset = offset,
        .priority = IO_PRIORITY_NORMAL,
        .timeout_ms = 0,
        .completion_callback = callback,
        .callback_data = callback_data
    };
}

extern uint64_t rdtsc(void);
extern uint64_t tsc_frequency_hz;
extern void interrupt_register_handler(uint8_t vector, void *handler, void *data);
extern void interrupt_unregister_handler(uint8_t vector);

#endif // INTERRUPT_DRIVEN_IO_H