#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "hello nature\n"
                "catch err: hello error\n"
                "catch err: world error\n"
                "foo = 12\n"
                "foo = 233\n"
                "hello nature\n"
                "coroutine 'main' uncaught error: '??hello error' at cases/20230422_02_catch.n:34:30\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}