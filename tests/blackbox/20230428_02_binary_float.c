#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "101.125000-101.230000100.125000-100.125000\n"
                             "-10025.01562539100.125000-100.125000\n"
                             "truefalsefalsefalsetruetrue100.125000-100.1250003.141593\n");
}

int main(void) {
    TEST_BASIC
}