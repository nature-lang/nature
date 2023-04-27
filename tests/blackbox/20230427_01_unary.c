#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "24-24-27-24-24-4716-16");
}

int main(void) {
    TEST_BASIC
}