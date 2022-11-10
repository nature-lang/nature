#include "test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "init, cash=1000\n"
                             "saved 10, cash=1010\n"
                             "saved 20, cash=1030\n");
}

int main(void) {
    TEST_BASIC
}