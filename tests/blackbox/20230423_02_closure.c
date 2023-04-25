#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "900\n800\n900\n800\n");
}

int main(void) {
    TEST_BASIC
}