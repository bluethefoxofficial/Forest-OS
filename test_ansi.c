/*
 * ANSI Escape Sequence Test Program for Forest OS
 * Tests the enhanced TTY implementation with comprehensive ANSI support
 */

#include "src/include/tty.h"
#include "src/include/graphics/graphics_manager.h"
#include <stdio.h>

void test_basic_colors() {
    tty_write_ansi("\x1b[1;32m=== Testing Basic 16-Color Support ===\x1b[0m\n");
    
    // Test standard foreground colors
    tty_write_ansi("Standard colors: ");
    for (int i = 30; i <= 37; i++) {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1b[%dmC%d\x1b[0m ", i, i-30);
        tty_write_ansi(seq);
    }
    tty_write_ansi("\n");
    
    // Test bright foreground colors
    tty_write_ansi("Bright colors:   ");
    for (int i = 90; i <= 97; i++) {
        char seq[16];
        snprintf(seq, sizeof(seq), "\x1b[%dmC%d\x1b[0m ", i, i-90);
        tty_write_ansi(seq);
    }
    tty_write_ansi("\n\n");
}

void test_256_colors() {
    tty_write_ansi("\x1b[1;33m=== Testing 256-Color Support ===\x1b[0m\n");
    
    // Test 256-color foreground
    tty_write_ansi("256-color sample: ");
    for (int i = 196; i <= 201; i++) {
        char seq[32];
        snprintf(seq, sizeof(seq), "\x1b[38;5;%dmR%d\x1b[0m ", i, i);
        tty_write_ansi(seq);
    }
    tty_write_ansi("\n");
    
    // Test 256-color background
    tty_write_ansi("Background test:  ");
    for (int i = 196; i <= 201; i++) {
        char seq[32];
        snprintf(seq, sizeof(seq), "\x1b[48;5;%dm  \x1b[0m", i);
        tty_write_ansi(seq);
    }
    tty_write_ansi("\n\n");
}

void test_truecolor() {
    tty_write_ansi("\x1b[1;35m=== Testing 24-bit Truecolor Support ===\x1b[0m\n");
    
    // Test RGB gradient
    tty_write_ansi("RGB gradient: ");
    for (int i = 0; i < 8; i++) {
        int r = i * 32;
        int g = 255 - i * 32;
        int b = 128;
        char seq[64];
        snprintf(seq, sizeof(seq), "\x1b[38;2;%d;%d;%dmR\x1b[0m", r, g, b);
        tty_write_ansi(seq);
    }
    tty_write_ansi("\n\n");
}

void test_formatting() {
    tty_write_ansi("\x1b[1;36m=== Testing Text Formatting ===\x1b[0m\n");
    
    tty_write_ansi("\x1b[1mBold text\x1b[0m | ");
    tty_write_ansi("\x1b[2mFaint text\x1b[0m | ");
    tty_write_ansi("\x1b[3mItalic text\x1b[0m | ");
    tty_write_ansi("\x1b[4mUnderlined\x1b[0m | ");
    tty_write_ansi("\x1b[5mBlinking\x1b[0m\n");
    
    tty_write_ansi("\x1b[7mInverse video\x1b[0m | ");
    tty_write_ansi("\x1b[9mStrikethrough\x1b[0m | ");
    tty_write_ansi("\x1b[21mDouble underline\x1b[0m\n\n");
}

void test_cursor_movement() {
    tty_write_ansi("\x1b[1;31m=== Testing Cursor Movement ===\x1b[0m\n");
    
    tty_write_ansi("Moving cursor: ");
    tty_write_ansi("\x1b[s");          // Save cursor position
    tty_write_ansi("\x1b[10CRight");   // Move right 10
    tty_write_ansi("\x1b[u");          // Restore cursor position
    tty_write_ansi("Back to start\n");
    
    tty_write_ansi("Positioned text at (20,5): ");
    tty_write_ansi("\x1b[s\x1b[5;20HHERE!\x1b[u");
    tty_write_ansi("Back to flow\n\n");
}

void test_screen_clearing() {
    tty_write_ansi("\x1b[1;34m=== Testing Screen Operations ===\x1b[0m\n");
    
    tty_write_ansi("Line clearing test: ");
    tty_write_ansi("This will be partially erased");
    tty_write_ansi("\x1b[10D\x1b[K");  // Move left 10, clear to end of line
    tty_write_ansi("CLEARED!\n");
    
    tty_write_ansi("Scroll test: Multiple lines\n");
    tty_write_ansi("Line 2\n");
    tty_write_ansi("Line 3\n\n");
}

void test_complex_sequences() {
    tty_write_ansi("\x1b[1;95m=== Testing Complex ANSI Sequences ===\x1b[0m\n");
    
    // Complex formatting with multiple attributes
    tty_write_ansi("\x1b[1;4;32;48;2;64;0;128mBold Underlined Green on Purple\x1b[0m\n");
    
    // Table with borders using Unicode box drawing
    tty_write_ansi("\x1b[37m┌─────────┬─────────┬─────────┐\x1b[0m\n");
    tty_write_ansi("\x1b[37m│\x1b[1;31m  Red   \x1b[0;37m│\x1b[1;32m  Green \x1b[0;37m│\x1b[1;34m  Blue  \x1b[0;37m│\x1b[0m\n");
    tty_write_ansi("\x1b[37m├─────────┼─────────┼─────────┤\x1b[0m\n");
    tty_write_ansi("\x1b[37m│\x1b[38;2;255;0;0m  #FF0000\x1b[0;37m│\x1b[38;2;0;255;0m #00FF00\x1b[0;37m│\x1b[38;2;0;0;255m #0000FF\x1b[0;37m│\x1b[0m\n");
    tty_write_ansi("\x1b[37m└─────────┴─────────┴─────────┘\x1b[0m\n\n");
}

int main() {
    printf("Testing Forest OS Enhanced ANSI TTY Implementation\n");
    printf("==================================================\n\n");
    
    // Initialize graphics and TTY
    if (!graphics_init()) {
        printf("ERROR: Graphics initialization failed!\n");
        return 1;
    }
    
    if (!tty_init()) {
        printf("ERROR: TTY initialization failed!\n");
        return 1;
    }
    
    // Run tests
    test_basic_colors();
    test_256_colors();
    test_truecolor();
    test_formatting();
    test_cursor_movement();
    test_screen_clearing();
    test_complex_sequences();
    
    tty_write_ansi("\x1b[1;92m✅ All ANSI tests completed!\x1b[0m\n");
    tty_write_ansi("\x1b[48;2;0;64;0;38;2;255;255;255m Forest OS TTY supports comprehensive ANSI escape sequences! \x1b[0m\n");
    
    return 0;
}