#define CCO_IMPLEMENTATION
#include "../../cco.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char **argv) {
    CCO_GO_REBUILD_URSELF(argc, argv);

    std::string prefix = "/usr";
    if (const char *env = std::getenv("PROTOBUF_DIR")) prefix = env;

    std::string protoc = "protoc";
    if (const char *env = std::getenv("PROTOC")) protoc = env;

    std::error_code ec;
    std::filesystem::create_directories("generated", ec);
    if (ec) {
        std::fprintf(stderr, "cannot create generated/: %s\n", ec.message().c_str());
        return 1;
    }

    if (!std::filesystem::exists("generated/addressbook.pb.cc")) {
        std::string cmd = "\"" + protoc +
                          "\" -I proto --cpp_out=generated proto/addressbook.proto";
        if (std::system(cmd.c_str()) != 0) {
            std::fprintf(stderr,
                         "protoc failed. Install protobuf and set PROTOC / PROTOBUF_DIR.\n");
            return 1;
        }
    }

    cco::Workspace ws(argc, argv);
    auto &app = ws.executable("protobuf_demo")
        .sources({"src/main.cpp", "generated/addressbook.pb.cc"})
        .include("generated")
        .include(prefix + "/include")
        .linkflag("-L" + prefix + "/lib")
        .linkflag("-lprotobuf")
        .linkflag("-pthread");
    (void)app;

    return ws.run();
}
