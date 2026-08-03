#include "tests/test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "4455566666";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
