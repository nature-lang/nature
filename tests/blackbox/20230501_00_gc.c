#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw,
                        "910111291827\n"
                        "163264128\n"
                        "hello world\n"
                        "9false64\n"
                        "helloworldnaturehaha\n"
                        "16\n");
}

int main(void) {
    TEST_BASIC
}