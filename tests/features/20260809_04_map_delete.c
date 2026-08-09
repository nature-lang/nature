#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "1 false true\n"
                "{\"b\":true}\n"
                "2 true false true\n"
                "{\"a\":1,\"c\":3}\n"
                "3 true true true\n"
                "{\"a\":1,\"c\":3,\"d\":4}\n"
                "3\n"
                "1 false true 20\n"
                "2 true true 30 20\n"
                "{\"q\":20,\"a\":30}\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
