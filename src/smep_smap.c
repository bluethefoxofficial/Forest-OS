#include <stdint.h>
#include <stdbool.h>
#include "include/screen.h"

// =============================================================================
// SUPERVISOR MEMORY ACCESS/EXECUTE PROTECTION (SMAP/SMEP)
// =============================================================================
// Hardware kernel security features to prevent exploitation
// Based on Intel documentation from your provided materials
// =============================================================================

#define CPUID_SMEP_BIT      7   // CPUID leaf 7, EBX bit 7
#define CPUID_SMAP_BIT      20  // CPUID leaf 7, EBX bit 20
#define CR4_SMEP_BIT        20  // CR4 bit 20
#define CR4_SMAP_BIT        21  // CR4 bit 21

// Global state
static bool smep_available = false;
static bool smap_available = false;
static bool smep_enabled = false;
static bool smap_enabled = false;

// CPUID helper function
static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

// Set bit in CR4 register
static void cpu_cr4_set_bit(int bit) {
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1U << bit);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

// Clear bit in CR4 register
static void cpu_cr4_clear_bit(int bit) {
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~(1U << bit);
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
}

// Set AC flag in RFLAGS register (enables user memory access)
static inline void cpu_flags_set_ac(void) {
    __asm__ volatile("stac" ::: "cc");
}

// Clear AC flag in RFLAGS register (disables user memory access)
static inline void cpu_flags_clear_ac(void) {
    __asm__ volatile("clac" ::: "cc");
}

// Check if SMEP and SMAP are supported
void smep_smap_detect_features(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check if extended feature enumeration is supported
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) {
        print("[SMEP/SMAP] Extended features not supported\n");
        return;
    }
    
    // Get structured extended feature flags
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    
    // Check SMEP support (EBX bit 7)
    smep_available = (ebx & (1U << CPUID_SMEP_BIT)) != 0;
    
    // Check SMAP support (EBX bit 20)  
    smap_available = (ebx & (1U << CPUID_SMAP_BIT)) != 0;
    
    print("[SMEP/SMAP] Feature detection:\n");
    print("  SMEP: ");
    print(smep_available ? "Available" : "Not available");
    print("\n");
    print("  SMAP: ");
    print(smap_available ? "Available" : "Not available");
    print("\n");
}

// Initialize SMEP and SMAP protections
void supervisor_memory_protection_init(void) {
    print("[SMEP/SMAP] Initializing supervisor memory protection...\n");
    
    // Detect features first
    smep_smap_detect_features();
    
    // Enable SMEP if available
    if (smep_available) {
        cpu_cr4_set_bit(CR4_SMEP_BIT);
        smep_enabled = true;
        print("[SMEP/SMAP] SMEP enabled - kernel cannot execute user pages\n");
    }
    
    // Enable SMAP if available
    if (smap_available) {
        cpu_cr4_set_bit(CR4_SMAP_BIT);
        smap_enabled = true;
        print("[SMEP/SMAP] SMAP enabled - kernel cannot access user pages without AC flag\n");
    }
    
    if (!smep_available && !smap_available) {
        print("[SMEP/SMAP] Warning: No hardware memory protection available\n");
    }
}

// Safe copy from/to user space with SMAP handling
void *safe_user_memcpy(void *destination, const void *source, size_t size) {
    if (!smap_enabled) {
        // No SMAP protection, use normal memcpy
        extern void* memcpy(void* dest, const void* src, size_t n);
        return memcpy(destination, source, size);
    }
    
    // Disable SMAP protections temporarily
    cpu_flags_set_ac();
    
    // Perform the copy
    extern void* memcpy(void* dest, const void* src, size_t n);
    void *result = memcpy(destination, source, size);
    
    // Restore SMAP protections
    cpu_flags_clear_ac();
    
    return result;
}

// Safe string copy from user space
int safe_user_strcpy(char *dest, const char *src, size_t max_len) {
    if (!smap_enabled) {
        // No SMAP protection, use normal string copy
        extern char* strncpy(char* dest, const char* src, size_t n);
        strncpy(dest, src, max_len - 1);
        dest[max_len - 1] = '\0';
        return 0;
    }
    
    // Disable SMAP protections temporarily
    cpu_flags_set_ac();
    
    size_t i;
    for (i = 0; i < max_len - 1; i++) {
        dest[i] = src[i];
        if (src[i] == '\0') {
            break;
        }
    }
    dest[i] = '\0';
    
    // Restore SMAP protections
    cpu_flags_clear_ac();
    
    return 0;
}

// Validate that an address is in user space (not kernel)
bool is_user_address(uint32_t addr) {
    // Assuming kernel space starts at 0xC0000000 (3GB split)
    return addr < 0xC0000000;
}

// Safe user memory validation
bool safe_user_memory_check(const void *ptr, size_t size) {
    uint32_t start = (uint32_t)ptr;
    uint32_t end = start + size;
    
    // Check for overflow
    if (end < start) {
        return false;
    }
    
    // Check if entirely in user space
    if (!is_user_address(start) || !is_user_address(end - 1)) {
        return false;
    }
    
    return true;
}

// Enable user memory access (for system calls)
void enable_user_access(void) {
    if (smap_enabled) {
        cpu_flags_set_ac();
    }
}

// Disable user memory access (restore protection)
void disable_user_access(void) {
    if (smap_enabled) {
        cpu_flags_clear_ac();
    }
}

// Get protection status
bool smep_is_enabled(void) { return smep_enabled; }
bool smap_is_enabled(void) { return smap_enabled; }
bool smep_is_available(void) { return smep_available; }
bool smap_is_available(void) { return smap_available; }

// Disable protections for debugging (use with caution!)
void debug_disable_smep_smap(void) {
    print("[SMEP/SMAP] WARNING: Disabling memory protection for debugging\n");
    
    if (smep_enabled) {
        cpu_cr4_clear_bit(CR4_SMEP_BIT);
        smep_enabled = false;
    }
    
    if (smap_enabled) {
        cpu_cr4_clear_bit(CR4_SMAP_BIT);
        smap_enabled = false;
    }
}

// Re-enable protections after debugging
void debug_enable_smep_smap(void) {
    print("[SMEP/SMAP] Re-enabling memory protection\n");
    
    if (smep_available && !smep_enabled) {
        cpu_cr4_set_bit(CR4_SMEP_BIT);
        smep_enabled = true;
    }
    
    if (smap_available && !smap_enabled) {
        cpu_cr4_set_bit(CR4_SMAP_BIT);
        smap_enabled = true;
    }
}