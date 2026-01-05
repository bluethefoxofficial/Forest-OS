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

    auth_user_info_t info;
    if (userdb_current(&info) != 0) {
        memset(&info, 0, sizeof(info));
        strcpy(info.name, "root");
    }

    char password[64];
    printf("Changing password for %s\n", info.name);
    printf("new password: ");
    if (!read_line(password, sizeof(password))) {
        return 1;
    }

    if (userdb_change_password(info.name, password) == 0) {
        printf("Password updated.\n");
        return 0;
    }

    printf("Password change failed.\n");
    return 1;
}
