#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char* raw = exec_output();
    char* str =
        "1\n"
        "2\n"
        "main.DIVISOR_ZERO\n"
        "1\n"
        "coroutine 'main' uncaught error: main.DIVISOR_ZERO at cases/20230506_01_throw.n:5:20\n"
        "stack backtrace:\n"
        "0:\tmain.rem\n"
        "\t\tat cases/20230506_01_throw.n:5:20\n"
        "1:\tmain.main\n"
        "\t\tat cases/20230506_01_throw.n:34:22\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}