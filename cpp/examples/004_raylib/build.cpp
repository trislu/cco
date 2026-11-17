#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    std::string raylib_dir = "/usr/local";
    if (const char *env = std::getenv("RAYLIB_DIR"))
        raylib_dir = env;

    if (!std::filesystem::exists(raylib_dir + "/include/raylib.h")) {
        std::fprintf(stderr,
                     "raylib not found in `%s`.\n"
                     "Set RAYLIB_DIR to a raylib prefix.\n",
                     raylib_dir.c_str());
        return 1;
    }

    cco::Workspace ws(argc, argv);
    auto &app = ws.executable("raylib_demo")
                    .sources({"src/main.cpp"})
                    .include(raylib_dir + "/include")
                    .linkflag("-L" + raylib_dir + "/lib")
                    .linkflag("-lraylib");

#if defined(__APPLE__)
    app.linkflag("-framework").linkflag("OpenGL");
    app.linkflag("-framework").linkflag("Cocoa");
    app.linkflag("-framework").linkflag("IOKit");
    app.linkflag("-framework").linkflag("CoreVideo");
#elif defined(_WIN32)
    app.linkflag("-lopengl32").linkflag("-lgdi32").linkflag("-lwinmm");
#else
    app.linkflag("-lGL").linkflag("-lm").linkflag("-lpthread");
    app.linkflag("-ldl").linkflag("-lrt").linkflag("-lX11");
#endif

    return ws.run();
}
