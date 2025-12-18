#include "../../src/include/libc/netlib.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/string.h"
#include "../../src/include/libc/time.h"
#include "../../src/include/libc/stdlib.h"

static int forest_poll_for_reply(int fd, char* buffer, size_t buf_len, int attempts) {
    for (int i = 0; i < attempts; i++) {
        int addr_len = sizeof(sockaddr_in_t);
        sockaddr_in_t addr;
        int read = recvfrom(fd, buffer, buf_len, 0, &addr, &addr_len);
        if (read > 0) {
            return read;
        }
        struct timespec req = {0, 50 * 1000 * 1000};
        nanosleep(&req, 0);
    }
    return -1;
}

uint32 forest_parse_ipv4(const char* text, bool* ok) {
    if (ok) {
        *ok = false;
    }
    if (!text) {
        return 0;
    }

    uint32 octets[4] = {0};
    int index = 0;
    const char* cursor = text;
    while (*cursor && index < 4) {
        uint32 value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (uint32)(*cursor - '0');
            cursor++;
        }
        if (value > 255) {
            return 0;
        }
        octets[index++] = value;
        if (*cursor == '.') {
            cursor++;
        } else {
            break;
        }
    }
    if (index != 4) {
        return 0;
    }
    if (*cursor != 0) {
        return 0;
    }
    if (ok) {
        *ok = true;
    }
    return (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
}

void forest_format_ipv4(uint32 addr, char* buffer, size_t buffer_len) {
    if (!buffer || buffer_len < 16) {
        return;
    }
    uint32 parts[4];
    parts[0] = (addr >> 24) & 0xFF;
    parts[1] = (addr >> 16) & 0xFF;
    parts[2] = (addr >> 8) & 0xFF;
    parts[3] = addr & 0xFF;
    itoa(parts[0], buffer, 10);
    size_t len = strlen(buffer);
    buffer[len++] = '.';
    itoa(parts[1], buffer + len, 10);
    len = strlen(buffer);
    buffer[len++] = '.';
    itoa(parts[2], buffer + len, 10);
    len = strlen(buffer);
    buffer[len++] = '.';
    itoa(parts[3], buffer + len, 10);
}

int forest_dns_resolve(const char* hostname, uint32* out_addr) {
    if (!out_addr || !hostname) {
        return -1;
    }
    if (strcmp(hostname, "localhost") == 0 || strcmp(hostname, "loopback") == 0) {
        *out_addr = INADDR_LOOPBACK;
        return 0;
    }
    bool ok = false;
    uint32 parsed = forest_parse_ipv4(hostname, &ok);
    if (ok) {
        *out_addr = parsed;
        return 0;
    }
    return -1;
}

int forest_echo_exchange(const char* payload, size_t length, char* reply, size_t reply_len) {
    if (!payload || length == 0 || !reply || reply_len == 0) {
        return -1;
    }

    sockaddr_in_t addr;
    addr.sin_family = AF_INET;
    addr.sin_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(NET_PORT_ECHO);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    int result = sendto(fd, payload, length, 0, &addr, sizeof(addr));
    if (result < 0) {
        close(fd);
        return -1;
    }

    int received = forest_poll_for_reply(fd, reply, reply_len, 5);
    close(fd);
    return received;
}

int forest_port_query(uint16 port, const char* payload, size_t payload_len, char* buffer, size_t buffer_len) {
    if (!buffer || buffer_len == 0 || !payload || payload_len == 0) {
        return -1;
    }

    sockaddr_in_t addr;
    addr.sin_family = AF_INET;
    addr.sin_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    int sent = sendto(fd, payload, payload_len, 0, &addr, sizeof(addr));
    if (sent < 0) {
        close(fd);
        return -1;
    }

    int received = forest_poll_for_reply(fd, buffer, buffer_len, 5);
    close(fd);
    return received;
}

int forest_http_get(const char* path, char* buffer, size_t buffer_len) {
    if (!buffer || buffer_len == 0) {
        return -1;
    }
    const char* use_path = path ? path : "/";
    char request[128];
    strcpy(request, "GET ");
    strcat(request, use_path);
    strcat(request, " HTTP/1.0\r\nHost: loopback\r\n\r\n");

    sockaddr_in_t addr;
    addr.sin_family = AF_INET;
    addr.sin_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(NET_PORT_HTTP);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (sendto(fd, request, strlen(request), 0, &addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    int received = forest_poll_for_reply(fd, buffer, buffer_len, 5);
    close(fd);
    return received;
}

int forest_netinfo(net_socket_info_t* info, int max_entries) {
    return netinfo(info, max_entries);
}
