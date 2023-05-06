#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "11.200000hellotrue\n22.200000worldfalse\n");
}

int main(void) {
    TEST_BASIC
}