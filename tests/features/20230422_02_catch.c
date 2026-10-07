#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    int status = 0;
    char *raw = exec_output_status(&status);
    assert(status != 0);
    char *str = "hello nature\n"
                "catch err: hello error\n"
                "catch err: world error\n"
                "foo = 12\n"
                "foo = 233\n"
                "hello nature\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}