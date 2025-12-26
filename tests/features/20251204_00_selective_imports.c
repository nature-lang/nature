#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    char *expected = "20\n"
                     "10\n";

    assert_string_equal(raw, expected);
}

int main(void) {
    TEST_BASIC
}
