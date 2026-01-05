#include "../src/include/libc/unistd.h"
#include "../src/include/libc/stdlib.h"

// Minimal C entrypoint: kernel jumps here directly. We do not rely on the
// initial stack layout (argc/argv/envp) and simply call main with zeros.
// If main returns, exit with its status.
void _start(void) {
    extern int main(int argc, char** argv, char** envp);
    int ret = main(0, 0, 0);
    _exit(ret);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
