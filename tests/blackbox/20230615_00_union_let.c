#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "18\n36\nfalsefalsetrue\n3\n");
}

int main(void) {
    TEST_BASIC
}