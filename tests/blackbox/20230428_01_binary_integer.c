#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "101-101\n-100005\n244\ntruefalsefalsefalsetruetrue\n");
}

int main(void) {
    TEST_BASIC
}