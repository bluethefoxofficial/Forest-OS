#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    char buffer[256];
    const char msg[] = "SSH probe";
    int received = forest_port_query(NET_PORT_SSH, msg, sizeof(msg) - 1, buffer, sizeof(buffer) - 1);
    if (received > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
    } else {
        printf("ssh: no loopback handshake available\n");
    }
    exit(0);
}
