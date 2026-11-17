#include "greeter/greeter.h"

namespace greeter {
std::string greet(const std::string &name) {
    return "hello from shared library, " + name;
}
} // namespace greeter
