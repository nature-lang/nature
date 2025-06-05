#include "test.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils/helper.h"

int setup() {
    printf("setup\n");
    return 0;
}

int teardown() {
    printf("teardown\n");
    return 0;
}

int main(void) {
    setup();
    test_basic();
    teardown();
}