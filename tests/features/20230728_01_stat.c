#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *expect = "stat regular: true\n"
                   "stat size positive: true\n"
                   "stat link count positive: true\n"
                   "fstat regular: true\n"
                   "stat and fstat size match: true\n"
                   "missing stat failed\n";
    assert_string_equal(raw, expect);
}

int main(void) {
    TEST_BASIC
}
