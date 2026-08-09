#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expected = "issue: 1 42\n"
                     "field: 1 7\n"
                     "method: 1\n"
                     "copy: 0\n";
    assert_string_equal(raw, expected);
}

int main(void) {
    TEST_BASIC
}
