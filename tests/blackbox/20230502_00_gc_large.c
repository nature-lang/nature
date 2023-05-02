#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    // 对 raw 按 \n 分割，并转化成数组
    int64_t *number = take_numbers(raw, 4);
    assert_int_equal(number[0], 4999);
    assert_true(number[1] > 50000 && number[1] < 100000); // 65568
    assert_true(number[2] > 100 && number[2] < 500); // 304
    assert_int_equal(number[3], 3);
}

int main(void) {
    TEST_BASIC
}