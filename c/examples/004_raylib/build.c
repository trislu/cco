#define CCO_IMPLEMENTATION
#include "../../cco.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    char raylib_dir[4096] = "/usr/local";
    const char *env = getenv("RAYLIB_DIR");
    if (env) {
        snprintf(raylib_dir, sizeof(raylib_dir), "%s", env);
    }

    char header[8192];
    int header_len = snprintf(header, sizeof(header), "%s/include/raylib.h", raylib_dir);
    if (header_len < 0 || (size_t)header_len >= sizeof(header)) {
        fprintf(stderr, "raylib path is too long\n");
        return 1;
    }
    FILE *f = fopen(header, "rb");
    if (!f) {
        fprintf(stderr,
                "raylib not found in `%s`.\n"
                "Set RAYLIB_DIR to a raylib prefix.\n",
                raylib_dir);
        return 1;
    }
    fclose(f);

    Cco_Workspace ws;
    cco_workspace_init(&ws, argc, argv);

    Cco_Target *app = cco_executable(&ws, "raylib_demo");
    cco_target_add_source(app, "src/main.c");

    char include_dir[8192];
    int include_len = snprintf(include_dir, sizeof(include_dir), "%s/include", raylib_dir);
    if (include_len < 0 || (size_t)include_len >= sizeof(include_dir)) {
        fprintf(stderr, "raylib include path is too long\n");
        return 1;
    }
    cco_target_add_include(app, include_dir);

    char lib_dir[8192];
    int lib_len = snprintf(lib_dir, sizeof(lib_dir), "-L%s/lib", raylib_dir);
    if (lib_len < 0 || (size_t)lib_len >= sizeof(lib_dir)) {
        fprintf(stderr, "raylib library path is too long\n");
        return 1;
    }
    cco_target_add_linkflag(app, lib_dir);
    cco_target_add_linkflag(app, "-lraylib");

#if defined(__APPLE__)
    cco_target_add_linkflag(app, "-framework");
    cco_target_add_linkflag(app, "OpenGL");
    cco_target_add_linkflag(app, "-framework");
    cco_target_add_linkflag(app, "Cocoa");
    cco_target_add_linkflag(app, "-framework");
    cco_target_add_linkflag(app, "IOKit");
    cco_target_add_linkflag(app, "-framework");
    cco_target_add_linkflag(app, "CoreVideo");
#elif defined(_WIN32)
    cco_target_add_linkflag(app, "-lopengl32");
    cco_target_add_linkflag(app, "-lgdi32");
    cco_target_add_linkflag(app, "-lwinmm");
#else
    cco_target_add_linkflag(app, "-lGL");
    cco_target_add_linkflag(app, "-lm");
    cco_target_add_linkflag(app, "-lpthread");
    cco_target_add_linkflag(app, "-ldl");
    cco_target_add_linkflag(app, "-lrt");
    cco_target_add_linkflag(app, "-lX11");
#endif

    return cco_workspace_run(&ws);
}
