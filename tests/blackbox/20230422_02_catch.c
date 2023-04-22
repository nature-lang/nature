#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw,
                        "hello nature\ncatch err: world error\none(true) has err:i'm one error\none(false) not err, s1=1\n");
}

int main(void) {
    TEST_BASIC
}