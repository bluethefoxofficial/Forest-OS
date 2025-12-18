#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

void _start(void) {
    char buffer[512];
    int received = forest_http_get("/", buffer, sizeof(buffer) - 1);
    if (received > 0) {
        buffer[received] = '\0';
        printf("--2024-- loopback fetch -- saved to stdout --\n");
        printf("%s\n", buffer);
    } else {
        printf("wget: unable to contact loopback HTTP service\n");
    }
    exit(0);
}
