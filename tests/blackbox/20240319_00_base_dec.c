#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "66166\n"
                "725202 -74558 120 0.083333\n"
                "291 83 21 233 292 499 6\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}