#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>

// Drive the interactive login/signup loop and shell lifecycle.
// When autologin_root is true, root is logged in automatically at boot.
void session_run(bool autologin_root);

#endif // SESSION_H
