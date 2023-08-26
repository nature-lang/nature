#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    assert_string_equal(raw, "catch list index err=index out of list [5] with length 3\n"
                             "0\n"
                             "catch type assert err=type assert error\n"
                             "22\n"
                             "chain access list err=index out of list [8] with length 5\n"
                             "chain access call err=divisor cannot zero\n"
                             "hello world\n");
}

int main(void) {
    TEST_BASIC
}