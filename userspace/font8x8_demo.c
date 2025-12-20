#include "forest_toolbox.h"
#include "../src/include/graphics/font8x8.h"

void render_demo(void) {
    printf("=== Forest OS 8x8 Font Demo ===\n\n");
    
    // Display basic information
    printf("Font system supports %u Unicode blocks:\n", font8x8_get_block_count());
    
    for (uint32_t i = 0; i < font8x8_get_block_count(); i++) {
        const font8x8_block_t* block = font8x8_get_block_info(i);
        if (block) {
            printf("  %u. %s (U+%04X - U+%04X, %u chars)\n", 
                   i + 1, block->name, block->unicode_start, 
                   block->unicode_end, block->count);
        }
    }
    
    printf("\n=== Character Samples ===\n");
    
    // Basic Latin samples
    printf("Basic Latin: ");
    for (char c = 'A'; c <= 'Z'; c++) {
        printf("%c", c);
    }
    printf("\n");
    
    printf("Digits: ");
    for (char c = '0'; c <= '9'; c++) {
        printf("%c", c);
    }
    printf("\n");
    
    printf("Special chars: !@#$%%^&*()_+-=[]{}|;':\",./<>?\n");
    
    // Extended Latin samples
    printf("Extended Latin: ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÑÒÓÔÕÖØÙÚÛÜÝßàáâãäå\n");
    
    // Box drawing characters
    printf("\n=== Box Drawing Demo ===\n");
    printf("┌─────────┐\n");
    printf("│ Forest  │\n");
    printf("│   OS    │\n");
    printf("└─────────┘\n");
    
    // Block elements
    printf("\n=== Block Elements Demo ===\n");
    printf("Progress: ████████░░ 80%%\n");
    printf("Pattern:  ▓▒░▓▒░▓▒░\n");
    
    // Greek characters
    printf("\n=== Greek Characters ===\n");
    printf("Greek: ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ\n");
    printf("Lower: αβγδεζηθικλμνξοπρστυφχψω\n");
    
    // Test font lookup functions
    printf("\n=== Font System Tests ===\n");
    printf("Testing character support:\n");
    
    uint32_t test_chars[] = {'A', 'α', '█', '─', 'あ', 0x2665}; // A, alpha, block, line, hiragana a, heart
    for (int i = 0; i < 6; i++) {
        bool supported = font8x8_is_supported(test_chars[i]);
        printf("  U+%04X: %s\n", test_chars[i], supported ? "Supported" : "Not supported");
    }
    
    printf("\n=== ASCII Art Demo ===\n");
    printf("    ███████╗ ██████╗ ██████╗ ███████╗███████╗████████╗\n");
    printf("    ██╔════╝██╔═══██╗██╔══██╗██╔════╝██╔════╝╚══██╔══╝\n");
    printf("    █████╗  ██║   ██║██████╔╝█████╗  ███████╗   ██║   \n");
    printf("    ██╔══╝  ██║   ██║██╔══██╗██╔══╝  ╚════██║   ██║   \n");
    printf("    ██║     ╚██████╔╝██║  ██║███████╗███████║   ██║   \n");
    printf("    ╚═╝      ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝   ╚═╝   \n");
    printf("\n");
    printf("                ██████╗ ███████╗\n");
    printf("               ██╔═══██╗██╔════╝\n");
    printf("               ██║   ██║███████╗\n");
    printf("               ██║   ██║╚════██║\n");
    printf("               ╚██████╔╝███████║\n");
    printf("                ╚═════╝ ╚══════╝\n");
    
    printf("\nFont rendering system ready!\n");
    printf("Comprehensive 8x8 font support with multiple Unicode blocks.\n");
}

int main(void) {
    render_demo();
    return 0;
}
