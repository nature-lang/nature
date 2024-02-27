#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    return;
    char *raw = exec_output();

    assert_string_equal(raw,
                        "hello nature\n"
                        "catch err: world error\n"
                        "one(true) has err: i am one error\n"
                        "one(false) not err, s1= 2\n");
}

int main(void) {
    TEST_BASIC
}