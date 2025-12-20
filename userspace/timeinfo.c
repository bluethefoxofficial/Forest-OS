#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/time.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    int now = time(NULL);
    printf("Forest OS fake time: %d\n", now);

    struct timespec req = { .tv_sec = 0, .tv_nsec = 500000000 };
    printf("Sleeping for ~500ms...\n");
    nanosleep(&req, NULL);
    printf("Awake again!\n");
    return 0;
}
