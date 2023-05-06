#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "1\n2\n"
                             "divisor cannot zero\n"
                             "runtime catch error: divisor cannot zero\n");
}

int main(void) {
    TEST_BASIC
}