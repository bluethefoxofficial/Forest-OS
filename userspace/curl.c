#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    char buffer[512];
    int received = forest_http_get("/", buffer, sizeof(buffer) - 1);
    if (received > 0) {
        buffer[received] = '\0';
        printf("%s\n", buffer);
    } else {
        printf("curl: failed to fetch loopback HTTP\n");
    }
    exit(0);
}
