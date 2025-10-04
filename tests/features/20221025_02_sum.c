#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "foo=-28\nbar=-16";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}