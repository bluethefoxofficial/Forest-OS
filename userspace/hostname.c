#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    struct utsname info;
    if (uname(&info) == 0) {
        printf("%s\n", info.nodename);
    } else {
        printf("hostname: uname syscall not available\n");
    }
    exit(0);
}
