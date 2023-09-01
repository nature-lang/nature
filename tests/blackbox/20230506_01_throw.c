#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "1\n"
                             "2\n"
                             "divisor cannot zero\n"
                             "catch error: 'divisor cannot zero' at cases/20230506_01_throw.n:3:27\n"
                             "stack backtrace:\n"
                             "0:\tmain.rem_0\n"
                             "\t\tat cases/20230506_01_throw.n:3:27\n"
                             "1:\tmain\n"
                             "\t\tat cases/20230506_01_throw.n:35:18\n");
}

int main(void) {
    TEST_BASIC
}