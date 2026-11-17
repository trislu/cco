#include <stdio.h>
#include <string.h>
#include "greeter/greeter.h"

int main(void) {
    if (strcmp(greeter_message(), "hello, cco C") != 0) {
        fprintf(stderr, "unexpected message\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}
