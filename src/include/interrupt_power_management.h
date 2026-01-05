#ifndef INTERRUPT_POWER_MANAGEMENT_H
#define INTERRUPT_POWER_MANAGEMENT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    POWER_MGMT_SUCCESS = 0,
    POWER_MGMT_ERROR_INVALID_PARAMS,
    POWER_MGMT_ERROR_NOT_INITIALIZED
} power_mgmt_error_t;

typedef enum {
    POWER_STATE_C0 = 0,
    POWER_STATE_C1,
    POWER_STATE_C2,
    POWER_STATE_C3
} power_state_t;

typedef struct {
    bool enable_c_states;
    bool enable_frequency_scaling;
    uint32_t idle_threshold_us;
} power_mgmt_config_t;

power_mgmt_error_t interrupt_power_mgmt_init(const power_mgmt_config_t *config);
power_mgmt_error_t interrupt_power_enter_state(power_state_t state);

#endif
