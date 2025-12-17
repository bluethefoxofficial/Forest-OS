#include "include/driver.h"
#include "include/util.h"
#include "include/string.h"

#define DRIVER_MAX_COUNT 32
#define DRIVER_EVENT_QUEUE_SIZE 64

static driver_t* g_drivers[DRIVER_MAX_COUNT];
static driver_event_t g_event_queue[DRIVER_EVENT_QUEUE_SIZE];
static uint32 g_event_head = 0;
static uint32 g_event_tail = 0;
static uint32 g_event_count = 0;
static bool g_driver_manager_ready = false;

static void driver_clear(driver_t* driver) {
    if (!driver) {
        return;
    }
    driver->id = 0;
    driver->initialized = false;
}

bool driver_manager_init(void) {
    memory_set((uint8*)g_drivers, 0, sizeof(g_drivers));
    memory_set((uint8*)g_event_queue, 0, sizeof(g_event_queue));
    g_event_head = g_event_tail = g_event_count = 0;
    g_driver_manager_ready = true;
    return true;
}

static uint16 next_driver_id(void) {
    for (uint32 i = 0; i < DRIVER_MAX_COUNT; i++) {
        if (!g_drivers[i]) {
            return (uint16)(i + 1);
        }
    }
    return 0;
}

bool driver_register(driver_t* driver) {
    if (!driver) {
        return false;
    }

    if (!g_driver_manager_ready && !driver_manager_init()) {
        return false;
    }

    // Prevent duplicate registrations by name
    if (driver->name) {
        for (uint32 i = 0; i < DRIVER_MAX_COUNT; i++) {
            if (g_drivers[i] && strcmp(g_drivers[i]->name, driver->name) == 0) {
                return false;
            }
        }
    }

    uint16 id = next_driver_id();
    if (!id) {
        return false;
    }

    driver->id = id;
    driver->initialized = false;

    g_drivers[id - 1] = driver;

    bool ok = true;
    if (driver->init) {
        ok = driver->init(driver);
    }

    driver->initialized = ok;
    uint16 event_id = driver->id;
    if (ok) {
        driver_emit_event(event_id, driver->driver_class,
                          DRIVER_EVENT_STATUS_READY, 0, 0);
        return true;
    }

    g_drivers[id - 1] = 0;
    driver_clear(driver);
    driver_emit_event(event_id, driver->driver_class,
                      DRIVER_EVENT_STATUS_FAILURE, 0, 0);
    return false;
}

driver_t* driver_find(const char* name) {
    if (!name) {
        return 0;
    }
    for (uint32 i = 0; i < DRIVER_MAX_COUNT; i++) {
        if (g_drivers[i] && strcmp(g_drivers[i]->name, name) == 0) {
            return g_drivers[i];
        }
    }
    return 0;
}

bool driver_emit_event(uint16 driver_id, driver_class_t driver_class,
                       uint32 code, const void* payload, uint32 payload_length) {
    if (!g_driver_manager_ready) {
        return false;
    }
    if (g_event_count >= DRIVER_EVENT_QUEUE_SIZE) {
        return false;
    }

    driver_event_t* evt = &g_event_queue[g_event_tail];
    evt->driver_id = driver_id;
    evt->driver_class = driver_class;
    evt->code = code;
    if (payload_length > DRIVER_EVENT_PAYLOAD_MAX) {
        payload_length = DRIVER_EVENT_PAYLOAD_MAX;
    }
    evt->payload_length = payload_length;
    if (payload && payload_length) {
        memory_copy((char*)payload, (char*)evt->payload, payload_length);
    } else {
        memory_set(evt->payload, 0, DRIVER_EVENT_PAYLOAD_MAX);
    }

    g_event_tail = (g_event_tail + 1) % DRIVER_EVENT_QUEUE_SIZE;
    g_event_count++;
    return true;
}

bool driver_event_pop(driver_event_t* out_event) {
    if (!out_event || g_event_count == 0) {
        return false;
    }

    driver_event_t* evt = &g_event_queue[g_event_head];
    memory_copy((char*)evt, (char*)out_event, sizeof(driver_event_t));

    g_event_head = (g_event_head + 1) % DRIVER_EVENT_QUEUE_SIZE;
    g_event_count--;
    return true;
}
