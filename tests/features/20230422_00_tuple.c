#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "1naturetrue\n1hellotrue\n1hellotrue\n2worldfalse\n");
}

int main(void) {
    TEST_BASIC
}