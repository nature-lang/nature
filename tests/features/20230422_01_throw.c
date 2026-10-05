#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    int status = 0;
    char *raw = exec_output_status(&status);
    assert(status != 0);
    char *str =
            "hello nature\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}