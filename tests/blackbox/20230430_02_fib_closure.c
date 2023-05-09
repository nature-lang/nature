#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "result=55,recursion_count=177\n");
}

int main(void) {
    TEST_BASIC
}