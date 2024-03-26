#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char* raw = exec_output();
    char* str =
        "1\n"
        "2\n"
        "divisor cannot zero\n"
        "1\n"
        "coroutine 'main' uncaught error: 'divisor cannot zero' at cases/20230506_01_throw.n:3:27\n"
        "stack backtrace:\n"
        "0:\t_main.rem_0\n"
        "\t\tat cases/20230506_01_throw.n:3:27\n"
        "1:\t_main\n"
        "\t\tat cases/20230506_01_throw.n:31:18\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}