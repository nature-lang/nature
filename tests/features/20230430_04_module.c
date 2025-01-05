#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "config.max=10000\n"
                             "config.count=125\n"
                             "[test_math] max result=10000\n");
}

int main(void) {
    TEST_BASIC
}