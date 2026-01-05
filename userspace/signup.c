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

    printf("New username: ");
    if (!read_line(username, sizeof(username))) {
        return 1;
    }
    printf("New password: ");
    if (!read_line(password, sizeof(password))) {
        return 1;
    }

    int res = userdb_signup(username, password, "users");
    if (res == 0) {
        printf("Created user %s\n", username);
        return 0;
    }

    printf("Signup failed (code %d)\n", res);
    return 1;
}
