#include <stdio.h>
#include "cmd/build.h"

int main(int argc, char *argv[]) {
    binary_path = argv[0];
    build("test/main.n");
    printf("Hello, World!%d, %s\n", argc, argv[0]);
    return 0;
}
