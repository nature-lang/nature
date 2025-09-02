#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
//    printf("%s", raw);
//    return;
    char *str = "1 3 9 5\n"
                "5\n"
                "5\n"
                "5\n"
                "self is 1314\n"
                "self is 10000\n"
                "10012 10011\n"
                "12 1 3\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}