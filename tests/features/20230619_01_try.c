#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str =
        "catch vec index err=true\n"
        "0\n"
        "catch type assert err=true\n"
        "22\n"
        "chain access vec err=true\n"
        "chain access call err=main.DIVISOR_ZERO\n"
        "hello world\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}