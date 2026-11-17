#pragma once

#include <string>

#if defined(_WIN32)
#if defined(GREETER_BUILD_DLL)
#define GREETER_API __declspec(dllexport)
#else
#define GREETER_API __declspec(dllimport)
#endif
#else
#define GREETER_API
#endif

namespace greeter {
GREETER_API std::string greet(const std::string &name);
}
