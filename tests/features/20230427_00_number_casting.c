#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "-10-10\n"
            "10001000\n"
            "1000-24\n"
            "-12818446744073709551488\n"
            "128128\n"
            "2555555 -349 -349 65187 65187\n"
            "3.1415933.1415933.1415933.141593\n"
            "-3.141593-3.141593-3.141593\n"
            "-3.141593 -3.141593 -3 4294967293 -3 4294967296.000000\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
