#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>

static void test_basic() {
    char *raw = exec_output();
//    char *str = "";
//    assert_string_equal(raw, str);
    printf("%s", raw);
}

int main(void) {
    TEST_BASIC
}