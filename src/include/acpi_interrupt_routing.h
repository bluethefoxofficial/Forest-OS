#ifndef ACPI_INTERRUPT_ROUTING_H
#define ACPI_INTERRUPT_ROUTING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_LOCAL_APIC_ENTRIES 256
#define MAX_IO_APIC_ENTRIES 64
#define MAX_ERROR_MESSAGE_LENGTH 512

typedef enum {
    ACPI_INT_SUCCESS = 0,
    ACPI_INT_ERROR_NO_MADT,
    ACPI_INT_ERROR_INVALID_CHECKSUM,
    ACPI_INT_ERROR_NO_LOCAL_APIC,
    ACPI_INT_ERROR_NO_IO_APIC,
    ACPI_INT_ERROR_NOT_INITIALIZED,
    ACPI_INT_ERROR_INVALID_PARAMS,
    ACPI_INT_ERROR_DEVICE_NOT_FOUND,
    ACPI_INT_ERROR_GSI_NOT_AVAILABLE,
    ACPI_INT_ERROR_CONFIGURATION_FAILED
} acpi_interrupt_error_t;

typedef void (*acpi_interrupt_handler_t)(void *context);

typedef struct {
    uint8_t legacy_irq;
    uint32_t global_irq;
    uint8_t io_apic_id;
    uint8_t io_apic_pin;
    bool active_low;
    bool level_triggered;
    bool has_override;
} acpi_irq_routing_info_t;

typedef struct {
    uint8_t processor_id;
    uint8_t local_apic_id;
    bool enabled;
} acpi_local_apic_entry_info_t;

typedef struct {
    acpi_local_apic_entry_info_t apics[MAX_LOCAL_APIC_ENTRIES];
    size_t count;
    uint32_t base_address;
} acpi_local_apic_list_t;

typedef struct {
    uint8_t io_apic_id;
    uint32_t address;
    uint32_t gsi_base;
    uint32_t gsi_count;
} acpi_io_apic_entry_info_t;

typedef struct {
    acpi_io_apic_entry_info_t apics[MAX_IO_APIC_ENTRIES];
    size_t count;
} acpi_io_apic_list_t;

typedef struct {
    bool is_valid;
    size_t local_apic_count;
    size_t io_apic_count;
    size_t override_count;
    size_t nmi_source_count;
    char error_message[MAX_ERROR_MESSAGE_LENGTH];
} acpi_validation_result_t;

acpi_interrupt_error_t acpi_interrupt_init(void);

acpi_interrupt_error_t acpi_interrupt_get_irq_routing(
    uint8_t bus, uint8_t device, uint8_t pin,
    acpi_irq_routing_info_t *routing_info);

acpi_interrupt_error_t acpi_interrupt_configure_device(
    uint8_t bus, uint8_t device, uint8_t pin,
    acpi_interrupt_handler_t handler, void *context);

acpi_interrupt_error_t acpi_interrupt_get_local_apic_info(
    acpi_local_apic_list_t *apic_list);

acpi_interrupt_error_t acpi_interrupt_get_io_apic_info(
    acpi_io_apic_list_t *apic_list);

acpi_interrupt_error_t acpi_interrupt_find_gsi_for_irq(uint8_t legacy_irq, uint32_t *gsi);

acpi_interrupt_error_t acpi_interrupt_configure_nmi_sources(void);

acpi_interrupt_error_t acpi_interrupt_disable_legacy_pic(void);

acpi_interrupt_error_t acpi_interrupt_validate_configuration(
    acpi_validation_result_t *validation);

bool acpi_interrupt_is_initialized(void);

bool acpi_interrupt_has_legacy_pic(void);

uint32_t acpi_interrupt_get_local_apic_base(void);

static inline const char* acpi_interrupt_error_to_string(acpi_interrupt_error_t error) {
    switch (error) {
        case ACPI_INT_SUCCESS:
            return "Success";
        case ACPI_INT_ERROR_NO_MADT:
            return "MADT (Multiple APIC Description Table) not found";
        case ACPI_INT_ERROR_INVALID_CHECKSUM:
            return "Invalid ACPI table checksum";
        case ACPI_INT_ERROR_NO_LOCAL_APIC:
            return "No Local APIC entries found";
        case ACPI_INT_ERROR_NO_IO_APIC:
            return "No I/O APIC entries found";
        case ACPI_INT_ERROR_NOT_INITIALIZED:
            return "ACPI interrupt routing not initialized";
        case ACPI_INT_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case ACPI_INT_ERROR_DEVICE_NOT_FOUND:
            return "Device not found in ACPI tables";
        case ACPI_INT_ERROR_GSI_NOT_AVAILABLE:
            return "Global System Interrupt not available";
        case ACPI_INT_ERROR_CONFIGURATION_FAILED:
            return "Interrupt configuration failed";
        default:
            return "Unknown ACPI interrupt error";
    }
}

typedef enum {
    ACPI_POLARITY_CONFORMS = 0,
    ACPI_POLARITY_ACTIVE_HIGH = 1,
    ACPI_POLARITY_RESERVED = 2,
    ACPI_POLARITY_ACTIVE_LOW = 3
} acpi_interrupt_polarity_t;

typedef enum {
    ACPI_TRIGGER_CONFORMS = 0,
    ACPI_TRIGGER_EDGE = 1,
    ACPI_TRIGGER_RESERVED = 2,
    ACPI_TRIGGER_LEVEL = 3
} acpi_interrupt_trigger_mode_t;

static inline acpi_interrupt_polarity_t acpi_get_interrupt_polarity(uint16_t flags) {
    return (acpi_interrupt_polarity_t)(flags & 0x3);
}

static inline acpi_interrupt_trigger_mode_t acpi_get_interrupt_trigger_mode(uint16_t flags) {
    return (acpi_interrupt_trigger_mode_t)((flags >> 2) & 0x3);
}

static inline bool acpi_interrupt_is_active_low(uint16_t flags) {
    acpi_interrupt_polarity_t polarity = acpi_get_interrupt_polarity(flags);
    return polarity == ACPI_POLARITY_ACTIVE_LOW;
}

static inline bool acpi_interrupt_is_level_triggered(uint16_t flags) {
    acpi_interrupt_trigger_mode_t trigger = acpi_get_interrupt_trigger_mode(flags);
    return trigger == ACPI_TRIGGER_LEVEL;
}

#endif // ACPI_INTERRUPT_ROUTING_H