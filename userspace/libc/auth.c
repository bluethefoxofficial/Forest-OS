#include "../../src/include/libc/auth.h"
#include "../../src/include/libc/unistd.h"
#include "../../src/include/libc/string.h"

int userdb_login(const char* username, const char* password, auth_user_info_t* out_info) {
    return user_syscall(USER_OP_LOGIN, username, password, 0, out_info, 0);
}

int userdb_signup(const char* username, const char* password, const char* primary_group) {
    return user_syscall(USER_OP_SIGNUP, username, password, primary_group, 0, 0);
}

int userdb_change_password(const char* username, const char* password) {
    return user_syscall(USER_OP_PASSWD, username, password, 0, 0, 0);
}

int userdb_group_add(const char* name) {
    return user_syscall(USER_OP_GROUP, 0, 0, name, 0, 0);
}

int userdb_current(auth_user_info_t* out_info) {
    return user_syscall(USER_OP_CURRENT, 0, 0, 0, out_info, 0);
}

int userdb_list(auth_user_info_t* buffer, int max_entries) {
    return user_syscall(USER_OP_LIST, 0, 0, 0, buffer, max_entries);
}

int userdb_logout(void) {
    return user_syscall(USER_OP_LOGOUT, 0, 0, 0, 0, 0);
}
