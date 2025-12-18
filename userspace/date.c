#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/time.h"

void _start(void) {
    int now = time(NULL);
    printf("date (epoch): %d\n", now);
    printf("No calendar formatting support is available yet.\n");
    exit(0);
}
