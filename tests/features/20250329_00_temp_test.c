#include "tests/test.h"

static void test_basic(void) {
    int status = -1;
    char *output = exec_output_status(&status);
    assert_int_equal(status, 0);
    assert_string_equal(output, "hello world\n");
}

int main(void) {
    TEST_BASIC
}
