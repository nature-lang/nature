#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expected = "i16: 0\n"
                     "i32: 0\n"
                     "i64: 0\n"
                     "struct: 0\n";
    assert_string_equal(raw, expected);
}

int main(void) {
    TEST_BASIC
}
