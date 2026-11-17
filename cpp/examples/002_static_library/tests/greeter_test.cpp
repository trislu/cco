#include "greeter/greeter.h"

#include <cstdio>

int main() {
    if (greeter::greet("cco") != "hello, cco") {
        std::printf("FAIL: greeter::greet\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
