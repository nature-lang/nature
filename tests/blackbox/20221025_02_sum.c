#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "foo=-28\nbar=-16");
}

int main(void) {
    TEST_BASIC
}