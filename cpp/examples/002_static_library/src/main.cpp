#include "greeter/greeter.h"

#include <cstdio>

int main() {
    std::printf("%s\n", greeter::greet("cco").c_str());
    return 0;
}
