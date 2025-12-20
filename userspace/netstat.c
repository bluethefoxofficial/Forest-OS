#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

int main(int argc, char **argv) {
    (void)argc;    (void)argv;
    net_socket_info_t sockets[16];
    int count = forest_netinfo(sockets, 16);
    if (count < 0) {
        count = 0;
    }
    printf("Proto Recv-Q Send-Q Local Address           Peer Address\n");
    for (int i = 0; i < count; i++) {
        printf("udp   %5u %5u 0.0.0.0:%u        0.0.0.0:%u\n",
               sockets[i].queue_depth, sockets[i].queue_capacity - sockets[i].queue_depth,
               sockets[i].port, sockets[i].last_peer_port);
    }
    return 0;
}
