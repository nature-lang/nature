#include "test.h"
#include <stdio.h>

int setup() {
    printf("setup\n");
    return 0;
}

int teardown() {
    printf("teardown\n");
    return 0;
}

static void test_hello() {
    printf("hello\n");
}

int main(void) {
    setup();
    test_hello();
    teardown();
}