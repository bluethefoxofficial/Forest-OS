#ifndef INTERRUPT_MANAGEMENT_H
#define INTERRUPT_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>
#include "interrupt.h"
#include "timer.h"
#include "atomic.h"
#include "spinlock.h"

// Forward declarations to avoid circular dependencies
struct interrupt_context;
struct interrupt_frame;

// Management system initialization
int interrupt_management_init(void);
int interrupt_management_shutdown(void);
bool interrupt_management_is_initialized(void);

// Core management functions
int interrupt_management_register_controller(const char *name, void *controller);
int interrupt_management_unregister_controller(const char *name);

// Statistics and monitoring
void interrupt_management_dump_stats(void);
void interrupt_management_reset_stats(void);

// Error handling
typedef enum {
    INTERRUPT_MGMT_SUCCESS = 0,
    INTERRUPT_MGMT_ERROR_INVALID_PARAMS,
    INTERRUPT_MGMT_ERROR_NOT_INITIALIZED,
    INTERRUPT_MGMT_ERROR_NO_MEMORY,
    INTERRUPT_MGMT_ERROR_NOT_FOUND
} interrupt_mgmt_error_t;

#endif // INTERRUPT_MANAGEMENT_H