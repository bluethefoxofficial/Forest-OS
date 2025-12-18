#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    struct utsname info;
    if (uname(&info) == 0) {
        printf("%s %s %s %s %s\n", info.sysname, info.nodename, info.release, info.version, info.machine);
    } else {
        printf("uname: syscall not available\n");
    }
    exit(0);
}
