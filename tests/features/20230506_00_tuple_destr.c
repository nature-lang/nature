#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "1 1.200000 hello true\n2 2.200000 world false\n");
}

int main(void) {
    TEST_BASIC
}