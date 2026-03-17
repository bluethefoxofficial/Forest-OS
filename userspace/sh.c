#include "tool_runtime.h"
#include "libc/process.h"

extern int chdir(const char *path);
extern char *getcwd(char *buf, size_t size);

#define MAX_ARGS 16
#define MAX_INPUT 256

static char g_cwd[256] = "/";

static void builtin_cd(const char *path) {
    if (!path || !*path) {
        printf("cd: missing path\n");
        return;
    }
    if (chdir(path) != 0) {
        printf("cd: %s: No such file or directory\n", path);
        return;
    }
    getcwd(g_cwd, sizeof(g_cwd));
}

static void builtin_pwd(void) {
    printf("%s\n", g_cwd);
}

static void builtin_exit(const char *arg) {
    int status = 0;
    if (arg && *arg) {
        status = atoi(arg);
    }
    _exit(status);
}

static int parse_command(char *line, char **argv, int max_args) {
    int argc = 0;
    int in_token = 0;
    
    while (*line && argc < max_args - 1) {
        if (*line == ' ' || *line == '\t') {
            if (in_token) {
                *line = '\0';
                in_token = 0;
            }
        } else {
            if (!in_token) {
                argv[argc++] = line;
                in_token = 1;
            }
        }
        line++;
    }
    argv[argc] = NULL;
    return argc;
}

static int is_builtin(const char *cmd) {
    return strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "true") == 0 ||
           strcmp(cmd, "false") == 0;
}

static void run_builtin(const char *cmd, char **argv, int argc) {
    if (strcmp(cmd, "cd") == 0) {
        builtin_cd(argv[1]);
    } else if (strcmp(cmd, "pwd") == 0) {
        builtin_pwd();
    } else if (strcmp(cmd, "exit") == 0) {
        builtin_exit(argv[1]);
    } else if (strcmp(cmd, "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) printf(" ");
            printf("%s", argv[i]);
        }
        printf("\n");
    } else if (strcmp(cmd, "true") == 0) {
    } else if (strcmp(cmd, "false") == 0) {
    }
}

static void run_command(char **argv) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return;
    }
    if (pid == 0) {
        execve(argv[0], argv, NULL);
        printf("%s: command not found\n", argv[0]);
        _exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char line[MAX_INPUT];
    
    getcwd(g_cwd, sizeof(g_cwd));
    
    while (1) {
        printf("sh:%s$ ", g_cwd);
        if (tr_read_line(NULL, line, sizeof(line)) < 0) {
            break;
        }
        
        if (line[0] == '\0') {
            continue;
        }
        
        char *cmd_args[MAX_ARGS];
        int argcnt = parse_command(line, cmd_args, MAX_ARGS);
        
        if (argcnt == 0) {
            continue;
        }
        
        if (is_builtin(cmd_args[0])) {
            run_builtin(cmd_args[0], cmd_args, argcnt);
        } else {
            run_command(cmd_args);
        }
    }
    
    printf("logout\n");
    return 0;
}
