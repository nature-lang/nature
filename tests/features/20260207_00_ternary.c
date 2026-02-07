#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "yes\n"
                "no\n"
                "1\n"
                "2\n"
                "10\n"
                "5\n"
                "B\n"
                "A\n"
                "D\n"
                "got true\n"
                "got false\n"
                "is null\n"
                "not null\n"
                "10\n"
                "called get_one\n"
                "1\n"
                "called get_two\n"
                "2\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
