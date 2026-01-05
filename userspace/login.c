#include "../src/include/libc/stdio.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/auth.h"
#include "../src/include/libc/unistd.h"

static int read_line(char* buffer, size_t max_len) {
    ssize_t n = read(0, buffer, max_len - 1);
    if (n <= 0) {
        return 0;
    }
    buffer[n] = '\0';
    size_t len = strlen(buffer);
    if (len && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
    }
    return 1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char username[AUTH_NAME_LEN];
    char password[64];
    auth_user_info_t info;

    printf("login: ");
    if (!read_line(username, sizeof(username))) {
        return 1;
    }
    printf("password: ");
    if (!read_line(password, sizeof(password))) {
        return 1;
    }

    if (userdb_login(username, password, &info) == 0) {
        printf("Welcome %s (uid %u)\n", info.name, info.uid);
        return 0;
    }

    printf("Login failed.\n");
    return 1;
}
