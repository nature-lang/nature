#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "hello nature\n"
                "catch err: main.HELLO_ERROR\n"
                "catch err: main.WORLD_ERROR\n"
                "foo = 12\n"
                "foo = 233\n"
                "hello nature\n"
                "coroutine 'main' uncaught error: main.WRAPPED_ERROR at cases/20230422_02_catch.n:38:27\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}