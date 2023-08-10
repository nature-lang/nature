#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "or_1 is true\n"
                "entry test_true\n"
                "or_2 is true\n"
                "or_3 is false\n"
                "or_4 is true\n"
                "or_5 is true\n"
                "----------------\n"
                "entry test_false\n"
                "and_1 is false\n"
                "and_2 is false\n"
                "and_3 is false\n"
                "and_4 is true\n"
                "and_5 is false\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}