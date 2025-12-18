#include "../src/include/libc/stdio.h"
#include "../src/include/libc/netlib.h"
#include "../src/include/libc/unistd.h"

void _start(void) {
    net_socket_info_t sockets[16];
    int count = forest_netinfo(sockets, 16);
    if (count < 0) {
        count = 0;
    }
    printf("State      Recv-Q Send-Q Local\n");
    for (int i = 0; i < count; i++) {
        printf("UNCONN     %u      %u      *:%u last:%u\n",
               sockets[i].queue_depth, sockets[i].queue_capacity - sockets[i].queue_depth,
               sockets[i].port, sockets[i].last_peer_port);
    }
    exit(0);
}
