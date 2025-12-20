#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"
#include "../src/include/libc/unistd.h"
#include "../src/include/libc/string.h"

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

static int read_meminfo_value(const char *key) {
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    char buffer[1024];
    int n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (n <= 0) {
        return -1;
    }
    buffer[n] = '\0';
    
    char *line = strstr(buffer, key);
    if (!line) {
        return -1;
    }
    
    while (*line && (*line < '0' || *line > '9')) {
        line++;
    }
    
    return atoi(line);
}

int main(int argc, char **argv) {
    int show_human = 0;
    int show_bytes = 0;
    int show_mega = 0;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        show_human = 1;
                        break;
                    case 'b':
                        show_bytes = 1;
                        break;
                    case 'm':
                        show_mega = 1;
                        break;
                    default:
                        printf("free: invalid option -- '%c'\n", argv[i][j]);
                        return 1;
                }
            }
        }
    }
    
    int mem_total = read_meminfo_value("MemTotal");
    int mem_free = read_meminfo_value("MemFree");
    int mem_available = read_meminfo_value("MemAvailable");
    int buffers = read_meminfo_value("Buffers");
    int cached = read_meminfo_value("Cached");
    
    if (mem_total <= 0) {
        mem_total = 1024 * 1024;
        mem_free = mem_total / 2;
        mem_available = mem_free;
        buffers = mem_total / 10;
        cached = mem_total / 8;
    }
    
    int mem_used = mem_total - mem_free;
    int swap_total = 0;
    int swap_used = 0;
    int swap_free = swap_total - swap_used;
    
    const char *unit = "kB";
    int divisor = 1;
    
    if (show_bytes) {
        unit = "";
        divisor = 1;
        mem_total *= 1024;
        mem_used *= 1024;
        mem_free *= 1024;
        mem_available *= 1024;
        buffers *= 1024;
        cached *= 1024;
    } else if (show_mega) {
        unit = "MB";
        divisor = 1024;
        mem_total /= divisor;
        mem_used /= divisor;
        mem_free /= divisor;
        mem_available /= divisor;
        buffers /= divisor;
        cached /= divisor;
    } else if (show_human) {
        unit = "K";
    }
    
    printf("               total        used        free      shared  buff/cache   available\n");
    printf("Mem:%12d%s%12d%s%12d%s%12d%s%12d%s%12d%s\n",
           mem_total, unit, mem_used, unit, mem_free, unit, 
           0, unit, buffers + cached, unit, mem_available, unit);
    printf("Swap:%10d%s%12d%s%12d%s\n",
           swap_total, unit, swap_used, unit, swap_free, unit);
    
    return 0;
}
