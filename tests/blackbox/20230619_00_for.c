#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "33\n2500\n");
}

int main(void) {
    TEST_BASIC
}