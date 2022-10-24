#include "test.h"
#include <stdio.h>

static void test_basic() {
    char *output = getenv("FORCE_OUTPUT");
    printf("hello %s\n", output);
}

int main(void) {
    TEST_BASIC
}