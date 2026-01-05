#include "timer_calibration.h"
#include "interrupt_management.h"
#include "timer_abstraction.h"
#include "tsc_calibration.h"
#include "apic.h"
#include "hpet.h"
#include "pit.h"
#include "rtc.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CALIBRATION_ITERATIONS 10
#define CALIBRATION_DURATION_MS 100
#define CALIBRATION_TOLERANCE_PPM 1000
#define CONFIDENCE_THRESHOLD 95

typedef struct {
    timer_source_type_t type;
    const char *name;
    uint64_t frequency;
    uint64_t resolution_ns;
    bool is_monotonic;
    bool is_continuous;
    uint32_t confidence_level;
    uint64_t (*read_counter)(void);
    void (*configure)(uint64_t frequency);
    bool (*is_available)(void);
} timer_source_info_t;

typedef struct {
    uint64_t frequency;
    uint64_t deviation;
    uint32_t confidence;
    uint64_t samples[CALIBRATION_ITERATIONS];
} calibration_result_t;

typedef struct {
    timer_source_info_t sources[MAX_TIMER_SOURCES];
    size_t source_count;
    timer_source_type_t primary_source;
    timer_source_type_t reference_source;
    bool calibration_complete;
    uint64_t system_frequency;
} timer_calibration_context_t;

static timer_calibration_context_t calibration_ctx = {0};

static uint64_t read_tsc_counter(void) {
    uint32_t low, high;
    __asm__ volatile ("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)high << 32) | low;
}

static uint64_t read_apic_timer_counter(void) {
    return apic_timer_get_time_ns();
}

static uint64_t read_hpet_counter(void) {
    return hpet_read_main_counter();
}

static uint64_t read_pit_counter(void) {
    return pit_read_counter();
}

static uint64_t read_rtc_counter(void) {
    return rtc_read_time_us();
}

static bool is_tsc_available(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (1));
    return (edx & (1 << 4)) != 0;
}

static bool is_apic_timer_available(void) {
    return apic_timer_is_available();
}

static bool is_hpet_available(void) {
    return hpet_is_available();
}

static bool is_pit_available(void) {
    return pit_is_available();
}

static bool is_rtc_available(void) {
    return rtc_is_available();
}

static void configure_hpet(uint64_t frequency) {
    hpet_configure_periodic(frequency);
}

static void configure_pit(uint64_t frequency) {
    pit_configure(frequency);
}

static void init_timer_sources(void) {
    calibration_ctx.source_count = 0;
    
    timer_source_info_t sources[] = {
        {
            .type = TIMER_SOURCE_TSC,
            .name = "Time Stamp Counter",
            .frequency = 0,
            .resolution_ns = 1,
            .is_monotonic = true,
            .is_continuous = true,
            .confidence_level = 90,
            .read_counter = read_tsc_counter,
            .configure = NULL,
            .is_available = is_tsc_available
        },
        {
            .type = TIMER_SOURCE_APIC_TIMER,
            .name = "Local APIC Timer",
            .frequency = 0,
            .resolution_ns = 10,
            .is_monotonic = true,
            .is_continuous = true,
            .confidence_level = 95,
            .read_counter = read_apic_timer_counter,
            .configure = NULL,
            .is_available = is_apic_timer_available
        },
        {
            .type = TIMER_SOURCE_HPET,
            .name = "High Precision Event Timer",
            .frequency = 0,
            .resolution_ns = 1,
            .is_monotonic = true,
            .is_continuous = true,
            .confidence_level = 99,
            .read_counter = read_hpet_counter,
            .configure = configure_hpet,
            .is_available = is_hpet_available
        },
        {
            .type = TIMER_SOURCE_PIT,
            .name = "Programmable Interval Timer",
            .frequency = 1193182,
            .resolution_ns = 838,
            .is_monotonic = true,
            .is_continuous = false,
            .confidence_level = 85,
            .read_counter = read_pit_counter,
            .configure = configure_pit,
            .is_available = is_pit_available
        },
        {
            .type = TIMER_SOURCE_RTC,
            .name = "Real Time Clock",
            .frequency = 32768,
            .resolution_ns = 30517,
            .is_monotonic = true,
            .is_continuous = true,
            .confidence_level = 70,
            .read_counter = read_rtc_counter,
            .configure = NULL,
            .is_available = is_rtc_available
        }
    };
    
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]) && 
         calibration_ctx.source_count < MAX_TIMER_SOURCES; i++) {
        if (sources[i].is_available && sources[i].is_available()) {
            calibration_ctx.sources[calibration_ctx.source_count] = sources[i];
            calibration_ctx.source_count++;
        }
    }
}

static uint64_t calculate_frequency_cross_reference(
    timer_source_info_t *target, 
    timer_source_info_t *reference,
    calibration_result_t *result) {
    
    uint64_t target_start, target_end, target_delta;
    uint64_t ref_start, ref_end, ref_delta;
    uint64_t frequencies[CALIBRATION_ITERATIONS];
    uint64_t total_frequency = 0;
    
    for (int i = 0; i < CALIBRATION_ITERATIONS; i++) {
        target_start = target->read_counter();
        ref_start = reference->read_counter();
        
        volatile uint32_t delay = 0;
        for (uint32_t j = 0; j < 1000000; j++) {
            delay += j;
        }
        
        target_end = target->read_counter();
        ref_end = reference->read_counter();
        
        target_delta = target_end - target_start;
        ref_delta = ref_end - ref_start;
        
        if (ref_delta == 0 || target_delta == 0) {
            continue;
        }
        
        frequencies[i] = (target_delta * reference->frequency) / ref_delta;
        total_frequency += frequencies[i];
        result->samples[i] = frequencies[i];
    }
    
    uint64_t average_frequency = total_frequency / CALIBRATION_ITERATIONS;
    
    uint64_t variance = 0;
    for (int i = 0; i < CALIBRATION_ITERATIONS; i++) {
        uint64_t diff = frequencies[i] > average_frequency ? 
            frequencies[i] - average_frequency : 
            average_frequency - frequencies[i];
        variance += diff * diff;
    }
    variance /= CALIBRATION_ITERATIONS;
    
    result->frequency = average_frequency;
    result->deviation = variance;
    result->confidence = (variance < (average_frequency / 1000)) ? 95 : 
                        (variance < (average_frequency / 100)) ? 80 : 50;
    
    return average_frequency;
}

static uint64_t calibrate_using_known_intervals(timer_source_info_t *timer) {
    const uint64_t known_intervals_us[] = {1000, 5000, 10000, 50000, 100000};
    const size_t interval_count = sizeof(known_intervals_us) / sizeof(known_intervals_us[0]);
    
    uint64_t frequencies[interval_count];
    uint64_t total_frequency = 0;
    
    for (size_t i = 0; i < interval_count; i++) {
        uint64_t start_time = timer->read_counter();
        
        uint64_t start_tsc = read_tsc_counter();
        while ((read_tsc_counter() - start_tsc) < (known_intervals_us[i] * 2000)) {
            __asm__ volatile ("pause");
        }
        
        uint64_t end_time = timer->read_counter();
        uint64_t elapsed_ticks = end_time - start_time;
        
        frequencies[i] = (elapsed_ticks * 1000000) / known_intervals_us[i];
        total_frequency += frequencies[i];
    }
    
    return total_frequency / interval_count;
}

static int compare_timer_sources(const timer_source_info_t *timer_a,
                                 const timer_source_info_t *timer_b) {
    int score_a = timer_a->confidence_level;
    int score_b = timer_b->confidence_level;
    
    if (timer_a->is_continuous) score_a += 10;
    if (timer_b->is_continuous) score_b += 10;
    
    if (timer_a->resolution_ns < 100) score_a += 5;
    if (timer_b->resolution_ns < 100) score_b += 5;
    
    return score_b - score_a;
}

static void sort_timer_sources(timer_source_info_t *sources, size_t count) {
    for (size_t i = 1; i < count; i++) {
        timer_source_info_t key = sources[i];
        size_t j = i;
        
        while (j > 0 && compare_timer_sources(&key, &sources[j - 1]) < 0) {
            sources[j] = sources[j - 1];
            j--;
        }
        
        sources[j] = key;
    }
}

static void select_optimal_timer_sources(void) {
    if (calibration_ctx.source_count < 2) {
        calibration_ctx.primary_source = TIMER_SOURCE_UNKNOWN;
        calibration_ctx.reference_source = TIMER_SOURCE_UNKNOWN;
        return;
    }
    
    sort_timer_sources(calibration_ctx.sources, calibration_ctx.source_count);
    
    calibration_ctx.primary_source = calibration_ctx.sources[0].type;
    calibration_ctx.reference_source = calibration_ctx.sources[1].type;
}

static bool validate_calibration_result(calibration_result_t *result, uint64_t expected_frequency) {
    if (result->confidence < CONFIDENCE_THRESHOLD) {
        return false;
    }
    
    if (expected_frequency > 0) {
        uint64_t diff = result->frequency > expected_frequency ? 
            result->frequency - expected_frequency : 
            expected_frequency - result->frequency;
        
        uint64_t ppm_error = (diff * 1000000) / expected_frequency;
        if (ppm_error > CALIBRATION_TOLERANCE_PPM) {
            return false;
        }
    }
    
    uint64_t min_sample = result->samples[0];
    uint64_t max_sample = result->samples[0];
    for (int i = 1; i < CALIBRATION_ITERATIONS; i++) {
        if (result->samples[i] < min_sample) min_sample = result->samples[i];
        if (result->samples[i] > max_sample) max_sample = result->samples[i];
    }
    
    uint64_t range_ppm = ((max_sample - min_sample) * 1000000) / result->frequency;
    return range_ppm < (CALIBRATION_TOLERANCE_PPM * 2);
}

timer_calibration_error_t timer_calibration_init(void) {
    memset(&calibration_ctx, 0, sizeof(calibration_ctx));
    
    init_timer_sources();
    
    if (calibration_ctx.source_count == 0) {
        return TIMER_CALIB_ERROR_NO_SOURCES;
    }
    
    select_optimal_timer_sources();
    
    if (calibration_ctx.primary_source == TIMER_SOURCE_UNKNOWN) {
        return TIMER_CALIB_ERROR_NO_RELIABLE_SOURCE;
    }
    
    return TIMER_CALIB_SUCCESS;
}

timer_calibration_error_t timer_calibration_auto_detect(timer_calibration_result_t *result) {
    if (!result) {
        return TIMER_CALIB_ERROR_INVALID_PARAMS;
    }
    
    memset(result, 0, sizeof(timer_calibration_result_t));
    
    timer_source_info_t *primary = NULL;
    timer_source_info_t *reference = NULL;
    
    for (size_t i = 0; i < calibration_ctx.source_count; i++) {
        if (calibration_ctx.sources[i].type == calibration_ctx.primary_source) {
            primary = &calibration_ctx.sources[i];
        }
        if (calibration_ctx.sources[i].type == calibration_ctx.reference_source) {
            reference = &calibration_ctx.sources[i];
        }
    }
    
    if (!primary) {
        return TIMER_CALIB_ERROR_NO_RELIABLE_SOURCE;
    }
    
    calibration_result_t primary_result = {0};
    
    if (reference && reference->frequency > 0) {
        calculate_frequency_cross_reference(primary, reference, &primary_result);
    } else {
        primary_result.frequency = calibrate_using_known_intervals(primary);
        primary_result.confidence = 75;
    }
    
    if (!validate_calibration_result(&primary_result, 0)) {
        return TIMER_CALIB_ERROR_VALIDATION_FAILED;
    }
    
    result->detected_frequency = primary_result.frequency;
    result->confidence_level = primary_result.confidence;
    result->timer_source_name = primary->name;
    result->resolution_ns = primary->resolution_ns;
    result->is_monotonic = primary->is_monotonic;
    result->calibration_method = reference ? "Cross-reference" : "Known intervals";
    
    primary->frequency = primary_result.frequency;
    calibration_ctx.system_frequency = primary_result.frequency;
    calibration_ctx.calibration_complete = true;
    
    return TIMER_CALIB_SUCCESS;
}

timer_calibration_error_t timer_calibration_cross_validate(
    timer_source_type_t source1, 
    timer_source_type_t source2,
    cross_validation_result_t *result) {
    
    if (!result) {
        return TIMER_CALIB_ERROR_INVALID_PARAMS;
    }
    
    timer_source_info_t *timer1 = NULL;
    timer_source_info_t *timer2 = NULL;
    
    for (size_t i = 0; i < calibration_ctx.source_count; i++) {
        if (calibration_ctx.sources[i].type == source1) {
            timer1 = &calibration_ctx.sources[i];
        }
        if (calibration_ctx.sources[i].type == source2) {
            timer2 = &calibration_ctx.sources[i];
        }
    }
    
    if (!timer1 || !timer2 || timer1->frequency == 0 || timer2->frequency == 0) {
        return TIMER_CALIB_ERROR_INVALID_SOURCES;
    }
    
    calibration_result_t cross_result = {0};
    calculate_frequency_cross_reference(timer1, timer2, &cross_result);
    
    uint64_t expected_ratio = (timer1->frequency * 1000000) / timer2->frequency;
    uint64_t actual_ratio = (cross_result.frequency * 1000000) / timer2->frequency;
    
    uint64_t ratio_diff = expected_ratio > actual_ratio ? 
        expected_ratio - actual_ratio : actual_ratio - expected_ratio;
    
    result->frequency_ratio = (double)actual_ratio / 1000000.0;
    result->expected_ratio = (double)expected_ratio / 1000000.0;
    result->error_ppm = (ratio_diff * 1000000) / expected_ratio;
    result->validation_passed = result->error_ppm < CALIBRATION_TOLERANCE_PPM;
    result->confidence_level = cross_result.confidence;
    
    return TIMER_CALIB_SUCCESS;
}

timer_calibration_error_t timer_calibration_benchmark_latency(latency_benchmark_result_t *result) {
    if (!result || !calibration_ctx.calibration_complete) {
        return TIMER_CALIB_ERROR_INVALID_PARAMS;
    }
    
    const size_t measurement_count = 1000;
    uint64_t latencies[measurement_count];
    
    for (size_t i = 0; i < measurement_count; i++) {
        uint64_t start = read_tsc_counter();
        uint64_t end = read_tsc_counter();
        
        latencies[i] = end - start;
    }
    
    uint64_t total_latency = 0;
    uint64_t min_latency = latencies[0];
    uint64_t max_latency = latencies[0];
    
    for (size_t i = 0; i < measurement_count; i++) {
        total_latency += latencies[i];
        if (latencies[i] < min_latency) min_latency = latencies[i];
        if (latencies[i] > max_latency) max_latency = latencies[i];
    }
    
    result->min_latency_ns = (min_latency * 1000000000) / calibration_ctx.system_frequency;
    result->max_latency_ns = (max_latency * 1000000000) / calibration_ctx.system_frequency;
    result->avg_latency_ns = (total_latency * 1000000000) / 
                           (measurement_count * calibration_ctx.system_frequency);
    result->measurement_count = measurement_count;
    
    return TIMER_CALIB_SUCCESS;
}

timer_calibration_error_t timer_calibration_get_system_frequency(uint64_t *frequency) {
    if (!frequency) {
        return TIMER_CALIB_ERROR_INVALID_PARAMS;
    }
    
    if (!calibration_ctx.calibration_complete) {
        return TIMER_CALIB_ERROR_NOT_CALIBRATED;
    }
    
    *frequency = calibration_ctx.system_frequency;
    return TIMER_CALIB_SUCCESS;
}

timer_calibration_error_t timer_calibration_get_available_sources(
    timer_source_list_t *sources) {
    
    if (!sources) {
        return TIMER_CALIB_ERROR_INVALID_PARAMS;
    }
    
    sources->count = calibration_ctx.source_count;
    
    for (size_t i = 0; i < calibration_ctx.source_count && i < MAX_TIMER_SOURCES; i++) {
        sources->sources[i].type = calibration_ctx.sources[i].type;
        strncpy(sources->sources[i].name, calibration_ctx.sources[i].name, 
               sizeof(sources->sources[i].name) - 1);
        sources->sources[i].name[sizeof(sources->sources[i].name) - 1] = '\0';
        sources->sources[i].frequency = calibration_ctx.sources[i].frequency;
        sources->sources[i].resolution_ns = calibration_ctx.sources[i].resolution_ns;
        sources->sources[i].confidence_level = calibration_ctx.sources[i].confidence_level;
        sources->sources[i].is_available = true;
    }
    
    return TIMER_CALIB_SUCCESS;
}

bool timer_calibration_is_completed(void) {
    return calibration_ctx.calibration_complete;
}

void timer_calibration_reset(void) {
    memset(&calibration_ctx, 0, sizeof(calibration_ctx));
}
