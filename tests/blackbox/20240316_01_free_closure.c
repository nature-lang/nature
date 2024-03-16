#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();

    // 字符串中包含 "main_co done"
    char *str = "1111 1111\n"
                "1111 2222 3333\n"
                "in third 4 44440 44441 44442\n"
                "in third 3 44440\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}