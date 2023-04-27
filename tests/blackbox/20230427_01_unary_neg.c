#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "24-24-27-24-24-4716-16\n3.141593-3.141593-4.420000\n");
}

int main(void) {
    TEST_BASIC
}