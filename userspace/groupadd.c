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

    char name[AUTH_NAME_LEN];
    printf("New group name: ");
    if (!read_line(name, sizeof(name))) {
        return 1;
    }

    int res = userdb_group_add(name);
    if (res == 0) {
        printf("Group %s added.\n", name);
        return 0;
    }

    printf("Unable to add group (code %d)\n", res);
    return 1;
}
