#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expect = "17 1 2 1.500000 true nature 42\n"
                   "22 12 34 3 2\n"
                   "true true true\n"
                   "1 21 20 68 4 -17 -18\n"
                   "false true true\n"
                   "40\n"
                   "mid\n"
                   "33\n"
                   "5\n"
                   "3 610\n"
                   "9 3\n"
                   "3 10 13\n"
                   "1 30 42\n"
                   "11\n"
                   "5 3 101 44 7.000000\n"
                   "1 true\n"
                   "-1 0 1\n"
                   "42\n"
                   "12\n";
    assert_string_equal(raw, expect);
}

int main(void) {
    TEST_BASIC
}
