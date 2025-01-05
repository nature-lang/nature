#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();

    // 字符串中包含 "main_co done"
    char *find = strstr(raw, "main_co done");

    assert_true(find);
}

int main(void) {
    TEST_BASIC
}