#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "true false\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
