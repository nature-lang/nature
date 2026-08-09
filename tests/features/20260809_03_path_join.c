#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expected = "/tmp/base\n"
                     "/tmp/base/a\n"
                     "/tmp/base/child\n"
                     ".\n"
                     "/\n";
    assert_string_equal(raw, expected);
}

int main(void) {
    TEST_BASIC
}
