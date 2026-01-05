#include "include/session.h"
#include "include/auth.h"
#include "include/kb.h"
#include "include/tty.h"
#include "include/shell_loader.h"
#include "include/dks.h"
#include "include/task.h"
#include "include/util.h"
#include "include/debuglog.h"
#include "include/string.h"
#include "include/timer.h"

#define SESSION_INPUT_MAX 64

static void print_banner(void) {
    tty_write_ansi("\x1b[32mForest OS login\x1b[0m\n");
    tty_write_ansi("Type 'signup' at the username prompt to create an account.\n");
}

static size_t read_hidden(char* buffer, size_t max_len) {
    size_t i = 0;
    while (1) {
        char ch = 0;
        if (!keyboard_poll_char(&ch)) {
            __asm__ __volatile__("hlt");
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            tty_write("\n");
            break;
        }
        if (ch == '\b') {
            if (i > 0) {
                i--;
                tty_write("\b \b");
            }
            continue;
        }
        if (i + 1 < max_len) {
            buffer[i++] = ch;
            buffer[i] = '\0';
            tty_write("*");
        }
    }
    buffer[i] = '\0';
    return i;
}

static bool handle_signup(void) {
    tty_write("New username: ");
    string name = readStr();
    if (!name) {
        return false;
    }
    tty_write("New password: ");
    char pass[SESSION_INPUT_MAX];
    read_hidden(pass, sizeof(pass));

    auth_result_t res = auth_signup(name, pass, "users", false);
    free(name);

    if (res == AUTH_OK) {
        tty_write("Account created.\n");
        return true;
    }
    tty_write("Signup failed. Root login may be required.\n");
    return false;
}

static bool prompt_login(auth_user_info_t* out_user) {
    while (1) {
        tty_write("Username: ");
        string user = readStr();
        if (!user) {
            continue;
        }
        if (strcmp(user, "signup") == 0) {
            handle_signup();
            free(user);
            continue;
        }

        tty_write("Password: ");
        char pass[SESSION_INPUT_MAX];
        read_hidden(pass, sizeof(pass));

        auth_result_t res = auth_login(user, pass, out_user);
        free(user);

        if (res == AUTH_OK) {
            tty_write("Login successful.\n\n");
            return true;
        }
        tty_write("Invalid credentials. Try again.\n");
    }
}

static void wait_for_shell(uint32 pid) {
    const uint32 startup_timeout_ticks = 300; // ~3s at 100Hz
    uint32 launch_tick = timer_get_ticks();
    bool saw_activity = false;

    while (pid != 0 && task_exists(pid)) {
        uint32 last_tick = task_get_last_active_tick(pid);
        if (last_tick != 0) {
            saw_activity = true;
        }

        uint32 now = timer_get_ticks();
        if (!saw_activity && (now - launch_tick) > startup_timeout_ticks) {
            tty_write("Shell appears unresponsive. Terminating and returning to login.\n");
            task_kill(pid);
            saw_activity = true; // Avoid repeated kill attempts while it is being reaped
        }

        task_schedule();
        __asm__ __volatile__("hlt");
    }
}

void session_run(bool autologin_root) {
    auth_init();
    // Prefer the PS/2 driver for proper scancode translation; caller can flip
    // back to legacy via keyboard_set_driver_mode if needed.
    keyboard_set_driver_mode(KEYBOARD_DRIVER_PS2);
    // Ensure the framebuffer TTY is active and clean before prompting.
    tty_try_enable_graphics_backend();
    tty_clear();
    tty_write_ansi("\x1b[0m");
    // Drop any stale scancodes from boot so the first prompt is clean.
    while (1) {
        char junk;
        if (!keyboard_poll_char(&junk)) {
            break;
        }
    }
    while (1) {
        auth_user_info_t user_info;
        bool logged_in = false;

        if (autologin_root && auth_force_login("root") == AUTH_OK) {
            auth_find_user("root", &user_info);
            logged_in = true;
            tty_write_ansi("\x1b[33mAuto-login enabled for root\x1b[0m\n");
        }

        if (!logged_in) {
            print_banner();
            logged_in = prompt_login(&user_info);
        }

        if (!logged_in) {
            continue;
        }

        debuglog(DEBUG_INFO, "[SESSION] Launching shell for user '%s'\n", user_info.name);
        bool shell_ok = shell_launch_embedded();
        uint32 shell_pid = shell_get_last_pid();
        task_t* shell_task = shell_get_last_task();
        if (!shell_ok || shell_pid == 0) {
            tty_write("Shell failed to start. Falling back to kernel shell.\n");
            dks_run();
            auth_logout();
            continue;
        }

        if (shell_task) {
            shell_task->state = TASK_STATE_READY;
            task_switch(shell_task);
        }

        wait_for_shell(shell_pid);
        auth_logout();
        tty_write("Session ended. Returning to login prompt.\n\n");
    }
}
