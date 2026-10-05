#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str =
            "catch vec index err=index out of range\n"
            "0\n"
            "catch type assert err=type assert failed\n"
            "22\n"
            "chain access vec err=index out of range\n"
            "chain access call err=divisor cannot zero\n"
            "hello world\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}