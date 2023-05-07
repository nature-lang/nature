#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "The user has already registered\nxiaoyou-nanana456\ncurrent user count=2\n");
}

int main(void) {
    TEST_BASIC
}