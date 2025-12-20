#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/time.h"
#include "../src/include/libc/unistd.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    int now = time(NULL);
    printf("date (epoch): %d\n", now);
    printf("No calendar formatting support is available yet.\n");
    return 0;
}
