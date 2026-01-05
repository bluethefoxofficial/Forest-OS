#ifndef TSC_CALIBRATION_H
#define TSC_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>

/*
 * TSC (Time Stamp Counter) calibration for precise timing
 * Provides frequency detection and time conversion functions
 */

// TSC calibration methods
typedef enum {
    TSC_CALIB_PIT = 0,      // Use Programmable Interval Timer
    TSC_CALIB_APIC,         // Use APIC timer
    TSC_CALIB_HPET,         // Use High Precision Event Timer
    TSC_CALIB_ACPI_PM,      // Use ACPI Power Management timer
    TSC_CALIB_CPUID,        // Use CPUID instruction (newer CPUs)
    TSC_CALIB_MSR           // Use MSR (Model Specific Register)
} tsc_calibration_method_t;

// TSC capabilities
typedef struct {
    bool tsc_available;
    bool tsc_constant;      // TSC rate is constant (invariant TSC)
    bool tsc_nonstop;       // TSC doesn't stop in deep C-states
    bool tsc_deadline;      // TSC-deadline timer available
    uint64_t frequency_hz;
    uint32_t crystal_freq;  // Base crystal frequency (if available)
    uint32_t ratio_num;     // TSC frequency ratio numerator
    uint32_t ratio_den;     // TSC frequency ratio denominator
} tsc_info_t;

// TSC calibration results
typedef struct {
    tsc_calibration_method_t method;
    uint64_t frequency_hz;
    uint64_t cycles_per_us;
    uint64_t cycles_per_ms;
    uint32_t calibration_time_ms;
    bool accurate;
    int error_ppm;          // Parts per million error estimate
} tsc_calibration_t;

// TSC initialization and calibration
int tsc_init(void);
int tsc_calibrate(void);
int tsc_calibrate_with_method(tsc_calibration_method_t method);
int tsc_recalibrate(void);

// TSC information
const tsc_info_t *tsc_get_info(void);
const tsc_calibration_t *tsc_get_calibration(void);
bool tsc_is_available(void);
bool tsc_is_constant_rate(void);

// TSC reading
static inline uint64_t tsc_read(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t tsc_read_ordered(void) {
    uint32_t lo, hi;
    __asm__ volatile ("lfence; rdtsc; lfence" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// Time conversion functions
uint64_t tsc_to_ns(uint64_t cycles);
uint64_t tsc_to_us(uint64_t cycles);
uint64_t tsc_to_ms(uint64_t cycles);
uint64_t ns_to_tsc(uint64_t nanoseconds);
uint64_t us_to_tsc(uint64_t microseconds);
uint64_t ms_to_tsc(uint64_t milliseconds);

// Frequency access
uint64_t tsc_get_frequency(void);
uint64_t tsc_get_cycles_per_ms(void);
uint64_t tsc_get_cycles_per_us(void);

// Timing utilities
void tsc_delay_ns(uint64_t nanoseconds);
void tsc_delay_us(uint64_t microseconds);
void tsc_delay_ms(uint64_t milliseconds);

// Calibration helpers
int tsc_calibrate_with_pit(void);
int tsc_calibrate_with_apic_timer(void);
int tsc_calibrate_with_hpet(void);
int tsc_calibrate_with_cpuid(void);

// TSC deadline timer (if supported)
bool tsc_deadline_timer_available(void);
int tsc_deadline_set(uint64_t deadline);
void tsc_deadline_disable(void);

// Performance monitoring
typedef struct {
    uint64_t start_tsc;
    uint64_t end_tsc;
    uint64_t duration_cycles;
    uint64_t duration_ns;
} tsc_measurement_t;

void tsc_measurement_start(tsc_measurement_t *measurement);
void tsc_measurement_end(tsc_measurement_t *measurement);

// CPU feature detection
bool tsc_check_cpu_features(void);
uint32_t tsc_get_max_basic_cpuid(void);

// Debugging and diagnostics
void tsc_print_info(void);
int tsc_verify_calibration(void);
int tsc_benchmark_methods(void);

#endif // TSC_CALIBRATION_H