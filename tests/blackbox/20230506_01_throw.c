#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "1\n"
                             "2\n"
                             "divisor cannot zero\n"
                             "catch error: 'divisor cannot zero' at cases/20230506_01_throw.n:3:28\n"
                             "stack backtrace:\n"
                             "main\n"
                             "\tat cases/20230506_01_throw.n:35:19\n");
}

int main(void) {
    TEST_BASIC
}