#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    cco::Workspace ws(argc, argv);

    [[maybe_unused]] auto &greeter = ws.shared_library("greeter")
                                         .sources({"src/greeter.cpp"})
                                         .include("include")
                                         .cxxflag("-DGREETER_BUILD_DLL");

    [[maybe_unused]] auto &app = ws.executable("app")
                                     .sources({"src/main.cpp"})
                                     .include("include")
                                     .link(greeter);

    return ws.run();
}
