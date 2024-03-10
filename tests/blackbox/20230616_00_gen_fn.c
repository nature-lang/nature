#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    return;
    char *raw = exec_output();
    char *str = "-902\n"
                "999\n"
                "40.000000\n"
                "false\n"
                "true false\n"
                "true false\n"
                "144\n"
                "48\n"
                "66 77 5082\n"
                "286\n"
                "hello 12\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}