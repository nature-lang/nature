#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "-875070392.69907521000084\n");
}

int main(void) {
    TEST_BASIC
}