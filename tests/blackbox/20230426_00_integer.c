#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "1234-1-2-3-4\n");
}

int main(void) {
    TEST_BASIC
}