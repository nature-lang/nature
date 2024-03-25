#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "null\n"
                             "123\n"
                             "i8 area= 50\n"
                             "i8 area= 50\n"
                             "float area= 18.910000\n"
                             "float area= 18.910000\n");
}

int main(void) {
    TEST_BASIC
}