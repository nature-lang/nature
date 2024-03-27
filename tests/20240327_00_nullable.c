#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    return;
    char *raw = exec_output();
    char *find = strstr(raw, "main_co done");

    assert_true(find);
}

int main(void) {
    TEST_BASIC
}