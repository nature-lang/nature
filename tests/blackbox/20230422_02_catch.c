#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw,
                        "hello nature\ncatch err: world error\none(true) has err:i am one error\none(false) not err, s1=2\n");
}

int main(void) {
    TEST_BASIC
}