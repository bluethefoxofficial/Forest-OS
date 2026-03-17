#ifndef SIGNAL_H
#define SIGNAL_H

/* Standard POSIX signal numbers */
#define SIGHUP       1  /* Hangup */
#define SIGINT       2  /* Interrupt */
#define SIGQUIT      3  /* Quit */
#define SIGILL       4  /* Illegal instruction */
#define SIGTRAP      5  /* Trace/breakpoint trap */
#define SIGABRT      6  /* Aborted */
#define SIGBUS       7  /* Bus error */
#define SIGFPE       8  /* Floating point exception */
#define SIGKILL      9  /* Killed */
#define SIGSEGV     11  /* Segmentation fault */
#define SIGPIPE     13  /* Broken pipe */
#define SIGALRM     14  /* Alarm clock */
#define SIGTERM     15  /* Terminated */
#define SIGSTOP     19  /* Stopped (signal) */
#define SIGTSTP     20  /* Stopped (user) */
#define SIGCONT     18  /* Continue */
#define SIGTTIN     21  /* Stopped (tty input) */
#define SIGTTOU     22  /* Stopped (tty output) */
#define SIGWINCH    28  /* Window changed */

#endif /* SIGNAL_H */
