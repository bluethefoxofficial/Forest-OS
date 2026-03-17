#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include "types.h"

#define AUTH_MAX_USERS 16
#define AUTH_MAX_GROUPS 8
#define AUTH_MAX_GROUPS_PER_USER 4
#define AUTH_NAME_LEN 16
#define AUTH_SALT_LEN 16
#define AUTH_HASH_HEX_LEN 65

#define AUTH_FLAG_ROOT 0x1u

typedef struct {
    char name[AUTH_NAME_LEN];
    uint32 gid;
} auth_group_info_t;

typedef struct {
    char name[AUTH_NAME_LEN];
    uint32 uid;
    uint32 gid;
    uint32 groups_mask;
    uint32 flags;
} auth_user_info_t;

typedef enum {
    AUTH_OK = 0,
    AUTH_ERR_FULL = -1,
    AUTH_ERR_EXISTS = -2,
    AUTH_ERR_BAD_CREDENTIALS = -3,
    AUTH_ERR_PERM = -4,
    AUTH_ERR_NOT_FOUND = -5,
    AUTH_ERR_INVALID = -6
} auth_result_t;

#ifdef USERSPACE_BUILD
int userdb_login(const char* username, const char* password, auth_user_info_t* out_info);
int userdb_signup(const char* username, const char* password, const char* primary_group);
int userdb_change_password(const char* username, const char* password);
int userdb_group_add(const char* name);
int userdb_current(auth_user_info_t* out_info);
int userdb_list(auth_user_info_t* buffer, int max_entries);
int userdb_logout(void);
#else
void auth_init(void);
auth_result_t auth_login(const char* username, const char* password, auth_user_info_t* out_info);
auth_result_t auth_signup(const char* username, const char* password, const char* primary_group, bool elevate_to_root);
auth_result_t auth_change_password(const char* username, const char* password);
auth_result_t auth_add_group(const char* name, uint32* out_gid);
auth_result_t auth_list(auth_user_info_t* buffer, uint32 max_entries, uint32* out_count);
auth_result_t auth_get_current(auth_user_info_t* out_info);
auth_result_t auth_logout(void);
bool auth_user_is_admin(const auth_user_info_t* info);
auth_result_t auth_find_user(const char* username, auth_user_info_t* out_info);
auth_result_t auth_force_login(const char* username);
uint32 auth_active_uid(void);
uint32 auth_active_gid(void);
uint32 auth_active_groups_mask(void);
uint32 auth_get_group_gid(const char* name);
#endif

// Shared operation codes for the user management syscall
#define USER_OP_LOGIN   1
#define USER_OP_SIGNUP  2
#define USER_OP_PASSWD  3
#define USER_OP_GROUP   4
#define USER_OP_CURRENT 5
#define USER_OP_LIST    6
#define USER_OP_LOGOUT  7

#endif // AUTH_H
