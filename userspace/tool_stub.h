#ifndef USERSPACE_TOOL_STUB_H
#define USERSPACE_TOOL_STUB_H

#include "../src/include/libc/stdio.h"
#include "../src/include/libc/stdlib.h"

static inline void tool_unimplemented(const char *name, const char *desc) {
    printf("%s: %s is not available in this build of Forest OS userland yet.\n", name, desc);
    printf("This utility is currently a stub placeholder.\n");
    exit(1);
}

#define DEFINE_STUB_TOOL(name, desc) \
void _start(void) { \
    tool_unimplemented(name, desc); \
}

#endif
