#include "../src/include/libc/stdio.h"
#include "../src/include/libc/auth.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    userdb_logout();
    printf("Session marked for logout. Close the shell to return to login screen.\n");
    return 0;
}
