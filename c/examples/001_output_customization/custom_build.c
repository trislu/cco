#define CCO_IMPLEMENTATION
#include "../../cco.h"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);
    Cco_Workspace ws;
    cco_workspace_init(&ws, argc, argv);
    Cco_Target *app = cco_executable(&ws, "custom-output");
    cco_target_add_source(app, "src/main.c");
    cco_target_output_to(app, "dist/custom-output");
    return cco_workspace_run(&ws);
}
