#include "interrupt_driven_io.h"
#include "interrupt_management.h"
#include "smp_interrupt_distribution.h"
#include "acpi_interrupt_routing.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_IO_DEVICES 256
#define MAX_IO_OPERATIONS 1024
#define IO_OPERATION_TIMEOUT_MS 5000
#define IO_COMPLETION_QUEUE_SIZE 512

typedef struct {
    uint32_t operation_id;
    io_device_handle_t device_handle;
    io_operation_type_t type;
    void *buffer;
    size_t buffer_size;
    uint64_t offset;
    io_completion_callback_t completion_callback;
    void *callback_data;
    io_priority_level_t priority;
    uint64_t timestamp;
    uint64_t deadline;
    volatile io_operation_status_t status;
    io_error_t result;
    size_t bytes_transferred;
} io_operation_t;

typedef struct {
    io_device_handle_t handle;
    io_device_type_t type;
    char name[64];
    uint8_t interrupt_vector;
    uint32_t base_address;
    size_t memory_size;
    io_device_capabilities_t capabilities;
    
    io_device_operations_t operations;
    void *device_data;
    
    io_operation_t *current_operation;
    
    uint64_t total_operations;
    uint64_t completed_operations;
    uint64_t failed_operations;
    uint64_t bytes_transferred;
    uint64_t average_latency_ns;
    
    bool enabled;
    bool interrupt_pending;
} io_device_context_t;

typedef struct {
    io_operation_t operations[MAX_IO_OPERATIONS];
    size_t operation_count;
    uint32_t next_operation_id;
    
    io_device_context_t devices[MAX_IO_DEVICES];
    size_t device_count;
    uint32_t next_device_handle;
    
    io_operation_t *completion_queue[IO_COMPLETION_QUEUE_SIZE];
    size_t completion_queue_head;
    size_t completion_queue_tail;
    
    interrupt_driven_io_config_t config;
    
    uint64_t total_interrupts_handled;
    uint64_t total_operations_completed;
    uint64_t total_bytes_transferred;
    
    bool initialized;
} interrupt_driven_io_context_t;

static interrupt_driven_io_context_t io_ctx = {0};

static io_operation_t* allocate_io_operation(void) {
    for (size_t i = 0; i < MAX_IO_OPERATIONS; i++) {
        if (io_ctx.operations[i].status == IO_STATUS_AVAILABLE) {
            io_operation_t *op = &io_ctx.operations[i];
            memset(op, 0, sizeof(io_operation_t));
            op->operation_id = io_ctx.next_operation_id++;
            op->status = IO_STATUS_ALLOCATED;
            op->timestamp = rdtsc();
            return op;
        }
    }
    return NULL;
}

static void free_io_operation(io_operation_t *operation) {
    if (operation) {
        operation->status = IO_STATUS_AVAILABLE;
    }
}

static io_device_context_t* find_device_by_handle(io_device_handle_t handle) {
    for (size_t i = 0; i < io_ctx.device_count; i++) {
        if (io_ctx.devices[i].handle == handle) {
            return &io_ctx.devices[i];
        }
    }
    return NULL;
}

static io_device_context_t* find_device_by_vector(uint8_t vector) {
    for (size_t i = 0; i < io_ctx.device_count; i++) {
        if (io_ctx.devices[i].interrupt_vector == vector) {
            return &io_ctx.devices[i];
        }
    }
    return NULL;
}

static void enqueue_completion(io_operation_t *operation) {
    size_t next_tail = (io_ctx.completion_queue_tail + 1) % IO_COMPLETION_QUEUE_SIZE;
    if (next_tail != io_ctx.completion_queue_head) {
        io_ctx.completion_queue[io_ctx.completion_queue_tail] = operation;
        io_ctx.completion_queue_tail = next_tail;
    }
}

static io_operation_t* dequeue_completion(void) {
    if (io_ctx.completion_queue_head == io_ctx.completion_queue_tail) {
        return NULL;
    }
    
    io_operation_t *operation = io_ctx.completion_queue[io_ctx.completion_queue_head];
    io_ctx.completion_queue_head = (io_ctx.completion_queue_head + 1) % IO_COMPLETION_QUEUE_SIZE;
    return operation;
}

static void process_io_completions(void) {
    io_operation_t *operation;
    while ((operation = dequeue_completion()) != NULL) {
        io_device_context_t *device = find_device_by_handle(operation->device_handle);
        
        if (device) {
            uint64_t completion_time = rdtsc();
            uint64_t latency_cycles = completion_time - operation->timestamp;
            uint64_t latency_ns = (latency_cycles * 1000000000ULL) / tsc_frequency_hz;
            
            device->average_latency_ns = (device->average_latency_ns + latency_ns) / 2;
            device->bytes_transferred += operation->bytes_transferred;
            
            if (operation->status == IO_STATUS_COMPLETED) {
                device->completed_operations++;
            } else {
                device->failed_operations++;
            }
            
            device->current_operation = NULL;
        }
        
        if (operation->completion_callback) {
            operation->completion_callback(operation->operation_id, operation->result,
                                         operation->bytes_transferred, operation->callback_data);
        }
        
        free_io_operation(operation);
        io_ctx.total_operations_completed++;
    }
}

static void generic_io_interrupt_handler(uint8_t vector) {
    io_device_context_t *device = find_device_by_vector(vector);
    if (!device || !device->enabled) {
        return;
    }
    
    device->interrupt_pending = true;
    io_ctx.total_interrupts_handled++;
    
    if (device->operations.interrupt_handler) {
        io_operation_result_t result = device->operations.interrupt_handler(device->device_data);
        
        io_operation_t *current_op = device->current_operation;
        if (current_op) {
            current_op->status = result.status;
            current_op->result = result.error;
            current_op->bytes_transferred = result.bytes_transferred;
            
            enqueue_completion(current_op);
        }
    }
    
    device->interrupt_pending = false;
}

io_error_t interrupt_driven_io_init(const interrupt_driven_io_config_t *config) {
    if (!config) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    memset(&io_ctx, 0, sizeof(io_ctx));
    io_ctx.config = *config;
    io_ctx.next_device_handle = 1;
    io_ctx.next_operation_id = 1;
    
    for (size_t i = 0; i < MAX_IO_OPERATIONS; i++) {
        io_ctx.operations[i].status = IO_STATUS_AVAILABLE;
    }
    
    io_ctx.initialized = true;
    return IO_SUCCESS;
}

io_error_t io_register_device(const io_device_descriptor_t *descriptor, 
                            io_device_handle_t *handle) {
    if (!io_ctx.initialized || !descriptor || !handle) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    if (io_ctx.device_count >= MAX_IO_DEVICES) {
        return IO_ERROR_NO_SPACE;
    }
    
    io_device_context_t *device = &io_ctx.devices[io_ctx.device_count];
    memset(device, 0, sizeof(io_device_context_t));
    
    device->handle = io_ctx.next_device_handle++;
    device->type = descriptor->type;
    strncpy(device->name, descriptor->name, sizeof(device->name) - 1);
    device->interrupt_vector = descriptor->interrupt_vector;
    device->base_address = descriptor->base_address;
    device->memory_size = descriptor->memory_size;
    device->capabilities = descriptor->capabilities;
    device->operations = descriptor->operations;
    device->device_data = descriptor->device_data;
    device->enabled = true;
    
    if (descriptor->interrupt_vector != 0) {
        interrupt_register_handler(descriptor->interrupt_vector, 
                                 generic_io_interrupt_handler, device);
        
        if (io_ctx.config.enable_smp_distribution) {
            interrupt_affinity_t affinity = create_cpu_affinity(0);
            smp_interrupt_set_affinity(descriptor->interrupt_vector, &affinity);
        }
    }
    
    if (device->operations.initialize) {
        io_operation_result_t result = device->operations.initialize(device->device_data);
        if (result.error != IO_SUCCESS) {
            return result.error;
        }
    }
    
    *handle = device->handle;
    io_ctx.device_count++;
    
    return IO_SUCCESS;
}

io_error_t io_unregister_device(io_device_handle_t handle) {
    if (!io_ctx.initialized) {
        return IO_ERROR_NOT_INITIALIZED;
    }
    
    io_device_context_t *device = find_device_by_handle(handle);
    if (!device) {
        return IO_ERROR_DEVICE_NOT_FOUND;
    }
    
    device->enabled = false;
    
    if (device->current_operation) {
        device->current_operation->status = IO_STATUS_CANCELLED;
        device->current_operation->result = IO_ERROR_CANCELLED;
        enqueue_completion(device->current_operation);
    }
    
    if (device->operations.shutdown) {
        device->operations.shutdown(device->device_data);
    }
    
    if (device->interrupt_vector != 0) {
        interrupt_unregister_handler(device->interrupt_vector);
    }
    
    memset(device, 0, sizeof(io_device_context_t));
    
    return IO_SUCCESS;
}

io_error_t io_submit_operation(const io_operation_request_t *request, 
                             uint32_t *operation_id) {
    if (!io_ctx.initialized || !request || !operation_id) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    io_device_context_t *device = find_device_by_handle(request->device_handle);
    if (!device || !device->enabled) {
        return IO_ERROR_DEVICE_NOT_FOUND;
    }
    
    if (device->current_operation != NULL) {
        return IO_ERROR_DEVICE_BUSY;
    }
    
    io_operation_t *operation = allocate_io_operation();
    if (!operation) {
        return IO_ERROR_NO_MEMORY;
    }
    
    operation->device_handle = request->device_handle;
    operation->type = request->type;
    operation->buffer = request->buffer;
    operation->buffer_size = request->buffer_size;
    operation->offset = request->offset;
    operation->completion_callback = request->completion_callback;
    operation->callback_data = request->callback_data;
    operation->priority = request->priority;
    operation->status = IO_STATUS_PENDING;
    
    if (request->timeout_ms > 0) {
        operation->deadline = operation->timestamp + 
                             (request->timeout_ms * tsc_frequency_hz) / 1000;
    }
    
    device->current_operation = operation;
    device->total_operations++;
    
    io_operation_result_t result = {0};
    
    switch (request->type) {
        case IO_OPERATION_READ:
            if (device->operations.read) {
                result = device->operations.read(device->device_data, operation->buffer,
                                               operation->buffer_size, operation->offset);
            } else {
                result.error = IO_ERROR_NOT_SUPPORTED;
            }
            break;
            
        case IO_OPERATION_WRITE:
            if (device->operations.write) {
                result = device->operations.write(device->device_data, operation->buffer,
                                                operation->buffer_size, operation->offset);
            } else {
                result.error = IO_ERROR_NOT_SUPPORTED;
            }
            break;
            
        case IO_OPERATION_CONTROL:
            if (device->operations.control) {
                result = device->operations.control(device->device_data, operation->offset,
                                                  operation->buffer, operation->buffer_size);
            } else {
                result.error = IO_ERROR_NOT_SUPPORTED;
            }
            break;
            
        default:
            result.error = IO_ERROR_INVALID_OPERATION;
    }
    
    if (result.status == IO_STATUS_COMPLETED) {
        operation->status = IO_STATUS_COMPLETED;
        operation->result = result.error;
        operation->bytes_transferred = result.bytes_transferred;
        enqueue_completion(operation);
    } else if (result.error != IO_SUCCESS) {
        operation->status = IO_STATUS_ERROR;
        operation->result = result.error;
        enqueue_completion(operation);
    }
    
    *operation_id = operation->operation_id;
    return IO_SUCCESS;
}

io_error_t io_cancel_operation(uint32_t operation_id) {
    if (!io_ctx.initialized) {
        return IO_ERROR_NOT_INITIALIZED;
    }
    
    for (size_t i = 0; i < MAX_IO_OPERATIONS; i++) {
        io_operation_t *operation = &io_ctx.operations[i];
        if (operation->operation_id == operation_id && 
            operation->status == IO_STATUS_PENDING) {
            
            io_device_context_t *device = find_device_by_handle(operation->device_handle);
            if (device && device->operations.cancel) {
                device->operations.cancel(device->device_data);
            }
            
            operation->status = IO_STATUS_CANCELLED;
            operation->result = IO_ERROR_CANCELLED;
            enqueue_completion(operation);
            
            return IO_SUCCESS;
        }
    }
    
    return IO_ERROR_OPERATION_NOT_FOUND;
}

io_error_t io_get_operation_status(uint32_t operation_id, io_operation_status_t *status) {
    if (!io_ctx.initialized || !status) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    for (size_t i = 0; i < MAX_IO_OPERATIONS; i++) {
        io_operation_t *operation = &io_ctx.operations[i];
        if (operation->operation_id == operation_id) {
            *status = operation->status;
            return IO_SUCCESS;
        }
    }
    
    return IO_ERROR_OPERATION_NOT_FOUND;
}

io_error_t io_get_device_statistics(io_device_handle_t handle, io_device_statistics_t *stats) {
    if (!io_ctx.initialized || !stats) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    io_device_context_t *device = find_device_by_handle(handle);
    if (!device) {
        return IO_ERROR_DEVICE_NOT_FOUND;
    }
    
    *stats = (io_device_statistics_t){
        .device_handle = device->handle,
        .total_operations = device->total_operations,
        .completed_operations = device->completed_operations,
        .failed_operations = device->failed_operations,
        .bytes_transferred = device->bytes_transferred,
        .average_latency_ns = device->average_latency_ns,
        .current_operation_pending = (device->current_operation != NULL),
        .interrupt_pending = device->interrupt_pending
    };
    
    return IO_SUCCESS;
}

io_error_t io_get_global_statistics(io_global_statistics_t *stats) {
    if (!io_ctx.initialized || !stats) {
        return IO_ERROR_INVALID_PARAMS;
    }
    
    *stats = (io_global_statistics_t){
        .registered_devices = io_ctx.device_count,
        .total_interrupts_handled = io_ctx.total_interrupts_handled,
        .total_operations_completed = io_ctx.total_operations_completed,
        .total_bytes_transferred = io_ctx.total_bytes_transferred,
        .pending_completions = (io_ctx.completion_queue_tail - io_ctx.completion_queue_head + 
                               IO_COMPLETION_QUEUE_SIZE) % IO_COMPLETION_QUEUE_SIZE
    };
    
    uint64_t active_operations = 0;
    for (size_t i = 0; i < io_ctx.device_count; i++) {
        if (io_ctx.devices[i].current_operation != NULL) {
            active_operations++;
        }
    }
    stats->active_operations = active_operations;
    
    return IO_SUCCESS;
}

void io_process_completions(void) {
    if (!io_ctx.initialized) {
        return;
    }
    
    process_io_completions();
    
    if (io_ctx.config.enable_timeout_checking) {
        uint64_t current_time = rdtsc();
        
        for (size_t i = 0; i < io_ctx.device_count; i++) {
            io_device_context_t *device = &io_ctx.devices[i];
            io_operation_t *operation = device->current_operation;
            
            if (operation && operation->deadline > 0 && current_time > operation->deadline) {
                operation->status = IO_STATUS_TIMEOUT;
                operation->result = IO_ERROR_TIMEOUT;
                enqueue_completion(operation);
            }
        }
    }
}

void io_enable_device_interrupts(io_device_handle_t handle, bool enable) {
    io_device_context_t *device = find_device_by_handle(handle);
    if (device && device->operations.enable_interrupts) {
        device->operations.enable_interrupts(device->device_data, enable);
    }
}

bool io_is_initialized(void) {
    return io_ctx.initialized;
}

size_t io_get_device_count(void) {
    return io_ctx.device_count;
}

uint64_t io_get_total_operations(void) {
    return io_ctx.total_operations_completed;
}