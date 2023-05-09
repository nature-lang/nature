#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "2\n2\n3\n4\n8\n");
}

int main(void) {
    TEST_BASIC
}