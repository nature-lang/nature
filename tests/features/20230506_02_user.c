#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "20230506_02_user.user.ALREADY_REGISTERED\n"
                "xiaoyou - nanana456\n"
                "current user count= 2\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}