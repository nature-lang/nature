#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "this is foo(i8 a)\n"
                             "this is foo(i8 a, f64 b)\n"
                             "this is foo(i16 a)\n"
                             "this is foo()\n");
}

int main(void) {
    TEST_BASIC
}