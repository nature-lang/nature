#include "tests/test.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    // 对 raw 按 \n 分割，并转化成数组
    int64_t *number = take_numbers(raw, 9);
    assert(number[0] > 100000 && number[0] < 200000); // 144320
    assert_int_equal(number[1], 990);
    assert_int_equal(number[2], 900);
    assert_int_equal(number[3], 800);

    assert(number[4] > 100000 && number[4] < 200000); // 144320
    assert_int_equal(number[5], 980);
    assert_int_equal(number[6], 800);
    assert_int_equal(number[7], 600);
    assert(number[8] < 200);
}

int main(void) {
    TEST_BASIC
}