#include <stdio.h>

#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "c0=true\nc0=false\nc1=true\nc1=false\ntruefalse\n");
}

int main(void) {
    TEST_BASIC
}