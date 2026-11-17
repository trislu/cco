#define CCO_IMPLEMENTATION
#include "../../cco.h"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);
    Cco_Workspace ws;
    cco_workspace_init(&ws, argc, argv);
    Cco_Target *app = cco_executable(&ws, "hello");
    cco_target_add_source(app, "src/main.c");
    return cco_workspace_run(&ws);
}
