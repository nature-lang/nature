#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw,
                        "null\n6\n6\n6\n7\nnull\n6\n8\n"
                        "truefalsefalse\n1\n00\nfalsetrue\ntrue\n");
}

int main(void) {
    TEST_BASIC
}