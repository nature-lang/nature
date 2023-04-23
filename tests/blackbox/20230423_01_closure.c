#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "timestamp=1682261163\ntimestamp=1682262975\ntimestamp=1682263024\n");
}

int main(void) {
    TEST_BASIC
}