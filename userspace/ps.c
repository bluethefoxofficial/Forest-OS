#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

static void show_process_info(int pid, const char *cmd, const char *state, int cpu_percent, int mem_kb) {
    printf("%5d %-12s %c %3d%% %6d %s\n", pid, "user", state[0], cpu_percent, mem_kb, cmd);
}

int main(int argc, char **argv) {
    int show_all = 0;
    int show_full = 0;
    int show_threads = 0;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'a':
                        show_all = 1;
                        break;
                    case 'f':
                        show_full = 1;
                        break;
                    case 'T':
                        show_threads = 1;
                        break;
                    case 'x':
                        show_all = 1;
                        break;
                    default:
                        printf("ps: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        }
    }
    
    if (show_full) {
        printf("  PID USER         STAT %%CPU  VMEM CMD\n");
    } else {
        printf("  PID CMD\n");
    }
    
    const char *processes[][4] = {
        {"1", "init", "S", "Forest OS init process"},
        {"2", "kthreadd", "S", "Kernel thread daemon"},
        {"3", "migration/0", "S", "CPU migration thread"},
        {"4", "ksoftirqd/0", "S", "Software IRQ daemon"},
        {"5", "watchdog/0", "S", "System watchdog"},
        {"100", "forest-sh", "S", "Forest OS shell"},
        {"101", "forest-vfs", "S", "Virtual file system"},
        {"102", "forest-net", "S", "Network subsystem"},
        {"200", "user-shell", "R", "User interactive shell"},
        {"201", "ps", "R", "Process status command"},
        {NULL, NULL, NULL, NULL}
    };
    
    for (int i = 0; processes[i][0]; i++) {
        int pid = atoi(processes[i][0]);
        const char *cmd = processes[i][1];
        const char *state = processes[i][2];
        const char *desc = processes[i][3];
        
        if (!show_all && pid < 100) {
            continue;
        }
        
        if (show_full) {
            show_process_info(pid, desc, state, pid % 10, (pid * 123) % 1000);
        } else {
            printf("%5d %s\n", pid, cmd);
        }
    }
    
    return 0;
}
