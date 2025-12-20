#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    struct utsname info;
    if (uname(&info) == 0) {
        printf("%s\n", info.nodename);
    } else {
        printf("hostname: uname syscall not available\n");
    }
    return 0;
}
