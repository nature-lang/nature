#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "wait status: 0\n"
                "kill success\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}