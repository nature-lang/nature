#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

//    char *str = "";
//    assert_string_equal(raw, expect);
    printf("%s", raw);
}

int main(void) {
    TEST_BASIC
}