#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
//    printf("%s", raw);
//    return;
    char *str = "1 2 13\n"
                "3 4 12\n"
                "1\n"
                "8\n"
                "1025\n"
                "2048\n"
                "1\n"
                "2048\n"
                "1 22 25\n"
                "bcd\n"
                "abcdefgh";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}