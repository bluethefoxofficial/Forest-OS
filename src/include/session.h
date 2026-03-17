#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "auth.h"

// Maximum number of TTY sessions supported
#define MAX_TTY_SESSIONS 9

// Session type - determines how the session handles login
typedef enum {
    SESSION_TYPE_GUI,       // Graphical login (CanopyDM) - typically TTY 1
    SESSION_TYPE_TEXT       // Text-based TTY login
} session_type_t;

// Session state - tracks where the session is in its lifecycle
typedef enum {
    SESSION_STATE_LOGIN,    // Waiting for login
    SESSION_STATE_ACTIVE,   // User logged in, session running
    SESSION_STATE_LOGOUT    // Session ending, returning to login
} session_state_t;

// Per-TTY session information
typedef struct {
    uint32_t session_id;            // TTY session number (1-9)
    session_type_t type;            // GUI or text login
    session_state_t state;          // Current session state
    bool logged_in;                 // Whether a user is logged in
    auth_user_info_t user_info;     // Logged in user info
    uint32_t shell_pid;             // PID of running shell/DE (0 if none)
    bool initialized;               // Whether this session has been set up
} tty_session_t;

// Drive the interactive login/signup loop and shell lifecycle.
// When autologin_root is true, root is logged in automatically at boot.
void session_run(bool autologin_root);

// Get the current TTY session structure
tty_session_t* session_get_current(void);

// Get a specific TTY session by number (1-9)
tty_session_t* session_get(uint32_t session_num);

// Initialize all TTY sessions
void session_init_all(void);

// Called when switching to a different TTY session
void session_switch_to(uint32_t session_num);

// Check if hotkey was pressed during input (for TTY switching during login)
bool session_check_hotkey(void);

#endif // SESSION_H
