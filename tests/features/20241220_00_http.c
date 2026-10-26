#include "tests/test.h"

static void test_basic(void) {
    char *output = exec_output();
    assert_string_equal(output, "hello nax\nserver is closed\n");
}

int main(void) {
    TEST_BASIC
}
