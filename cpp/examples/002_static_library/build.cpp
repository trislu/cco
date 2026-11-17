#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    cco::Workspace ws(argc, argv);

    [[maybe_unused]] auto &greeter =
        ws.library("greeter").sources({"src/greeter.cpp"}).include("include");

    [[maybe_unused]] auto &app = ws.executable("app")
                                     .sources({"src/main.cpp"})
                                     .include("include")
                                     .link(greeter);

    [[maybe_unused]] auto &test = ws.test("greeter_test")
                                      .sources({"tests/greeter_test.cpp"})
                                      .include("include")
                                      .link(greeter);

    return ws.run();
}
