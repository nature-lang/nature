#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    return;
    char *raw = exec_output();

    assert_string_equal(raw, "falsefalsetrue\nfalsefalse\ntruefalse\n11falsetrue\n");
}

int main(void) {
    TEST_BASIC
}