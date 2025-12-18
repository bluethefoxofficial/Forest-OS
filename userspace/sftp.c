#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

void _start(void) {
    char buffer[256];
    const char probe[] = "SFTP connect";
    int received = forest_port_query(NET_PORT_SFTP, probe, sizeof(probe) - 1, buffer, sizeof(buffer) - 1);
    if (received > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
    } else {
        printf("sftp: loopback endpoint not responding\n");
    }
    exit(0);
}
