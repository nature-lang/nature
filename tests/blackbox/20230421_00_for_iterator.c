#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "36\n88\na\nb\nc\n6\n");
}

int main(void) {
    TEST_BASIC
}