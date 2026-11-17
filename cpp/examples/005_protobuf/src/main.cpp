#include "addressbook.pb.h"

#include <cstdio>
#include <string>

int main() {
    tutorial::Person person;
    person.set_name("cco");
    person.set_id(1);
    person.set_email("cco@example.com");

    std::string wire;
    if (!person.SerializeToString(&wire)) return 1;

    tutorial::Person parsed;
    if (!parsed.ParseFromString(wire)) return 1;

    std::printf("%s %d %s\n", parsed.name().c_str(), parsed.id(), parsed.email().c_str());
    return 0;
}
