#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "hello nature\n"
                "catch error: 'world error' at cases/20230422_01_throw.n:3:21\n"
                "stack backtrace:\n"
                "0:\tmain.hello_0\n"
                "\t\tat cases/20230422_01_throw.n:3:21\n"
                "1:\tmain\n"
                "\t\tat cases/20230422_01_throw.n:6:0\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}