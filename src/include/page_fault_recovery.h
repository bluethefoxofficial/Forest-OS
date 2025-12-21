#ifndef PAGE_FAULT_RECOVERY_H
#define PAGE_FAULT_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// INTELLIGENT PAGE FAULT RECOVERY SYSTEM
// =============================================================================
// Provides intelligent page fault recovery capabilities to prevent system
// crashes and handle memory issues gracefully.
// =============================================================================

// =============================================================================
// INITIALIZATION
// =============================================================================

// Initialize the page fault recovery system
void page_fault_recovery_init(void);

// =============================================================================
// RECOVERY FUNCTIONS
// =============================================================================

// Attempt to recover from a page fault gracefully
// Returns true if recovery was successful, false if panic is needed
bool page_fault_attempt_recovery(uint32_t fault_addr, uint32_t error_code);

// Enable/disable page fault recovery system
void page_fault_recovery_enable(bool enable);

// =============================================================================
// VALIDATION FUNCTIONS
// =============================================================================

// Validate if an address access should be allowed
bool page_fault_validate_address_access(uint32_t addr, bool is_write);

// Pre-map critical memory regions to prevent faults
void page_fault_premap_critical_regions(void);

// =============================================================================
// STATISTICS AND DEBUGGING
// =============================================================================

// Get recovery statistics
void page_fault_recovery_get_stats(uint32_t* total_recoveries, uint32_t* total_failures);

// Dump recovery history for debugging
void page_fault_recovery_dump_history(void);

#endif // PAGE_FAULT_RECOVERY_H