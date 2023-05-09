#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "foo.a = 22\nfoo.b = true\nfoo.c = 11\n");
}

int main(void) {
    TEST_BASIC
}