#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str =
        "hello nax\n"
        "coroutine 'main' uncaught error: main.WORLD_ERROR at cases/20230422_01_throw.n:5:21\n"
        "stack backtrace:\n"
        "0:\tmain.hello\n"
        "\t\tat cases/20230422_01_throw.n:5:21\n"
        "1:\tmain.main\n"
        "\t\tat cases/20230422_01_throw.n:9:10\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}