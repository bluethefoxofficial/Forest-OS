#include <stdbool.h>
#include <stddef.h>
#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/string.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/auth.h"

#define MAX_INPUT 128
#define MOTD_PATH "/usr/share/motd"
#define LOCALE_PATH "/usr/share/locales/default.locale"

static bool starts_with(const char* text, const char* prefix) {
    size_t idx = 0;
    while (prefix[idx]) {
        if (text[idx] != prefix[idx]) {
            return false;
        }
        idx++;
    }
    return true;
}

static void handle_help(void) {
    printf("Forest shell built-ins:\n");
    printf("  help        - show this help\n");
    printf("  uname       - print kernel identity\n");
    printf("  time        - show fake system time\n");
    printf("  catreadme   - dump /README.txt\n");
    printf("  whoami      - show active user\n");
    printf("  login       - switch user\n");
    printf("  signup      - create a new user (root only)\n");
    printf("  passwd      - change your password\n");
    printf("  logout      - exit back to login screen\n");
    printf("  echo <text> - display text\n");
    printf("  shutdown    - request ACPI poweroff\n");
    printf("  reboot      - request ACPI reboot\n");
    printf("  exit        - return to kernel\n");
}

static void show_motd(void) {
    FILE* fp = fopen(MOTD_PATH, "r");
    if (!fp) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        printf("%s", line);
    }
    fclose(fp);
    printf("\n");
}

static void show_locale(void) {
    FILE* fp = fopen(LOCALE_PATH, "r");
    if (!fp) {
        return;
    }
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "LANG=", 5) == 0) {
            char* lang = line + 5;
            size_t len = strlen(lang);
            while (len > 0 && (lang[len - 1] == '\n' || lang[len - 1] == '\r')) {
                lang[len - 1] = '\0';
                len--;
            }
            printf("Locale: %s\n", lang);
            break;
        }
    }
    fclose(fp);
}

static void handle_uname(void) {
    struct utsname info;
    if (uname(&info) == 0) {
        printf("%s %s (%s) %s\n", info.sysname, info.release, info.version, info.machine);
    } else {
        printf("uname: syscall unavailable\n");
    }
}

static void handle_time(void) {
    int now = time(NULL);
    printf("Kernel reports epoch: %d\n", now);
}

static void handle_catreadme(void) {
    int fd = open("/README.txt", 0);
    if (fd < 0) {
        printf("catreadme: unable to open /README.txt\n");
        return;
    }
    char buffer[256];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
        write(1, buffer, (size_t)n);
    }
    close(fd);
    printf("\n");
}

static void handle_echo(const char* text) {
    if (!text || !*text) {
        printf("echo: missing text\n");
        return;
    }
    printf("%s\n", text);
}

static int read_line(char* buffer, size_t max_len) {
    ssize_t read_bytes = read(0, buffer, max_len - 1);
    if (read_bytes <= 0) {
        return 0;
    }
    buffer[read_bytes] = '\0';
    // remove trailing newline
    size_t len = strlen(buffer);
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
    }
    return 1;
}

static void refresh_user(auth_user_info_t* info) {
    if (userdb_current(info) != 0) {
        memset(info, 0, sizeof(*info));
        strcpy(info->name, "root");
    }
}

static void handle_whoami(const auth_user_info_t* info) {
    printf("%s (uid %u gid %u)\n",
           info->name[0] ? info->name : "unknown",
           info->uid,
           info->gid);
}

static void handle_login_command(auth_user_info_t* current_user) {
    char username[AUTH_NAME_LEN];
    char password[64];
    printf("login as: ");
    if (!read_line(username, sizeof(username))) {
        return;
    }
    printf("password: ");
    if (!read_line(password, sizeof(password))) {
        return;
    }
    if (userdb_login(username, password, current_user) == 0) {
        printf("Logged in as %s\n", current_user->name);
    } else {
        printf("Login failed.\n");
    }
}

static void handle_signup_command(void) {
    char username[AUTH_NAME_LEN];
    char password[64];
    printf("new username: ");
    if (!read_line(username, sizeof(username))) {
        return;
    }
    printf("new password: ");
    if (!read_line(password, sizeof(password))) {
        return;
    }
    if (userdb_signup(username, password, "users") == 0) {
        printf("User %s created.\n", username);
    } else {
        printf("Signup failed (root login may be required).\n");
    }
}

static void handle_passwd_command(const auth_user_info_t* current_user) {
    char password[64];
    const char* target = current_user->name[0] ? current_user->name : "root";
    printf("new password for %s: ", target);
    if (!read_line(password, sizeof(password))) {
        return;
    }
    if (userdb_change_password(target, password) == 0) {
        printf("Password updated.\n");
    } else {
        printf("Password change failed.\n");
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    auth_user_info_t current_user;
    refresh_user(&current_user);
    show_motd();
    show_locale();
    printf("Forest Shell (userspace) - logged in as %s\n",
           current_user.name[0] ? current_user.name : "root");
    printf("Type 'help' for a list of commands.\n");
    char line[MAX_INPUT];

    while (1) {
        const char* prompt_user = current_user.name[0] ? current_user.name : "user";
        printf("%s@forest> ", prompt_user);
        if (!read_line(line, sizeof(line))) {
            continue;
        }

        if (strcmp(line, "help") == 0) {
            handle_help();
        } else if (strcmp(line, "uname") == 0) {
            handle_uname();
        } else if (strcmp(line, "time") == 0) {
            handle_time();
        } else if (strcmp(line, "catreadme") == 0) {
            handle_catreadme();
        } else if (strcmp(line, "whoami") == 0) {
            handle_whoami(&current_user);
        } else if (strcmp(line, "login") == 0) {
            handle_login_command(&current_user);
        } else if (strcmp(line, "signup") == 0) {
            handle_signup_command();
            refresh_user(&current_user);
        } else if (strcmp(line, "passwd") == 0) {
            handle_passwd_command(&current_user);
        } else if (strcmp(line, "logout") == 0) {
            userdb_logout();
            printf("logout\n");
            return 0;
        } else if (strcmp(line, "shutdown") == 0) {
            printf("Requesting shutdown...\n");
            if (poweroff() != 0) {
                printf("Shutdown syscall failed.\n");
            }
        } else if (strcmp(line, "reboot") == 0) {
            printf("Requesting reboot...\n");
            if (reboot(0) != 0) {
                printf("Reboot syscall failed.\n");
            }
        } else if (strcmp(line, "exit") == 0) {
            userdb_logout();
            printf("logout\n");
            return 0;
        } else if (starts_with(line, "echo ")) {
            handle_echo(line + 5);
        } else if (line[0] == '\0') {
            continue;
        } else {
            printf("Unknown command: %s\n", line);
        }
    }
}
