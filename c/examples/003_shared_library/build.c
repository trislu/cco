#define CCO_IMPLEMENTATION
#include "../../cco.h"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);
    Cco_Workspace ws;
    cco_workspace_init(&ws, argc, argv);

    Cco_Target *greeter = cco_shared_library(&ws, "greeter");
    cco_target_add_source(greeter, "src/greeter.c");
    cco_target_add_include(greeter, "include");

    Cco_Target *app = cco_executable(&ws, "app");
    cco_target_add_source(app, "src/main.c");
    cco_target_add_include(app, "include");
    cco_target_link(app, greeter);

    return cco_workspace_run(&ws);
}
