#include "include/net.h"
#include "include/util.h"
#include "include/string.h"
#include "include/libc/stdlib.h"
#include "include/screen.h"

#define NET_MAX_SOCKETS 16
#define NET_SOCKET_FD_BASE 64
#define NET_SOCKET_QUEUE 8
#define NET_MAX_PAYLOAD 1024
#define NET_EPHEMERAL_BASE 40000

typedef struct {
    uint32 src_addr;
    uint16 src_port;
    uint32 dest_addr;
    uint16 dest_port;
    uint32 length;
    uint8 data[NET_MAX_PAYLOAD];
} net_datagram_t;

typedef struct {
    bool used;
    bool bound;
    uint16 port;
    net_datagram_t queue[NET_SOCKET_QUEUE];
    uint8 head;
    uint8 tail;
    uint8 count;
    uint32 bytes_sent;
    uint32 bytes_received;
    uint32 last_peer_addr;
    uint16 last_peer_port;
    bool   reuseaddr;
} net_socket_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];
static bool g_net_ready = false;
static uint16 g_next_ephemeral_port = NET_EPHEMERAL_BASE;
static driver_t g_loopback_driver;

static uint16 net_allocate_ephemeral_port(void) {
    uint16 port = g_next_ephemeral_port++;
    if (g_next_ephemeral_port == 0) {
        g_next_ephemeral_port = NET_EPHEMERAL_BASE;
    }
    return port;
}

static net_socket_t* net_socket_from_fd(uint32 fd) {
    if (fd < NET_SOCKET_FD_BASE) {
        return 0;
    }
    uint32 index = fd - NET_SOCKET_FD_BASE;
    if (index >= NET_MAX_SOCKETS) {
        return 0;
    }
    return g_sockets[index].used ? &g_sockets[index] : 0;
}

static net_socket_t* net_socket_by_port(uint16 port) {
    if (!port) {
        return 0;
    }
    for (uint32 i = 0; i < NET_MAX_SOCKETS; i++) {
        if (g_sockets[i].used && g_sockets[i].bound && g_sockets[i].port == port) {
            return &g_sockets[i];
        }
    }
    return 0;
}

static bool net_socket_queue_push(net_socket_t* sock, const net_datagram_t* msg) {
    if (!sock || sock->count >= NET_SOCKET_QUEUE) {
        return false;
    }
    sock->queue[sock->tail] = *msg;
    sock->tail = (sock->tail + 1) % NET_SOCKET_QUEUE;
    sock->count++;
    return true;
}

static bool net_socket_queue_pop(net_socket_t* sock, net_datagram_t* out) {
    if (!sock || sock->count == 0) {
        return false;
    }
    *out = sock->queue[sock->head];
    sock->head = (sock->head + 1) % NET_SOCKET_QUEUE;
    sock->count--;
    return true;
}

static bool loopback_driver_init(driver_t* driver) {
    (void)driver;
    print("[NET] Loopback driver online\n");
    return true;
}

bool net_init(void) {
    if (g_net_ready) {
        return true;
    }

    memory_set((uint8*)g_sockets, 0, sizeof(g_sockets));

    g_loopback_driver.name = "loopback-net";
    g_loopback_driver.driver_class = DRIVER_CLASS_NETWORK;
    g_loopback_driver.init = loopback_driver_init;
    g_loopback_driver.shutdown = 0;
    g_loopback_driver.context = 0;
    g_loopback_driver.id = 0;
    g_loopback_driver.initialized = false;

    if (!driver_register(&g_loopback_driver)) {
        print("[NET] Failed to register loopback driver\n");
        return false;
    }

    g_net_ready = true;
    return true;
}

bool net_is_fd(uint32 fd) {
    return net_socket_from_fd(fd) != 0;
}

int32 net_close(uint32 fd) {
    net_socket_t* sock = net_socket_from_fd(fd);
    if (!sock) {
        return -1;
    }
    memory_set((uint8*)sock, 0, sizeof(net_socket_t));
    return 0;
}

int32 net_socket_create(uint32 domain, uint32 type, uint32 protocol) {
    if (domain != AF_INET || type != SOCK_DGRAM) {
        return -1;
    }
    (void)protocol;

    for (uint32 i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used) {
            memory_set((uint8*)&g_sockets[i], 0, sizeof(net_socket_t));
            g_sockets[i].used = true;
            g_sockets[i].bound = false;
            g_sockets[i].port = 0;
            return (int32)(NET_SOCKET_FD_BASE + i);
        }
    }
    return -1;
}

int32 net_bind(uint32 fd, uint16 port) {
    net_socket_t* sock = net_socket_from_fd(fd);
    if (!sock) {
        return -1;
    }
    if (sock->bound) {
        return -1;
    }
    if (port == 0) {
        port = net_allocate_ephemeral_port();
    }
    if (net_socket_by_port(port)) {
        return -1;
    }
    sock->port = port;
    sock->bound = true;
    return 0;
}

static void net_emit_rx_event(uint16 port, uint32 length) {
    struct {
        uint16 port;
        uint32 length;
    } payload;
    payload.port = port;
    payload.length = length;
    driver_emit_event(g_loopback_driver.id, DRIVER_CLASS_NETWORK,
                      DRIVER_EVENT_NETWORK_RX_READY, &payload, sizeof(payload));
}

static bool net_send_virtual_response(const net_datagram_t* request,
                                      const uint8* payload, uint32 length,
                                      uint16 response_port) {
    net_socket_t* reply = net_socket_by_port(request->src_port);
    if (!reply || !payload || length == 0 || length > NET_MAX_PAYLOAD) {
        return false;
    }

    net_datagram_t response;
    memory_set((uint8*)&response, 0, sizeof(response));
    response.src_addr = request->dest_addr;
    response.src_port = response_port;
    response.dest_addr = request->src_addr;
    response.dest_port = request->src_port;
    response.length = length;
    memory_copy((char*)payload, (char*)response.data, length);

    if (!net_socket_queue_push(reply, &response)) {
        return false;
    }
    reply->last_peer_addr = request->dest_addr;
    reply->last_peer_port = response_port;
    reply->bytes_received += length;
    net_emit_rx_event(reply->port, response.length);
    return true;
}

static bool net_try_virtual_service(const net_datagram_t* msg) {
    if (!msg) {
        return false;
    }

    switch (msg->dest_port) {
        case NET_PORT_ECHO:
            return net_send_virtual_response(msg, msg->data, msg->length, NET_PORT_ECHO);
        case NET_PORT_HTTP: {
            const char body[] = "Forest loopback HTTP endpoint.\n"
                                "Available services: echo, ssh, ftp, rsync.\n";
            char buffer[NET_MAX_PAYLOAD];
            memory_set((uint8*)buffer, 0, sizeof(buffer));
            const char header[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";
            uint32 header_len = (uint32)strlen(header);
            memory_copy((char*)header, buffer, header_len);
            char length_field[16];
            uint32 body_len = (uint32)strlen(body);
            itoa((int)body_len, length_field, 10);
            uint32 len_len = (uint32)strlen(length_field);
            memory_copy(length_field, buffer + header_len, len_len);
            const char end_headers[] = "\r\n\r\n";
            memory_copy(end_headers, buffer + header_len + len_len, 4);
            memory_copy((char*)body, buffer + header_len + len_len + 4, body_len);
            uint32 total_len = header_len + len_len + 4 + body_len;
            return net_send_virtual_response(msg, (uint8*)buffer, total_len, NET_PORT_HTTP);
        }
        case NET_PORT_FTP: {
            const char banner[] = "220 Forest loopback FTP ready. Try wget/curl for HTTP.\n";
            return net_send_virtual_response(msg, (const uint8*)banner,
                                             (uint32)strlen(banner), NET_PORT_FTP);
        }
        case NET_PORT_SSH: {
            const char banner[] = "SSH-0.1-ForestOS loopback\n";
            return net_send_virtual_response(msg, (const uint8*)banner,
                                             (uint32)strlen(banner), NET_PORT_SSH);
        }
        case NET_PORT_RSYNCD: {
            const char banner[] = "@RSYNCD: 0.1 Forest loopback ready\n";
            return net_send_virtual_response(msg, (const uint8*)banner,
                                             (uint32)strlen(banner), NET_PORT_RSYNCD);
        }
        case NET_PORT_SFTP: {
            const char banner[] = "115 Forest SFTP loopback greeting\n";
            return net_send_virtual_response(msg, (const uint8*)banner,
                                             (uint32)strlen(banner), NET_PORT_SFTP);
        }
        default:
            return false;
    }
}

static int32 net_deliver_local(const net_datagram_t* msg) {
    net_socket_t* dest = net_socket_by_port(msg->dest_port);
    if (!dest) {
        return net_try_virtual_service(msg) ? (int32)msg->length : (int32)msg->length;
    }
    if (!net_socket_queue_push(dest, msg)) {
        return -1;
    }
    dest->last_peer_addr = msg->src_addr;
    dest->last_peer_port = msg->src_port;
    dest->bytes_received += msg->length;
    net_emit_rx_event(dest->port, msg->length);
    return (int32)msg->length;
}

int32 net_send_datagram(uint32 fd, const uint8* buffer, uint32 length,
                        uint32 dest_addr, uint16 dest_port) {
    net_socket_t* sock = net_socket_from_fd(fd);
    if (!sock || !buffer || length == 0 || length > NET_MAX_PAYLOAD) {
        return -1;
    }
    if (!sock->bound) {
        // Automatically bind if not already bound
        if (net_bind(fd, 0) != 0) {
            return -1;
        }
    }

    net_datagram_t msg;
    memory_set((uint8*)&msg, 0, sizeof(msg));
    msg.src_addr = INADDR_LOOPBACK;
    msg.src_port = sock->port;
    msg.dest_addr = dest_addr ? dest_addr : INADDR_LOOPBACK;
    msg.dest_port = dest_port;
    msg.length = length;
    memory_copy((char*)buffer, (char*)msg.data, length);

    sock->bytes_sent += length;
    sock->last_peer_addr = dest_addr;
    sock->last_peer_port = dest_port;

    return net_deliver_local(&msg);
}

int32 net_recv_datagram(uint32 fd, uint8* buffer, uint32 length,
                        uint32* out_addr, uint16* out_port) {
    net_socket_t* sock = net_socket_from_fd(fd);
    if (!sock || !buffer || length == 0) {
        return -1;
    }
    net_datagram_t msg;
    if (!net_socket_queue_pop(sock, &msg)) {
        return -1;
    }

    uint32 to_copy = (msg.length < length) ? msg.length : length;
    memory_copy((char*)msg.data, (char*)buffer, to_copy);
    if (out_addr) {
        *out_addr = msg.src_addr;
    }
    if (out_port) {
        *out_port = msg.src_port;
    }
    sock->bytes_received += to_copy;
    return (int32)to_copy;
}

uint32 net_snapshot(net_socket_info_t* out, uint32 max_entries) {
    if (!out || max_entries == 0) {
        return 0;
    }

    uint32 count = 0;
    for (uint32 i = 0; i < NET_MAX_SOCKETS && count < max_entries; i++) {
        if (!g_sockets[i].used) {
            continue;
        }
        net_socket_info_t* dst = &out[count++];
        dst->used = g_sockets[i].used;
        dst->bound = g_sockets[i].bound;
        dst->port = g_sockets[i].port;
        dst->queue_depth = g_sockets[i].count;
        dst->queue_capacity = NET_SOCKET_QUEUE;
        dst->bytes_sent = g_sockets[i].bytes_sent;
        dst->bytes_received = g_sockets[i].bytes_received;
        dst->last_peer_addr = g_sockets[i].last_peer_addr;
        dst->last_peer_port = g_sockets[i].last_peer_port;
    }
    return count;
}
