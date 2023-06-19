#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "25\n26.000000\n6.200000\n15\n6.900000\n40\n");
}

int main(void) {
    TEST_BASIC
}