#ifndef DRIVER_H
#define DRIVER_H

#include "types.h"
#include <stdbool.h>

#define DRIVER_MAX_NAME_LEN 32
#define DRIVER_EVENT_PAYLOAD_MAX 64

typedef enum {
    DRIVER_CLASS_UNKNOWN = 0,
    DRIVER_CLASS_INPUT,
    DRIVER_CLASS_SOUND,
    DRIVER_CLASS_STORAGE,
    DRIVER_CLASS_NETWORK,
    DRIVER_CLASS_MISC
} driver_class_t;

// Common event codes that drivers may emit.
// Additional codes can be layered on top of these ranges.
#define DRIVER_EVENT_STATUS_READY      0x0001
#define DRIVER_EVENT_STATUS_FAILURE    0x0002
#define DRIVER_EVENT_NETWORK_RX_READY  0x0100

struct driver;
typedef struct driver driver_t;

typedef struct {
    uint16 driver_id;
    driver_class_t driver_class;
    uint32 code;
    uint32 payload_length;
    uint8 payload[DRIVER_EVENT_PAYLOAD_MAX];
} driver_event_t;

struct driver {
    const char* name;
    driver_class_t driver_class;
    bool (*init)(driver_t* driver);
    void (*shutdown)(driver_t* driver);
    void* context;
    uint16 id;
    bool initialized;
};

bool driver_manager_init(void);
bool driver_register(driver_t* driver);
driver_t* driver_find(const char* name);
bool driver_emit_event(uint16 driver_id, driver_class_t driver_class,
                       uint32 code, const void* payload, uint32 payload_length);
bool driver_event_pop(driver_event_t* out_event);
void driver_shutdown_all(void);

#endif
