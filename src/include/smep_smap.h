#ifndef __SMEP_SMAP_H__
#define __SMEP_SMAP_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// SUPERVISOR MEMORY ACCESS/EXECUTE PROTECTION HEADER
// =============================================================================

// Initialization and detection
void smep_smap_detect_features(void);
void supervisor_memory_protection_init(void);

// Safe user memory access functions
void *safe_user_memcpy(void *destination, const void *source, size_t size);
int safe_user_strcpy(char *dest, const char *src, size_t max_len);

// User memory validation
bool is_user_address(uint32_t addr);
bool safe_user_memory_check(const void *ptr, size_t size);

// Access control
void enable_user_access(void);
void disable_user_access(void);

// Status queries
bool smep_is_enabled(void);
bool smap_is_enabled(void);
bool smep_is_available(void);
bool smap_is_available(void);

// Debug functions (use with extreme caution)
void debug_disable_smep_smap(void);
void debug_enable_smep_smap(void);

// Convenience macros for system calls
#define USER_ACCESS_BEGIN() enable_user_access()
#define USER_ACCESS_END()   disable_user_access()

// Safe user access wrapper macro
#define WITH_USER_ACCESS(code) do { \
    enable_user_access(); \
    code; \
    disable_user_access(); \
} while(0)

#endif /* __SMEP_SMAP_H__ */