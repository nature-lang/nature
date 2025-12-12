#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "The user xiaoyou has already registered\n"
                "xiaoyou - nanana456\n"
                "current user count= 2\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}