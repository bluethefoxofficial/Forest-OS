#ifndef LEGACY_INTERRUPT_DETECTION_H
#define LEGACY_INTERRUPT_DETECTION_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LEGACY_INTERRUPT_SUCCESS = 0,
    LEGACY_INTERRUPT_ERROR_NOT_SUPPORTED
} legacy_interrupt_error_t;

typedef enum {
    INTERRUPT_MODE_PIC = 0,
    INTERRUPT_MODE_APIC,
    INTERRUPT_MODE_X2APIC
} interrupt_mode_t;

legacy_interrupt_error_t legacy_interrupt_detect(void);
interrupt_mode_t legacy_interrupt_get_mode(void);
bool legacy_interrupt_pic_available(void);

#endif
