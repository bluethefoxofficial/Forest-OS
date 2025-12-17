#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"

void _start(void) {
    struct utsname info;
    if (uname(&info) == 0) {
        printf("sysname : %s\n", info.sysname);
        printf("nodename: %s\n", info.nodename);
        printf("release : %s\n", info.release);
        printf("version : %s\n", info.version);
        printf("machine : %s\n", info.machine);
    } else {
        printf("uname syscall not available\n");
    }
    exit(0);
}
