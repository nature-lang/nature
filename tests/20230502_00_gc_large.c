#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    // 对 raw 按 \n 分割，并转化成数组
    int64_t *number = take_numbers(raw, 5);
    assert_int_equal(number[0], 380);
    assert_int_equal(number[1], 2000);

    // vec_grow: 8+16+32+64+128+256+512+1024+2048+4096+8192 才能承载 5000 个数组元素
    // 每个数组元素 8byte, 一共是 128k 大小
    assert_true(number[2] > 100000 && number[2] < 200000); // 131520
    assert_true(number[3] > 100 && number[3] < 1000);      // 688
    assert_int_equal(number[4], 3);
}

int main(void) {
    TEST_BASIC
}