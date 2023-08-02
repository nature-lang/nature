#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "write bytes: 11\n"
                "read len: 11, data: hello world\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}