#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    cco::Workspace ws(argc, argv);

    std::string output = "dist/custom-output";
#ifdef _WIN32
    output += ".exe";
#endif

    [[maybe_unused]] auto &app = ws.executable("custom-output")
        .sources({"src/main.cpp"})
        .output_to(output);

    return ws.run();
}
