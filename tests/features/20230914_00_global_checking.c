#include "tests/test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void test_basic() {
    char *raw = exec_output();

    char *str = "12 12\n"
                "hello world\n"
                "3.141500\n"
                "1\n"
                "5\n"
                "7\n"
                "9\n"
                "2\n"
                "true\n"
                "false\n"
                "false\n"
                "true\n"
                "false\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
