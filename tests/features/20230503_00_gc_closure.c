#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    printf("%s", raw);
}

int main(void) {
    TEST_BASIC
}