#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expected = "1\n"
                     "0\n"
                     "1\n"
                     "0\n"
                     "1\n"
                     "word\n"
                     "2\n"
                     "1\n"
                     "2\n";
    assert_string_equal(raw, expected);
}

int main(void) {
    TEST_BASIC
}
