#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    return;
    char *raw = exec_output();

    assert_string_equal(raw, "1 2 13\n"
                             "3 4 12\n"
                             "1\n"
                             "8\n"
                             "1025\n"
                             "2048\n"
                             "1\n"
                             "2048\n"
                             "1 22 25\n"
                             "bcd\n"
                             "abcdefgh");
}

int main(void) {
    TEST_BASIC
}