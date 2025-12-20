#define TOOL_NAME "parted"
#include "forest_toolbox.h"
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ftb_run_tool(TOOL_NAME);
    return 0;
}
