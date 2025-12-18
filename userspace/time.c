#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/time.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    int now = time(NULL);
    printf("epoch seconds: %d\n", now);
    exit(0);
}
