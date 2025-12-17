#include "../src/include/libc/stdio.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"

typedef struct {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
} utsname_t;

void _start(void) {
    utsname_t info;
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
