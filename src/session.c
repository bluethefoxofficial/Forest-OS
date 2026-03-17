#include "include/session.h"
#include "include/auth.h"
#include "include/kb.h"
#include "include/tty.h"
#include "include/shell_loader.h"
#include "include/task.h"
#include "include/util.h"
#include "include/debuglog.h"
#include "include/string.h"
#include "include/timer.h"
#include "include/vfs.h"
#include "include/libc/stdio.h"
#include "include/hotkey.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/window_manager.h"
#include "include/graphics/app_graphics.h"
#include "include/interrupt.h"

#define SESSION_INPUT_MAX 64
#define SESSION_DE_PATH_MAX 256
#define GRAPHICS_TASK_STARTUP_TIMEOUT_TICKS 3000  // ~30s at 100Hz
#define GRAPHICS_TASK_ASSUME_READY_TICKS 20       // ~200ms alive => startup ok

typedef enum {
    GRAPHICS_TASK_STARTUP_OK = 0,
    GRAPHICS_TASK_STARTUP_TIMEOUT,
    GRAPHICS_TASK_STARTUP_EXITED_EARLY
} graphics_task_startup_result_t;

// ============================================================================
// Multi-TTY Session Management
// ============================================================================

// Global array of TTY sessions
static tty_session_t g_tty_sessions[MAX_TTY_SESSIONS];
static bool g_sessions_initialized = false;

// External reference to current TTY session number (from hotkey.c)
extern uint32_t g_current_tty_session;

// Track if we need to switch sessions (set by hotkey handler)
static volatile bool g_session_switch_pending = false;
static volatile uint32_t g_session_switch_target = 0;

static inline void session_idle_wait(void) {
    task_schedule();
    if (irq_are_enabled()) {
        __asm__ __volatile__("hlt");
    } else {
        __asm__ __volatile__("pause");
    }
}

static bool is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool has_elf_suffix(const char* s) {
    size_t len = strlen(s);
    return (len >= 4) && strcmp(s + len - 4, ".elf") == 0;
}

static bool resolve_desktop_path(const char* raw_value, char* out_path, size_t out_size) {
    if (!raw_value || !out_path || out_size == 0) {
        return false;
    }

    while (*raw_value && is_space_char(*raw_value)) {
        raw_value++;
    }
    if (*raw_value == '\0') {
        return false;
    }

    size_t raw_len = strlen(raw_value);
    while (raw_len > 0 && is_space_char(raw_value[raw_len - 1])) {
        raw_len--;
    }
    if (raw_len == 0) {
        return false;
    }

    char value[SESSION_DE_PATH_MAX];
    if (raw_len >= sizeof(value)) {
        raw_len = sizeof(value) - 1;
    }
    memcpy(value, raw_value, raw_len);
    value[raw_len] = '\0';

    if (value[0] == '"') {
        size_t len = strlen(value);
        if (len >= 2 && value[len - 1] == '"') {
            memmove(value, value + 1, len - 2);
            value[len - 2] = '\0';
        } else {
            memmove(value, value + 1, len - 1);
            value[len - 1] = '\0';
        }
    }

    if (value[0] == '\0') {
        return false;
    }

    if (value[0] == '/') {
        strncpy(out_path, value, out_size - 1);
        out_path[out_size - 1] = '\0';
        return true;
    }

    if (strchr(value, '/')) {
        snprintf(out_path, out_size, "/%s", value);
        return true;
    }

    if (has_elf_suffix(value)) {
        snprintf(out_path, out_size, "/usr/bin/%s", value);
    } else {
        snprintf(out_path, out_size, "/usr/bin/%s.elf", value);
    }

    return true;
}

static bool parse_desktop_path_from_config(const char* config_data, uint32 config_size,
                                           char* out_path, size_t out_size) {
    if (!config_data || config_size == 0 || !out_path || out_size == 0) {
        return false;
    }

    const char* p = config_data;
    const char* end = config_data + config_size;

    while (p < end) {
        const char* line_start = p;
        while (p < end && *p != '\n' && *p != '\r') {
            p++;
        }
        const char* line_end = p;

        while (line_start < line_end && is_space_char(*line_start)) {
            line_start++;
        }
        while (line_end > line_start && is_space_char(line_end[-1])) {
            line_end--;
        }

        const char* value = NULL;
        if ((size_t)(line_end - line_start) > 8 && strncmp(line_start, "desktop=", 8) == 0) {
            value = line_start + 8;
        } else if ((size_t)(line_end - line_start) > 3 && strncmp(line_start, "DE=", 3) == 0) {
            value = line_start + 3;
        } else if ((size_t)(line_end - line_start) > 3 && strncmp(line_start, "de=", 3) == 0) {
            value = line_start + 3;
        }

        if (value && value < line_end) {
            char value_buf[SESSION_DE_PATH_MAX];
            size_t value_len = (size_t)(line_end - value);
            if (value_len >= sizeof(value_buf)) {
                value_len = sizeof(value_buf) - 1;
            }

            memcpy(value_buf, value, value_len);
            value_buf[value_len] = '\0';

            if (resolve_desktop_path(value_buf, out_path, out_size)) {
                return true;
            }
        }

        while (p < end && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }

    return false;
}

static bool load_user_desktop_path(const auth_user_info_t* user_info,
                                   char* out_path, size_t out_size) {
    if (!user_info || !out_path || out_size == 0 || !user_info->name[0]) {
        return false;
    }

    char user_config[256];
    snprintf(user_config, sizeof(user_config), "/home/%s/.session/.conf", user_info->name);

    const uint8* config_data = NULL;
    uint32 config_size = 0;
    if (!vfs_read_file(user_config, &config_data, &config_size) || !config_data || config_size == 0) {
        return false;
    }

    bool ok = parse_desktop_path_from_config((const char*)config_data, config_size, out_path, out_size);
    free((void*)config_data);
    return ok;
}

static bool load_system_desktop_path(char* out_path, size_t out_size) {
    if (!out_path || out_size == 0) {
        return false;
    }

    const uint8* sys_config_data = NULL;
    uint32 sys_config_size = 0;
    if (!vfs_read_file("/usr/share/sysconf/sys.conf", &sys_config_data, &sys_config_size) ||
        !sys_config_data || sys_config_size == 0) {
        return false;
    }

    bool ok = parse_desktop_path_from_config((const char*)sys_config_data, sys_config_size,
                                             out_path, out_size);
    free((void*)sys_config_data);
    return ok;
}

static bool load_first_elf(const char* const* paths, char* out_path, size_t out_path_size,
                           const uint8** out_data, uint32* out_size) {
    if (!paths || !out_path || out_path_size == 0 || !out_data || !out_size) {
        return false;
    }

    for (int i = 0; paths[i] != NULL; i++) {
        const uint8* elf_data = NULL;
        uint32 elf_size = 0;
        if (vfs_read_file(paths[i], &elf_data, &elf_size) && elf_data && elf_size > 0) {
            strncpy(out_path, paths[i], out_path_size - 1);
            out_path[out_path_size - 1] = '\0';
            *out_data = elf_data;
            *out_size = elf_size;
            return true;
        }
    }

    return false;
}

// Wait for a graphics task startup signal.
// Some apps may not update last_active_tick immediately, so we accept either:
//   1) observed activity tick, or
//   2) process remains alive for a short stability window.
static graphics_task_startup_result_t wait_for_graphics_task_startup(uint32 pid, const char* task_name) {
    uint32 launch_tick = timer_get_ticks();

    while (task_exists(pid)) {
        uint32 now = timer_get_ticks();
        if (task_get_last_active_tick(pid) != 0) {
            return GRAPHICS_TASK_STARTUP_OK;
        }

        if ((now - launch_tick) >= GRAPHICS_TASK_ASSUME_READY_TICKS) {
            return GRAPHICS_TASK_STARTUP_OK;
        }

        if ((now - launch_tick) > GRAPHICS_TASK_STARTUP_TIMEOUT_TICKS) {
            debuglog(DEBUG_ERROR,
                     "[SESSION] %s (PID %u) did not become responsive, terminating\n",
                     task_name ? task_name : "Graphics task",
                     pid);
            task_kill(pid);
            return GRAPHICS_TASK_STARTUP_TIMEOUT;
        }

        session_idle_wait();
    }

    // Task exited before showing activity - this may be normal (e.g., CanopyDM after login)
    // or it may indicate a crash. The caller will check auth status to determine which.
    debuglog(DEBUG_INFO,
             "[SESSION] %s exited before startup activity observed (may be normal if login completed)\n",
              task_name ? task_name : "Graphics task");
    return GRAPHICS_TASK_STARTUP_EXITED_EARLY;
}

void session_init_all(void) {
    if (g_sessions_initialized) {
        return;
    }

    for (int i = 0; i < MAX_TTY_SESSIONS; i++) {
        g_tty_sessions[i].session_id = i + 1;
        // Session 1 uses GUI, others use text TTY
        g_tty_sessions[i].type = (i == 0) ? SESSION_TYPE_GUI : SESSION_TYPE_TEXT;
        g_tty_sessions[i].state = SESSION_STATE_LOGIN;
        g_tty_sessions[i].logged_in = false;
        memset(&g_tty_sessions[i].user_info, 0, sizeof(auth_user_info_t));
        g_tty_sessions[i].shell_pid = 0;
        g_tty_sessions[i].initialized = true;
    }

    g_sessions_initialized = true;
    debuglog(DEBUG_INFO, "[SESSION] Initialized %d TTY sessions\n", MAX_TTY_SESSIONS);
}

tty_session_t* session_get_current(void) {
    if (!g_sessions_initialized || g_current_tty_session < 1 || g_current_tty_session > MAX_TTY_SESSIONS) {
        return NULL;
    }
    return &g_tty_sessions[g_current_tty_session - 1];
}

tty_session_t* session_get(uint32_t session_num) {
    if (!g_sessions_initialized || session_num < 1 || session_num > MAX_TTY_SESSIONS) {
        return NULL;
    }
    return &g_tty_sessions[session_num - 1];
}

void session_switch_to(uint32_t session_num) {
    if (session_num < 1 || session_num > MAX_TTY_SESSIONS) {
        return;
    }

    g_session_switch_pending = true;
    g_session_switch_target = session_num;
}

bool session_check_hotkey(void) {
    // Check if a session switch is pending
    if (g_session_switch_pending) {
        g_session_switch_pending = false;
        return true;
    }
    return false;
}

// ============================================================================
// TTY Login (fallback when canopydm is not available)
// ============================================================================

static void print_banner(void) {
    extern uint32_t g_current_tty_session;
    char banner[128];
    snprintf(banner, sizeof(banner), "\x1b[32mForest OS - TTY %u\x1b[0m\n", g_current_tty_session);
    tty_write_ansi(banner);
    tty_write_ansi("Type 'signup' at the username prompt to create an account.\n");
    tty_write_ansi("Press Ctrl+Alt+F1-F9 to switch TTY sessions.\n\n");
}

// Read input with hotkey checking - returns false if session switch occurred
static bool read_line_with_hotkey_check(char* buffer, size_t max_len, bool hidden) {
    size_t i = 0;
    uint32_t start_session = g_current_tty_session;
    uint8_t start_vt = tty_get_current_vt();

    while (1) {
        // Check for session switch or VT switch
        if (g_current_tty_session != start_session || tty_get_current_vt() != start_vt) {
            buffer[i] = '\0';
            return false;  // Session or VT switched, abort input
        }

        char ch = 0;
        if (!keyboard_poll_char(&ch)) {
            // No input available, yield and continue
            session_idle_wait();
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
            if (hidden) {
                tty_write("*");
            } else {
                char echo[2] = {ch, '\0'};
                tty_write(echo);
            }
        }
    }

    buffer[i] = '\0';
    return true;  // Input completed successfully
}

static bool handle_signup(void) {
    tty_write("New username: ");
    char name[SESSION_INPUT_MAX];
    if (!read_line_with_hotkey_check(name, sizeof(name), false) || name[0] == '\0') {
        return false;
    }

    tty_write("New password: ");
    char pass[SESSION_INPUT_MAX];
    if (!read_line_with_hotkey_check(pass, sizeof(pass), true) || pass[0] == '\0') {
        return false;
    }

    auth_result_t res = auth_signup(name, pass, "users", false);

    if (res == AUTH_OK) {
        tty_write("Account created.\n");
        return true;
    }
    tty_write("Signup failed. Root login may be required.\n");
    return false;
}

// Returns: 1 = login successful, 0 = keep trying, -1 = session switched
static int prompt_tty_login_once(auth_user_info_t* out_user) {
    uint32_t start_session = g_current_tty_session;
    uint8_t start_vt = tty_get_current_vt();

    tty_write("Username: ");
    char user[SESSION_INPUT_MAX];
    if (!read_line_with_hotkey_check(user, sizeof(user), false)) {
        return -1;  // Session or VT switched
    }

    if (user[0] == '\0') {
        return 0;  // Empty input, try again
    }

    if (strcmp(user, "signup") == 0) {
        handle_signup();
        return 0;  // Continue login loop
    }

    // Check if session or VT switched during username input
    if (g_current_tty_session != start_session || tty_get_current_vt() != start_vt) {
        return -1;
    }

    tty_write("Password: ");
    char pass[SESSION_INPUT_MAX];
    if (!read_line_with_hotkey_check(pass, sizeof(pass), true)) {
        return -1;  // Session or VT switched
    }

    // Check if session or VT switched during password input
    if (g_current_tty_session != start_session || tty_get_current_vt() != start_vt) {
        return -1;
    }

    auth_result_t res = auth_login(user, pass, out_user);

    if (res == AUTH_OK) {
        tty_write("Login successful.\n\n");
        return 1;  // Success
    }

    tty_write("Invalid credentials. Try again.\n");
    return 0;  // Keep trying
}

static bool prompt_tty_login(auth_user_info_t* out_user) {
    uint32_t start_session = g_current_tty_session;
    uint8_t start_vt = tty_get_current_vt();

    while (1) {
        int result = prompt_tty_login_once(out_user);
        if (result == 1) {
            return true;   // Login successful
        }
        if (result == -1) {
            // Check if just VT switched (not session)
            if (g_current_tty_session == start_session && tty_get_current_vt() != start_vt) {
                // VT switched but same session - redraw and continue
                tty_clear();
                print_banner();
                start_vt = tty_get_current_vt();
                continue;
            }
            return false;  // Session switched, exit login loop
        }
        // result == 0: keep trying
    }
}

// ============================================================================
// Graphical Login (CanopyDM)
// ============================================================================

// Try to launch CanopyDM for graphical login
// Returns true if login successful, false if should fall back to TTY.
// out_failed_to_start is set when CanopyDM could not be launched or became unresponsive.
static bool try_canopydm_login(auth_user_info_t* out_user, bool* out_failed_to_start) {
    if (out_failed_to_start) {
        *out_failed_to_start = false;
    }

    if (tty_in_boot_mode()) {
        tty_exit_boot_mode();
    }

    if (!graphics_is_initialized() || !tty_try_enable_graphics_backend()) {
        debuglog(DEBUG_WARN,
                 "[SESSION] Graphics backend unavailable for CanopyDM, falling back to TTY login\n");
        tty_write("[SESSION] Graphics backend unavailable. Falling back to TTY login.\n");
        if (out_failed_to_start) {
            *out_failed_to_start = true;
        }
        return false;
    }

    if (!window_manager_is_initialized()) {
        debuglog(DEBUG_WARN,
                 "[SESSION] Window manager not initialized, initializing now for CanopyDM...\n");
        graphics_result_t wm_result = window_manager_init();
        if (wm_result != GRAPHICS_SUCCESS) {
            debuglog(DEBUG_ERROR,
                     "[SESSION] Failed to initialize window manager: %d, falling back to TTY login\n",
                     wm_result);
            tty_write("[SESSION] Window manager init failed. Falling back to TTY login.\n");
            if (out_failed_to_start) {
                *out_failed_to_start = true;
            }
            return false;
        }
        debuglog(DEBUG_INFO, "[SESSION] Window manager initialized successfully\n");
    }

    // Also initialize app_graphics if not already done
    if (!app_graphics_is_initialized()) {
        debuglog(DEBUG_WARN,
                 "[SESSION] App graphics not initialized, initializing now for CanopyDM...\n");
        graphics_result_t ag_result = app_graphics_init();
        if (ag_result != GRAPHICS_SUCCESS) {
            debuglog(DEBUG_ERROR,
                     "[SESSION] Failed to initialize app graphics: %d, falling back to TTY login\n",
                     ag_result);
            tty_write("[SESSION] App graphics init failed. Falling back to TTY login.\n");
            if (out_failed_to_start) {
                *out_failed_to_start = true;
            }
            return false;
        }
        debuglog(DEBUG_INFO, "[SESSION] App graphics initialized successfully\n");
    }

    const char* dm_paths[] = {
        "/bin/canopydm.elf",
        "/usr/bin/canopydm.elf",
        NULL
    };

    const uint8* dm_elf_data = NULL;
    uint32 dm_elf_size = 0;
    const char* found_path = NULL;

    // Try to find canopydm
    for (int i = 0; dm_paths[i] != NULL; i++) {
        if (vfs_read_file(dm_paths[i], &dm_elf_data, &dm_elf_size) && dm_elf_data && dm_elf_size > 0) {
            found_path = dm_paths[i];
            break;
        }
    }

    if (!found_path) {
        debuglog(DEBUG_WARN, "[SESSION] CanopyDM not found, falling back to TTY login\n");
        tty_write("[SESSION] CanopyDM not found. Falling back to TTY login.\n");
        if (out_failed_to_start) {
            *out_failed_to_start = true;
        }
        return false;
    }

    debuglog_printf("[SESSION] Launching CanopyDM: %s\n", found_path);

    /*
     * Claim framebuffer ownership before task creation/switch so stale TTY
     * overlays never remain visible if launch succeeds.
     */
    tty_set_status_bar_visible(false);
    tty_set_graphics_app_active(true);

    // Create task for canopydm
    debuglog_printf("[SESSION] About to create CanopyDM task\n");
    task_t* dm_task = task_create_elf(dm_elf_data, dm_elf_size, "canopydm");
    debuglog_printf("[SESSION] After task_create_elf\n");
    if (!dm_task) {
        debuglog(DEBUG_ERROR, "[SESSION] Failed to create CanopyDM task\n");
        debuglog_printf("[SESSION] task_create_elf(canopydm) failed\n");
        tty_set_graphics_app_active(false);
        tty_set_status_bar_visible(true);
        tty_write("[SESSION] Failed to start CanopyDM.\n");
        if (out_failed_to_start) {
            *out_failed_to_start = true;
        }
        return false;
    }
    debuglog_printf("[SESSION] CanopyDM task created (pid=%u)\n", dm_task->id);

    // Set as foreground task for priority scheduling
    // This ensures the GUI app gets responsive scheduling
    debuglog_printf("[SESSION] About to set foreground\n");
    task_set_foreground(dm_task);
    debuglog_printf("[SESSION] After set foreground\n");

    // Start the display manager
    dm_task->state = TASK_STATE_READY;
    debuglog_printf("[SESSION] About to switch to CanopyDM task\n");
    task_switch(dm_task);
    debuglog_printf("[SESSION] After task_switch\n");

    // Wait for canopydm to complete, but allow session switching
    uint32 dm_pid = dm_task->id;
    uint32_t start_session = g_current_tty_session;

    graphics_task_startup_result_t dm_startup =
        wait_for_graphics_task_startup(dm_pid, "CanopyDM");
    if (dm_startup == GRAPHICS_TASK_STARTUP_TIMEOUT) {
        debuglog_printf("[SESSION] CanopyDM startup timeout\n");
        task_clear_foreground();
        tty_set_graphics_app_active(false);
        tty_set_status_bar_visible(true);
        tty_write("[SESSION] CanopyDM unresponsive (segmentation fault)\n");
        if (out_failed_to_start) {
            *out_failed_to_start = true;
        }
        return false;
    }
    if (dm_startup == GRAPHICS_TASK_STARTUP_EXITED_EARLY) {
        debuglog_printf("[SESSION] CanopyDM exited early before startup confirmation\n");
        // Can happen if CanopyDM logs in and exits very quickly.
        task_clear_foreground();
        auth_result_t auth_res = auth_get_current(out_user);
        if (auth_res == AUTH_OK && out_user->name[0] != '\0') {
            debuglog(DEBUG_INFO,
                     "[SESSION] CanopyDM exited quickly after successful login for '%s'\n",
                     out_user->name);
            tty_set_graphics_app_active(true);
            return true;
        }

        // Treat as normal login cancellation/failure, not startup failure.
        tty_set_graphics_app_active(false);
        tty_set_status_bar_visible(true);
        tty_write("[SESSION] CanopyDM closed (segmentation fault)\n");
        debuglog(DEBUG_INFO, "[SESSION] CanopyDM exited before login completion\n");
        return false;
    }

    // Enable graphics app mode once CanopyDM is confirmed responsive.
    tty_set_graphics_app_active(true);

    debuglog(DEBUG_WARN, "[SESSION] CanopyDM running as foreground (PID %u), entering wait loop...\n", dm_pid);

    uint32_t loop_count = 0;
    while (task_exists(dm_pid)) {
        // Check if user switched to a different TTY
        if (g_current_tty_session != start_session) {
            debuglog(DEBUG_INFO, "[SESSION] TTY switch during CanopyDM, suspending\n");
            // Clear foreground status when switching away
            task_clear_foreground();
            // Disable graphics app mode when switching away
            tty_set_graphics_app_active(false);
            // Don't kill the DM, just return - it can continue when we switch back
            return false;
        }

        session_idle_wait();
        
        loop_count++;
        if (loop_count % 1000 == 0) {
            debuglog(DEBUG_INFO, "[SESSION] Still waiting for CanopyDM (PID %u), loop=%u\n", dm_pid, loop_count);
        }
    }

    debuglog(DEBUG_INFO, "[SESSION] CanopyDM wait loop exited (task_exists=%d)\n", task_exists(dm_pid));
    // CanopyDM exited - clear foreground
    debuglog(DEBUG_INFO, "[SESSION] CanopyDM exited, clearing foreground...\n");
    task_clear_foreground();
    debuglog(DEBUG_INFO, "[SESSION] CanopyDM exited normally\n");

    // Check if login was successful by checking if a user is now logged in
    debuglog(DEBUG_INFO, "[SESSION] Checking authentication status...\n");
    auth_result_t auth_res = auth_get_current(out_user);
    debuglog(DEBUG_INFO, "[SESSION] auth_get_current returned: %d, user: '%s'\n", auth_res, out_user->name[0] ? out_user->name : "(none)");
    
    if (auth_res == AUTH_OK && out_user->name[0] != '\0') {
        /*
         * Keep graphics ownership during DM -> DE handoff to avoid a
         * transient TTY redraw touching the framebuffer between GUI apps.
         */
        debuglog(DEBUG_INFO, "[SESSION] Login successful, keeping graphics mode active\n");
        tty_set_graphics_app_active(true);
        debuglog(DEBUG_INFO, "[SESSION] CanopyDM login successful for user '%s'\n", out_user->name);
        return true;
    }

    // Login failed/cancelled: return ownership to TTY.
    debuglog(DEBUG_INFO, "[SESSION] Login failed or cancelled, returning to TTY mode\n");
    tty_set_graphics_app_active(false);
    tty_write("[SESSION] CanopyDM closed (segmentation fault)\n");
    debuglog(DEBUG_INFO, "[SESSION] CanopyDM exited without successful login\n");
    return false;
}

// ============================================================================
// Desktop Environment / Shell Launch
// ============================================================================

static void launch_user_session(auth_user_info_t* user_info, tty_session_t* session) {
    debuglog(DEBUG_INFO, "[SESSION] launch_user_session() called for user '%s' on TTY %u (type=%s)\n",
             user_info->name, session->session_id, 
             session->type == SESSION_TYPE_GUI ? "GUI" : "TEXT");
    
    // Load DE configuration from system config or user config
    char de_path[SESSION_DE_PATH_MAX];
    bool de_found = false;

    // For GUI sessions, try to launch DE; for text sessions, launch shell
    if (session->type == SESSION_TYPE_GUI &&
        (!graphics_is_initialized() || !tty_try_enable_graphics_backend() || !tty_is_ready())) {
        debuglog(DEBUG_WARN,
                 "[SESSION] Graphics backend unavailable for DE launch, falling back to shell\n");
        session->type = SESSION_TYPE_TEXT;
    }

    if (session->type == SESSION_TYPE_GUI) {
        strcpy(de_path, "/bin/canopyde.elf");

        // Prefer user-specific desktop config over system-wide config.
        de_found = load_user_desktop_path(user_info, de_path, sizeof(de_path));
        if (!de_found) {
            de_found = load_system_desktop_path(de_path, sizeof(de_path));
        }
        if (!de_found) {
            strcpy(de_path, "/bin/canopyde.elf");
        }

        // Try to launch the Desktop Environment
        const uint8* de_elf_data = NULL;
        uint32 de_elf_size = 0;
        bool de_loaded = vfs_read_file(de_path, &de_elf_data, &de_elf_size) &&
                         de_elf_data && de_elf_size > 0;

        if (!de_loaded) {
            char fallback_path[SESSION_DE_PATH_MAX];
            fallback_path[0] = '\0';
            const char* base = strrchr(de_path, '/');
            base = base ? base + 1 : de_path;

            if (strncmp(de_path, "/usr/bin/", 9) == 0) {
                snprintf(fallback_path, sizeof(fallback_path), "/bin/%s", base);
            } else if (strncmp(de_path, "/bin/", 5) == 0) {
                snprintf(fallback_path, sizeof(fallback_path), "/usr/bin/%s", base);
            }

            if (fallback_path[0] != '\0' &&
                vfs_read_file(fallback_path, &de_elf_data, &de_elf_size) &&
                de_elf_data && de_elf_size > 0) {
                strncpy(de_path, fallback_path, sizeof(de_path) - 1);
                de_path[sizeof(de_path) - 1] = '\0';
                de_loaded = true;
            }
        }

        if (de_loaded) {
            debuglog(DEBUG_INFO, "[SESSION] DE ELF loaded: %s (%u bytes)\n", de_path, de_elf_size);
            
            char de_task_name[32] = "desktop";
            const char* base = strrchr(de_path, '/');
            base = base ? base + 1 : de_path;
            size_t i = 0;
            while (base[i] && base[i] != '.' && i + 1 < sizeof(de_task_name)) {
                de_task_name[i] = base[i];
                i++;
            }
            de_task_name[i] = '\0';

            debuglog(DEBUG_INFO, "[SESSION] Creating DE task '%s'...\n", de_task_name);
            task_t* de_task = task_create_elf(de_elf_data, de_elf_size, de_task_name);
            if (de_task) {
                debuglog(DEBUG_INFO, "[SESSION] DE task created (PID %u), preparing to launch...\n", de_task->id);

                // GUI app owns framebuffer while DE is active.
                debuglog(DEBUG_INFO, "[SESSION] Setting graphics app active...\n");
                tty_set_graphics_app_active(true);
                tty_set_status_bar_visible(false);

                // Set as foreground task for priority scheduling
                debuglog(DEBUG_INFO, "[SESSION] Setting foreground task (PID %u)...\n", de_task->id);
                task_set_foreground(de_task);

                de_task->state = TASK_STATE_READY;
                session->shell_pid = de_task->id;
                
                debuglog(DEBUG_INFO, "[SESSION] Switching to DE task (PID %u)...\n", de_task->id);
                task_switch(de_task);

                // Wait for DE to complete
                uint32 de_pid = de_task->id;
                debuglog(DEBUG_INFO, "[SESSION] DE running as foreground (PID %u), waiting for startup...\n", de_pid);

                debuglog(DEBUG_INFO, "[SESSION] Waiting for DE startup confirmation...\n");
                graphics_task_startup_result_t de_startup =
                    wait_for_graphics_task_startup(de_pid, "Desktop environment");
                if (de_startup != GRAPHICS_TASK_STARTUP_OK) {
                    if (de_startup == GRAPHICS_TASK_STARTUP_TIMEOUT) {
                        debuglog(DEBUG_ERROR, "[SESSION] DE startup FAILED (PID %u timed out)\n", de_pid);
                    } else {
                        debuglog(DEBUG_ERROR, "[SESSION] DE startup FAILED (PID %u exited early)\n", de_pid);
                    }
                    task_clear_foreground();
                    session->shell_pid = 0;
                    tty_set_graphics_app_active(false);
                    tty_set_status_bar_visible(true);
                    tty_clear();
                    tty_force_redraw();
                    debuglog(DEBUG_ERROR,
                             "[SESSION] DE failed to start, falling back to shell\n");
                    session->type = SESSION_TYPE_TEXT;
                } else {
                    debuglog(DEBUG_INFO, "[SESSION] DE startup confirmed (PID %u), entering main loop...\n", de_pid);
                    /*
                     * Keep foreground boost only for DE startup.
                     * Once the desktop is running, clear it so spawned app tasks
                     * can be scheduled fairly and cannot be starved by the DE loop.
                     */
                    task_clear_foreground();
                    while (task_exists(de_pid)) {
                        task_schedule();
                        session_idle_wait();
                    }
                    debuglog(DEBUG_INFO, "[SESSION] DE exited (PID %u no longer exists)\n", de_pid);

                    // DE exited - restore TTY ownership and redraw cleanly.
                    session->shell_pid = 0;
                    tty_set_graphics_app_active(false);
                    tty_set_status_bar_visible(true);
                    tty_clear();
                    tty_force_redraw();
                    debuglog(DEBUG_INFO, "[SESSION] DE session ended\n");
                    return;
                }
            } else {
                debuglog(DEBUG_ERROR, "[SESSION] Failed to create DE task: %s\n", de_path);
                session->type = SESSION_TYPE_TEXT;
            }
        } else {
            debuglog(DEBUG_INFO, "[SESSION] DE not found: %s, falling back to shell\n", de_path);
            session->type = SESSION_TYPE_TEXT;
        }

        // Could not start DE; return GUI ownership back to TTY fallback path.
        tty_set_graphics_app_active(false);
        tty_set_status_bar_visible(true);
        tty_clear();
        tty_force_redraw();
    }

    // Launch shell (for text sessions or when DE fails) using /bin/sh.elf first.
    debuglog(DEBUG_INFO, "[SESSION] Launching shell fallback for user '%s' on TTY %u\n",
             user_info->name, session->session_id);

    const char* shell_paths[] = {
        "/bin/sh.elf",
        "/usr/bin/sh.elf",
        "/hbin/shell.elf",
        "/bin/shell.elf",
        NULL
    };
    char shell_path[SESSION_DE_PATH_MAX];
    const uint8* shell_elf_data = NULL;
    uint32 shell_elf_size = 0;
    uint32 shell_pid = 0;
    task_t* shell_task = NULL;

    if (load_first_elf(shell_paths, shell_path, sizeof(shell_path), &shell_elf_data, &shell_elf_size)) {
        shell_task = task_create_elf(shell_elf_data, shell_elf_size, "sh");
        if (shell_task) {
            shell_pid = shell_task->id;
            debuglog(DEBUG_INFO, "[SESSION] Shell launched from %s (PID %u)\n", shell_path, shell_pid);
        } else {
            debuglog(DEBUG_ERROR, "[SESSION] Failed to create shell task from %s\n", shell_path);
        }
    } else {
        debuglog(DEBUG_WARN, "[SESSION] /bin/sh.elf not found, trying embedded shell loader\n");
        bool shell_ok = shell_launch_embedded();
        shell_pid = shell_get_last_pid();
        shell_task = shell_get_last_task();
        if (!shell_ok || shell_pid == 0) {
            shell_task = NULL;
        }
    }

    if (!shell_task || shell_pid == 0) {
        tty_write("Shell failed to start.\n");
        return;
    }

    session->shell_pid = shell_pid;

    if (shell_task) {
        task_set_foreground(shell_task);
        shell_task->state = TASK_STATE_READY;
        task_switch(shell_task);
    }

    // Wait for shell to complete
    const uint32 startup_timeout_ticks = 300; // ~3s at 100Hz
    uint32 launch_tick = timer_get_ticks();
    bool saw_activity = false;

    while (shell_pid != 0 && task_exists(shell_pid)) {
        uint32 last_tick = task_get_last_active_tick(shell_pid);
        if (last_tick != 0) {
            saw_activity = true;
        }

        uint32 now = timer_get_ticks();
        if (!saw_activity && (now - launch_tick) > startup_timeout_ticks) {
            tty_write("Shell appears unresponsive. Terminating and returning to login.\n");
            task_kill(shell_pid);
            saw_activity = true;
        }

        session_idle_wait();
    }

    task_clear_foreground();
    session->shell_pid = 0;
}

// ============================================================================
// Per-Session Login Handler
// ============================================================================

static void run_session_login(tty_session_t* session, bool autologin_root) {
    session->state = SESSION_STATE_LOGIN;
    session->logged_in = false;

    // Hide status bar for clean login screen (Windows 7 style)
    extern void tty_set_status_bar_visible(bool visible);
    tty_set_status_bar_visible(false);

    // Clear screen and show appropriate login
    tty_clear();

    // Handle auto-login for root (debug/testing mode)
    if (autologin_root && auth_force_login("root") == AUTH_OK) {
        auth_find_user("root", &session->user_info);
        session->logged_in = true;
        tty_write_ansi("\x1b[33mAuto-login enabled for root\x1b[0m\n");
    }

    // For GUI session (TTY 1), try graphical login first
    if (!session->logged_in && session->type == SESSION_TYPE_GUI) {
        debuglog(DEBUG_WARN, "[SESSION] Attempting CanopyDM login on TTY %u...\n", session->session_id);
        tty_write("Starting graphical login (CanopyDM)...\n");
        bool failed_to_start = false;
        session->logged_in = try_canopydm_login(&session->user_info, &failed_to_start);
        debuglog(DEBUG_WARN, "[SESSION] CanopyDM login result: logged_in=%d, failed_to_start=%d\n",
                 session->logged_in, failed_to_start);
        if (!session->logged_in && failed_to_start) {
            debuglog(DEBUG_WARN,
                     "[SESSION] Disabling GUI login on TTY %u due to startup failure\n",
                     session->session_id);
            tty_write("CanopyDM failed to start. Falling back to TTY login.\n");
            session->type = SESSION_TYPE_TEXT;
        }
    }

    // Fall back to or use TTY login
    if (!session->logged_in) {
        tty_clear();
        print_banner();
        session->logged_in = prompt_tty_login(&session->user_info);
    }

    if (!session->logged_in) {
        // Login was aborted (likely due to session switch)
        return;
    }

    // Mark session as active
    session->state = SESSION_STATE_ACTIVE;

    // Show status bar now that user is logged in
    tty_set_status_bar_visible(true);

    debuglog(DEBUG_INFO, "[SESSION] User '%s' logged in on TTY %u\n",
             session->user_info.name, session->session_id);

    // Launch user session (DE or shell)
    debuglog(DEBUG_INFO, "[SESSION] About to launch user session...\n");
    launch_user_session(&session->user_info, session);

    // Session ended, logout
    session->state = SESSION_STATE_LOGOUT;
    auth_logout();
    session->logged_in = false;
    memset(&session->user_info, 0, sizeof(auth_user_info_t));

    // Clear screen for next login
    tty_clear();
    tty_write("Session ended. Returning to login...\n\n");

    // Reset to login state
    session->state = SESSION_STATE_LOGIN;
}

// ============================================================================
// Main Session Loop
// ============================================================================

void session_run(bool autologin_root) {
    debuglog_printf("[SESSION] session_run enter\n");
    auth_init();
    debuglog_printf("[SESSION] auth_init complete\n");

    // Initialize all TTY sessions
    session_init_all();

    // Prefer the PS/2 driver for proper scancode translation
    keyboard_set_driver_mode(KEYBOARD_DRIVER_PS2);

    // Ensure the framebuffer TTY is active and clean
    tty_try_enable_graphics_backend();
    if (tty_in_boot_mode()) {
        tty_exit_boot_mode();
    }
    tty_clear();
    tty_write_ansi("\x1b[0m");
    tty_write("Starting session manager...\n");
    debuglog_printf("[SESSION] tty prepared, entering session loop\n");

    // Drop any stale scancodes from boot
    keyboard_clear_buffers();

    debuglog(DEBUG_WARN, "[SESSION] Starting multi-TTY session manager\n");
    debuglog(DEBUG_WARN, "[SESSION] TTY 1: GUI login, TTY 2-9: Text TTY login\n");
    debuglog(DEBUG_WARN, "[SESSION] Use Ctrl+Alt+F1-F9 to switch sessions\n");

    // Main session loop - runs for each TTY session
    while (1) {
        // Get current session
        tty_session_t* current = session_get_current();
        if (!current) {
            // Invalid session, default to session 1
            g_current_tty_session = 1;
            current = session_get_current();
            if (!current) {
                debuglog(DEBUG_ERROR, "[SESSION] Failed to get session, halting\n");
                while (1) { __asm__ __volatile__("hlt"); }
            }
        }

        // Run login/session for current TTY
        run_session_login(current, autologin_root);

        // After session ends or switch, continue loop
        // The hotkey handler will update g_current_tty_session when Ctrl+Alt+Fn is pressed
    }
}
