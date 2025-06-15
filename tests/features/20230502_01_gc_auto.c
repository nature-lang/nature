#include <stdio.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    printf("%s", raw);
    // 对 raw 按 \n 分割，并转化成数组
    int64_t *number = take_numbers(raw, 3);
    assert_true(number[0] > 200000 && number[0] < 300000);
    assert_int_equal(number[1], 9999);
    assertf(number[2] > 100000 && number[2] < 200000, "actual: %d", number[2]);
}

int main(void) {
    TEST_BASIC
}