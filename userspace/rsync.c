#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    char buffer[256];
    const char payload[] = "RSYNC inventory";
    int received = forest_port_query(NET_PORT_RSYNCD, payload, sizeof(payload) - 1, buffer, sizeof(buffer) - 1);
    if (received > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
    } else {
        printf("rsync: loopback daemon not responding\n");
    }
    return 0;
}
