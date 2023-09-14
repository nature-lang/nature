#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
//    printf("%s", raw);
//    return;
    assert_string_equal(raw, "null\n6\n6\n6\n7\nnull\n6\n8\n"
                             "truefalsefalse\n1\n00\nfalsetrue\n");
}

int main(void) {
    TEST_BASIC
}