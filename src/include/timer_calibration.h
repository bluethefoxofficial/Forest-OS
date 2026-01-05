#ifndef TIMER_CALIBRATION_H
#define TIMER_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_TIMER_SOURCES 8
#define MAX_TIMER_NAME_LENGTH 64

typedef enum {
    TIMER_CALIB_SUCCESS = 0,
    TIMER_CALIB_ERROR_NO_SOURCES,
    TIMER_CALIB_ERROR_NO_RELIABLE_SOURCE,
    TIMER_CALIB_ERROR_INVALID_PARAMS,
    TIMER_CALIB_ERROR_INVALID_SOURCES,
    TIMER_CALIB_ERROR_VALIDATION_FAILED,
    TIMER_CALIB_ERROR_NOT_CALIBRATED,
    TIMER_CALIB_ERROR_INSUFFICIENT_DATA,
    TIMER_CALIB_ERROR_HARDWARE_FAULT
} timer_calibration_error_t;

typedef enum {
    TIMER_SOURCE_TSC = 0,
    TIMER_SOURCE_APIC_TIMER,
    TIMER_SOURCE_HPET,
    TIMER_SOURCE_PIT,
    TIMER_SOURCE_RTC,
    TIMER_SOURCE_ACPI_PM,
    TIMER_SOURCE_UEFI_TIME,
    TIMER_SOURCE_UNKNOWN
} timer_source_type_t;

typedef struct {
    uint64_t detected_frequency;
    uint32_t confidence_level;
    const char *timer_source_name;
    uint64_t resolution_ns;
    bool is_monotonic;
    const char *calibration_method;
} timer_calibration_result_t;

typedef struct {
    double frequency_ratio;
    double expected_ratio;
    uint64_t error_ppm;
    bool validation_passed;
    uint32_t confidence_level;
} cross_validation_result_t;

typedef struct {
    uint64_t min_latency_ns;
    uint64_t max_latency_ns;
    uint64_t avg_latency_ns;
    size_t measurement_count;
} latency_benchmark_result_t;

typedef struct {
    timer_source_type_t type;
    char name[MAX_TIMER_NAME_LENGTH];
    uint64_t frequency;
    uint64_t resolution_ns;
    uint32_t confidence_level;
    bool is_available;
} timer_source_descriptor_t;

typedef struct {
    timer_source_descriptor_t sources[MAX_TIMER_SOURCES];
    size_t count;
} timer_source_list_t;

timer_calibration_error_t timer_calibration_init(void);

timer_calibration_error_t timer_calibration_auto_detect(timer_calibration_result_t *result);

timer_calibration_error_t timer_calibration_cross_validate(
    timer_source_type_t source1, 
    timer_source_type_t source2,
    cross_validation_result_t *result);

timer_calibration_error_t timer_calibration_benchmark_latency(latency_benchmark_result_t *result);

timer_calibration_error_t timer_calibration_get_system_frequency(uint64_t *frequency);

timer_calibration_error_t timer_calibration_get_available_sources(timer_source_list_t *sources);

bool timer_calibration_is_completed(void);

void timer_calibration_reset(void);

static inline const char* timer_calibration_error_to_string(timer_calibration_error_t error) {
    switch (error) {
        case TIMER_CALIB_SUCCESS:
            return "Success";
        case TIMER_CALIB_ERROR_NO_SOURCES:
            return "No timer sources available";
        case TIMER_CALIB_ERROR_NO_RELIABLE_SOURCE:
            return "No reliable timer source found";
        case TIMER_CALIB_ERROR_INVALID_PARAMS:
            return "Invalid parameters";
        case TIMER_CALIB_ERROR_INVALID_SOURCES:
            return "Invalid timer sources";
        case TIMER_CALIB_ERROR_VALIDATION_FAILED:
            return "Calibration validation failed";
        case TIMER_CALIB_ERROR_NOT_CALIBRATED:
            return "Timer not calibrated";
        case TIMER_CALIB_ERROR_INSUFFICIENT_DATA:
            return "Insufficient calibration data";
        case TIMER_CALIB_ERROR_HARDWARE_FAULT:
            return "Hardware fault detected";
        default:
            return "Unknown error";
    }
}

static inline const char* timer_source_type_to_string(timer_source_type_t type) {
    switch (type) {
        case TIMER_SOURCE_TSC:
            return "Time Stamp Counter";
        case TIMER_SOURCE_APIC_TIMER:
            return "Local APIC Timer";
        case TIMER_SOURCE_HPET:
            return "High Precision Event Timer";
        case TIMER_SOURCE_PIT:
            return "Programmable Interval Timer";
        case TIMER_SOURCE_RTC:
            return "Real Time Clock";
        case TIMER_SOURCE_ACPI_PM:
            return "ACPI Power Management Timer";
        case TIMER_SOURCE_UEFI_TIME:
            return "UEFI Time Services";
        default:
            return "Unknown Timer Source";
    }
}

#endif // TIMER_CALIBRATION_H