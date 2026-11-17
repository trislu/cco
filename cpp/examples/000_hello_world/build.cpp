#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    cco::Workspace ws(argc, argv);
    [[maybe_unused]] auto &app = ws.executable("hello")
        .sources(cco::glob("src/**/*.cpp"));

    return ws.run();
}
