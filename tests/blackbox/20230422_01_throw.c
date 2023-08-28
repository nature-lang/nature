#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "hello nature\n"
                             "catch error: 'world error' at cases/20230422_01_throw.n:3:22\n"
                             "stack backtrace:\n"
                             "main\n"
                             "\tat cases/20230422_01_throw.n:6:1\n");
}

int main(void) {
    TEST_BASIC
}