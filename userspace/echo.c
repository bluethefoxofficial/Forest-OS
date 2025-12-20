#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

int main(int argc, char **argv) {
    int newline = 1;
    int start_arg = 1;
    
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'n' && argv[1][2] == '\0') {
        newline = 0;
        start_arg = 2;
    }
    
    for (int i = start_arg; i < argc; i++) {
        if (i > start_arg) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    
    if (newline) {
        printf("\n");
    }
    
    return 0;
}
