#ifndef MEMORY_REGION_MANAGER_H
#define MEMORY_REGION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// INTELLIGENT MEMORY REGION MANAGER
// =============================================================================
// Provides intelligent detection and protection of critical system memory 
// regions including kernel areas, framebuffers, and hardware MMIO spaces.
// =============================================================================

// =============================================================================
// INITIALIZATION
// =============================================================================

// Initialize the memory region manager with default protected regions
void memory_region_manager_init(void);

// =============================================================================
// DYNAMIC REGION REGISTRATION
// =============================================================================

// Register an active framebuffer region for protection
void memory_region_register_framebuffer(uint32_t phys_addr, uint32_t size);

// Register a hardware MMIO region for protection  
void memory_region_register_mmio(uint32_t phys_addr, uint32_t size, const char* device_name);

// =============================================================================
// REGION ANALYSIS
// =============================================================================

// Get human-readable description of memory region containing the address
const char* memory_region_get_description(uint32_t addr);

// Check if address is in a critical system region
bool memory_region_is_critical(uint32_t addr);

// Check if address should be writable
bool memory_region_is_writable(uint32_t addr);

// =============================================================================
// PAGE FAULT INTEGRATION
// =============================================================================

// Analyze a page fault and provide detailed region information
void memory_analyze_page_fault(uint32_t fault_addr, uint32_t error_code,
                               char* analysis_buffer, size_t buffer_size);

// =============================================================================
// DEBUG AND REPORTING
// =============================================================================

// Dump information about all protected regions
void memory_region_dump_info(void);

#endif // MEMORY_REGION_MANAGER_H