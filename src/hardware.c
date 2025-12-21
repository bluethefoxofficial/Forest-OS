#include "include/hardware.h"
#include "include/system.h"
#include "include/util.h"

#define FEATURE_SUMMARY_CAPACITY  (sizeof(g_cpuid_info.feature_summary))

typedef enum {
    FEATURE_REG_BASIC_ECX,
    FEATURE_REG_BASIC_EDX,
    FEATURE_REG_EXT_ECX,
    FEATURE_REG_EXT_EDX,
    FEATURE_REG_STRUCT_EBX,
    FEATURE_REG_STRUCT_ECX
} feature_register_t;

typedef struct {
    const char* name;
    feature_register_t reg;
    uint8 bit;
} feature_descriptor_t;

static cpuid_info_t g_cpuid_info;
static bool g_hw_initialized = false;

static uint32 str_len(const char* str) {
    if (!str) {
        return 0;
    }
    uint32 len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

static void copy_ascii(char* dest, const char* src, uint32 capacity) {
    if (!dest || !src || capacity == 0) {
        return;
    }
    uint32 i = 0;
    for (; i < capacity - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

static void trim_trailing_spaces(char* str) {
    if (!str) {
        return;
    }
    int len = (int)str_len(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t')) {
        str[len - 1] = '\0';
        len--;
    }
}

static bool cpuid_supported(void) {
    uint32 original_eflags;
    uint32 toggled_eflags;
    
    __asm__ __volatile__(
        "pushfl\n\t"
        "popl %0"
        : "=r"(original_eflags)
    );
    
    toggled_eflags = original_eflags ^ (1 << 21);
    
    __asm__ __volatile__(
        "pushl %0\n\t"
        "popfl"
        :
        : "r"(toggled_eflags)
    );
    
    uint32 new_eflags;
    __asm__ __volatile__(
        "pushfl\n\t"
        "popl %0"
        : "=r"(new_eflags)
    );
    
    return ((new_eflags ^ original_eflags) & (1 << 21)) != 0;
}

static void cpuid_exec(uint32 leaf, uint32 subleaf, cpuid_regs_t* out) {
    if (!out) {
        return;
    }
    __asm__ __volatile__(
        "cpuid"
        : "=a"(out->eax), "=b"(out->ebx), "=c"(out->ecx), "=d"(out->edx)
        : "a"(leaf), "c"(subleaf)
    );
}

static void append_feature(char* buffer, uint32 capacity, const char* feature, bool* first) {
    if (!buffer || !feature || capacity == 0) {
        return;
    }
    
    uint32 current_len = str_len(buffer);
    uint32 feature_len = str_len(feature);
    uint32 needed = feature_len + (current_len == 0 ? 0 : 2);
    
    if (current_len + needed + 1 >= capacity) {
        return;
    }
    
    if (!(*first)) {
        buffer[current_len++] = ',';
        buffer[current_len++] = ' ';
    } else {
        *first = false;
    }
    
    for (uint32 i = 0; i < feature_len; i++) {
        buffer[current_len + i] = feature[i];
    }
    buffer[current_len + feature_len] = '\0';
}

static uint32* resolve_feature_register(feature_register_t reg, cpuid_feature_bits_t* bits) {
    if (!bits) {
        return 0;
    }
    switch (reg) {
        case FEATURE_REG_BASIC_ECX:
            return &bits->basic_ecx;
        case FEATURE_REG_BASIC_EDX:
            return &bits->basic_edx;
        case FEATURE_REG_EXT_ECX:
            return &bits->extended_ecx;
        case FEATURE_REG_EXT_EDX:
            return &bits->extended_edx;
        case FEATURE_REG_STRUCT_EBX:
            return &bits->structured_ebx;
        case FEATURE_REG_STRUCT_ECX:
            return &bits->structured_ecx;
        default:
            return 0;
    }
}

static void build_feature_summary(void) {
    static const feature_descriptor_t features[] = {
        {"FPU", FEATURE_REG_BASIC_EDX, 0},
        {"TSC", FEATURE_REG_BASIC_EDX, 4},
        {"PSE", FEATURE_REG_BASIC_EDX, 3},
        {"MSR", FEATURE_REG_BASIC_EDX, 5},
        {"PAE", FEATURE_REG_BASIC_EDX, 6},
        {"APIC", FEATURE_REG_BASIC_EDX, 9},
        {"SSE", FEATURE_REG_BASIC_EDX, 25},
        {"SSE2", FEATURE_REG_BASIC_EDX, 26},
        {"HTT", FEATURE_REG_BASIC_EDX, 28},
        {"SSE3", FEATURE_REG_BASIC_ECX, 0},
        {"PCLMUL", FEATURE_REG_BASIC_ECX, 1},
        {"MONITOR", FEATURE_REG_BASIC_ECX, 3},
        {"VMX", FEATURE_REG_BASIC_ECX, 5},
        {"SMX", FEATURE_REG_BASIC_ECX, 6},
        {"SSSE3", FEATURE_REG_BASIC_ECX, 9},
        {"FMA", FEATURE_REG_BASIC_ECX, 12},
        {"CX16", FEATURE_REG_BASIC_ECX, 13},
        {"SSE4.1", FEATURE_REG_BASIC_ECX, 19},
        {"SSE4.2", FEATURE_REG_BASIC_ECX, 20},
        {"MOVBE", FEATURE_REG_BASIC_ECX, 22},
        {"POPCNT", FEATURE_REG_BASIC_ECX, 23},
        {"AES", FEATURE_REG_BASIC_ECX, 25},
        {"XSAVE", FEATURE_REG_BASIC_ECX, 26},
        {"AVX", FEATURE_REG_BASIC_ECX, 28},
        {"F16C", FEATURE_REG_BASIC_ECX, 29},
        {"RDRAND", FEATURE_REG_BASIC_ECX, 30},
        {"Hypervisor", FEATURE_REG_BASIC_ECX, 31},
        {"AVX2", FEATURE_REG_STRUCT_EBX, 5},
        {"BMI1", FEATURE_REG_STRUCT_EBX, 3},
        {"BMI2", FEATURE_REG_STRUCT_EBX, 8},
        {"SMEP", FEATURE_REG_STRUCT_EBX, 7},
        {"ERMS", FEATURE_REG_STRUCT_EBX, 9},
        {"INVPCID", FEATURE_REG_STRUCT_EBX, 10},
        {"RDSEED", FEATURE_REG_STRUCT_EBX, 18},
        {"ADX", FEATURE_REG_STRUCT_EBX, 19},
        {"SMAP", FEATURE_REG_STRUCT_EBX, 20},
        {"CLFLUSHOPT", FEATURE_REG_STRUCT_EBX, 23},
        {"SHA", FEATURE_REG_STRUCT_EBX, 29},
        {"PREFETCHWT1", FEATURE_REG_STRUCT_ECX, 0}
    };
    
    g_cpuid_info.feature_summary[0] = '\0';
    if (!g_cpuid_info.cpuid_supported) {
        copy_ascii(g_cpuid_info.feature_summary, "CPUID unsupported", FEATURE_SUMMARY_CAPACITY);
        return;
    }
    
    bool first = true;
    cpuid_feature_bits_t* bits = &g_cpuid_info.features;
    
    for (uint32 i = 0; i < (sizeof(features) / sizeof(features[0])); i++) {
        uint32* reg = resolve_feature_register(features[i].reg, bits);
        if (!reg) {
            continue;
        }
        if (*reg & (1u << features[i].bit)) {
            append_feature(g_cpuid_info.feature_summary, FEATURE_SUMMARY_CAPACITY, features[i].name, &first);
        }
    }
    
    if (g_cpuid_info.feature_summary[0] == '\0') {
        copy_ascii(g_cpuid_info.feature_summary, "No feature flags detected", FEATURE_SUMMARY_CAPACITY);
    }
}

static void detect_vendor(void) {
    cpuid_regs_t regs;
    cpuid_exec(0, 0, &regs);
    
    g_cpuid_info.max_basic_leaf = regs.eax;
    
    ((uint32*)g_cpuid_info.vendor_id)[0] = regs.ebx;
    ((uint32*)g_cpuid_info.vendor_id)[1] = regs.edx;
    ((uint32*)g_cpuid_info.vendor_id)[2] = regs.ecx;
    g_cpuid_info.vendor_id[12] = '\0';
}

static void detect_basic_features(void) {
    cpuid_regs_t regs;
    cpuid_exec(1, 0, &regs);
    
    g_cpuid_info.signature = regs.eax;
    g_cpuid_info.stepping = regs.eax & 0xF;
    g_cpuid_info.model = (regs.eax >> 4) & 0xF;
    g_cpuid_info.family = (regs.eax >> 8) & 0xF;
    g_cpuid_info.extended_model = (regs.eax >> 16) & 0xF;
    g_cpuid_info.extended_family = (regs.eax >> 20) & 0xFF;
    
    if (g_cpuid_info.family == 6 || g_cpuid_info.family == 15) {
        g_cpuid_info.model += (g_cpuid_info.extended_model << 4);
    }
    if (g_cpuid_info.family == 15) {
        g_cpuid_info.family += g_cpuid_info.extended_family;
    }
    
    g_cpuid_info.apic_id = (regs.ebx >> 24) & 0xFF;
    g_cpuid_info.logical_processor_count = (regs.ebx >> 16) & 0xFF;
    
    g_cpuid_info.features.basic_ecx = regs.ecx;
    g_cpuid_info.features.basic_edx = regs.edx;
    
    g_cpuid_info.hypervisor_present = (regs.ecx & CPUID_FEAT_ECX_HYPERVISOR) != 0;
}

static void detect_structured_features(void) {
    if (g_cpuid_info.max_basic_leaf < 7) {
        g_cpuid_info.features.structured_ebx = 0;
        g_cpuid_info.features.structured_ecx = 0;
        return;
    }
    
    cpuid_regs_t regs;
    cpuid_exec(7, 0, &regs);
    g_cpuid_info.features.structured_ebx = regs.ebx;
    g_cpuid_info.features.structured_ecx = regs.ecx;
}

static void detect_extended_features(void) {
    cpuid_regs_t regs;
    cpuid_exec(0x80000000, 0, &regs);
    g_cpuid_info.max_extended_leaf = regs.eax;
    
    if (g_cpuid_info.max_extended_leaf >= 0x80000001) {
        cpuid_exec(0x80000001, 0, &regs);
        g_cpuid_info.features.extended_ecx = regs.ecx;
        g_cpuid_info.features.extended_edx = regs.edx;
    }
    
    if (g_cpuid_info.max_extended_leaf >= 0x80000004) {
        uint32* brand_words = (uint32*)g_cpuid_info.brand_string;
        for (uint32 i = 0; i < 3; i++) {
            cpuid_exec(0x80000002 + i, 0, &regs);
            brand_words[i * 4 + 0] = regs.eax;
            brand_words[i * 4 + 1] = regs.ebx;
            brand_words[i * 4 + 2] = regs.ecx;
            brand_words[i * 4 + 3] = regs.edx;
        }
        g_cpuid_info.brand_string[48] = '\0';
        trim_trailing_spaces(g_cpuid_info.brand_string);
    } else {
        copy_ascii(g_cpuid_info.brand_string, "Generic x86 CPU", sizeof(g_cpuid_info.brand_string));
    }
}

bool hardware_detect_init(void) {
    if (g_hw_initialized) {
        return g_cpuid_info.cpuid_supported;
    }
    
    g_hw_initialized = true;
    memory_set((uint8*)&g_cpuid_info, 0, sizeof(g_cpuid_info));
    
    g_cpuid_info.cpuid_supported = cpuid_supported();
    if (!g_cpuid_info.cpuid_supported) {
        copy_ascii(g_cpuid_info.vendor_id, "Unavailable", sizeof(g_cpuid_info.vendor_id));
        copy_ascii(g_cpuid_info.brand_string, "CPUID unsupported platform", sizeof(g_cpuid_info.brand_string));
        copy_ascii(g_cpuid_info.feature_summary, "CPUID unsupported", FEATURE_SUMMARY_CAPACITY);
        return false;
    }
    
    detect_vendor();
    detect_basic_features();
    detect_structured_features();
    detect_extended_features();
    build_feature_summary();
    
    return true;
}

const cpuid_info_t* hardware_get_cpuid_info(void) {
    if (!g_hw_initialized) {
        hardware_detect_init();
    }
    return &g_cpuid_info;
}

const char* hardware_get_feature_summary(void) {
    const cpuid_info_t* info = hardware_get_cpuid_info();
    if (info->feature_summary[0] == '\0') {
        return "No feature data";
    }
    return info->feature_summary;
}

bool hardware_cpuid_supported(void) {
    return hardware_get_cpuid_info()->cpuid_supported;
}

bool hardware_cpu_has_tsc(void) {
    const cpuid_info_t* info = hardware_get_cpuid_info();
    if (!info->cpuid_supported) {
        return false;
    }
    return (info->features.basic_edx & CPUID_FEAT_EDX_TSC) != 0;
}
