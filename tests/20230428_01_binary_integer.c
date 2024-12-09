#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "101-101\n"
            "-100005\n"
            "244\n"
            "truefalsefalsefalsetruetrue\n"
            "100-10024\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
