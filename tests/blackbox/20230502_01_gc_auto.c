#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    // 对 raw 按 \n 分割，并转化成数组
    int64_t *number = take_numbers(raw, 2);
    assert_int_equal(number[0], 9999);
    assert(number[1] > 100000 && number[1] < 200000);
}

int main(void) {
    TEST_BASIC
}