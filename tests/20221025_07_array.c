#include "test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "12225\n");
}

int main(void) {
    TEST_BASIC
}