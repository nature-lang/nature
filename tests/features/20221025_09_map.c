#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "a = 1\n"
                "3 33\n"
                "3\n"
                "2\n"
                "110\n"
                "panic: 'key 'b' not found in map' at cases/20221025_09_map.n:24:15\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}