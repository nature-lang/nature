#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "hello world\n"
                "nlelo world\n"
                "hello world one piece\n"
                "hello world one piece nice\n"
                "true\n"
                "false\n"
                "true\n"
                "false\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}