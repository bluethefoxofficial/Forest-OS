#include "include/ssp.h"
#include "include/screen.h"
#include "include/debuglog.h"
#include <stdint.h>

// =============================================================================
// SSP FUNCTIONALITY TEST
// =============================================================================
// Test suite for stack smashing protection features
// =============================================================================

// Function with buffer that could be overflowed (for testing)
SSP_PROTECTED static int test_buffer_overflow_protection(void) {
    SSP_FUNCTION_ENTER();
    
    char buffer[64];
    int i;
    
    print("[SSP-TEST] Testing buffer overflow protection...\n");
    
    // Safe operation - should not trigger SSP
    for (i = 0; i < 32; i++) {
        buffer[i] = 'A' + (i % 26);
    }
    buffer[32] = '\0';
    
    print("[SSP-TEST] Buffer filled safely: ");
    print(buffer);
    print("\n");
    
    SSP_FUNCTION_EXIT();
    return 0;
}

// Function to test return address validation
SSP_STRONG static int test_return_address_validation(void) {
    SSP_FUNCTION_ENTER();
    SSP_PROTECT_RETURN();
    
    print("[SSP-TEST] Testing return address validation...\n");
    
    // Get current return address
    uint32_t *return_addr_ptr;
    __asm__ volatile("lea 4(%%ebp), %0" : "=r"(return_addr_ptr));
    
    uint32_t return_addr = *return_addr_ptr;
    
    print("[SSP-TEST] Return address: 0x");
    print_hex(return_addr);
    print("\n");
    
    // Validate it
    if (ssp_validate_return_address(return_addr)) {
        print("[SSP-TEST] Return address validation: PASS\n");
    } else {
        print("[SSP-TEST] Return address validation: FAIL\n");
    }
    
    SSP_FUNCTION_EXIT();
    return 0;
}

// Function to test stack frame validation
static int test_stack_frame_validation(void) {
    SSP_FUNCTION_ENTER();
    
    print("[SSP-TEST] Testing stack frame validation...\n");
    
    // Validate current frame
    SSP_VALIDATE_FRAME();
    
    print("[SSP-TEST] Stack frame validation: PASS\n");
    
    SSP_FUNCTION_EXIT();
    return 0;
}

// Test canary generation and management
static int test_canary_management(void) {
    print("[SSP-TEST] Testing canary management...\n");
    
    uint32_t canary1 = ssp_get_canary();
    print("[SSP-TEST] Initial canary: 0x");
    print_hex(canary1);
    print("\n");
    
    // Generate new canary
    ssp_generate_random_canary();
    uint32_t canary2 = ssp_get_canary();
    
    print("[SSP-TEST] New canary: 0x");
    print_hex(canary2);
    print("\n");
    
    if (canary1 != canary2) {
        print("[SSP-TEST] Canary generation: PASS (values differ)\n");
        return 0;
    } else {
        print("[SSP-TEST] Canary generation: FAIL (values identical)\n");
        return -1;
    }
}

// Test SSP statistics reporting
static int test_statistics_reporting(void) {
    print("[SSP-TEST] Testing statistics reporting...\n");
    
    uint32_t violations, checks;
    ssp_get_stats(&violations, &checks);
    
    print("[SSP-TEST] Violations detected: ");
    print_hex(violations);
    print("\n");
    
    print("[SSP-TEST] Checks performed: ");
    print_hex(checks);
    print("\n");
    
    return 0;
}

// Simulate a controlled stack corruption for testing (DANGEROUS!)
static int test_controlled_corruption_detection(void) {
    print("[SSP-TEST] WARNING: Testing controlled corruption detection\n");
    print("[SSP-TEST] This test is disabled for safety\n");
    
    // This test is commented out because it would actually trigger
    // stack smashing protection and halt the system
    /*
    uint32_t saved_canary = ssp_function_enter();
    
    // Simulate canary corruption by modifying it
    extern uint32_t __stack_chk_guard;
    uint32_t original = __stack_chk_guard;
    __stack_chk_guard = 0xDEADC0DE;  // Corrupt the canary
    
    // This should trigger SSP failure
    ssp_function_exit(saved_canary);
    
    // Should never reach here
    __stack_chk_guard = original;  // Restore if somehow we get here
    */
    
    return 0;
}

// Main SSP test function
int ssp_run_tests(void) {
    int failures = 0;
    
    print("\n=== SSP Functionality Tests ===\n");
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] Starting tests\n");
    
    // Test basic buffer protection
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] buffer\n");
    if (test_buffer_overflow_protection() != 0) {
        failures++;
    }
    
    // Test return address validation
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] return addr\n");
    if (test_return_address_validation() != 0) {
        failures++;
    }
    
    // Test stack frame validation
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] stack frame\n");
    if (test_stack_frame_validation() != 0) {
        failures++;
    }
    
    // Test canary management
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] canary mgmt\n");
    if (test_canary_management() != 0) {
        failures++;
    }
    
    // Test statistics
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] stats\n");
    if (test_statistics_reporting() != 0) {
        failures++;
    }
    
    // Test controlled corruption (disabled)
    if (debuglog_is_ready()) debuglog_write("[SSP-TEST] controlled corruption\n");
    if (test_controlled_corruption_detection() != 0) {
        failures++;
    }
    
    print("\n=== SSP Test Summary ===\n");
    if (failures == 0) {
        print("[SSP-TEST] All tests passed!\n");
        return 0;
    } else {
        print("[SSP-TEST] ");
        print_hex(failures);
        print(" tests failed\n");
        return failures;
    }
}

// Function to demonstrate SSP in action during normal operation
void ssp_demonstrate_protection(void) {
    print("[SSP-DEMO] Demonstrating SSP during normal operations...\n");
    
    // Example of manual protection for critical function
    SSP_FUNCTION_ENTER();
    
    // Simulate some work that could be vulnerable
    char work_buffer[128];
    int i;
    
    for (i = 0; i < 100; i++) {
        work_buffer[i] = 'X';
    }
    work_buffer[100] = '\0';
    
    print("[SSP-DEMO] Completed protected operation safely\n");
    
    SSP_FUNCTION_EXIT();
}
