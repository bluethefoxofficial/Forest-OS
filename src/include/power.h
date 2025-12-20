#ifndef POWER_H
#define POWER_H

#include "types.h"
#include <stdbool.h>

typedef enum {
    POWER_ACTION_SHUTDOWN = 0,
    POWER_ACTION_REBOOT = 1,
} power_action_t;

bool power_request(power_action_t action);
bool power_shutdown(void);
bool power_reboot(void);

#endif
