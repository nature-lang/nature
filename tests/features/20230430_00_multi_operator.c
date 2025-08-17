#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "-875 0 70392.699075 210000 8 4\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}