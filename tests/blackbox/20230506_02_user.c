#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "The user xiaoyou has already registered\n"
                             "xiaoyou - nanana456\n"
                             "current user count= 2\n");
}

int main(void) {
    TEST_BASIC
}