#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();

    // 字符串中包含 "main_co done"
    char *str = "1 2 4 8 4 8\n"
                "418063755 418063755\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}