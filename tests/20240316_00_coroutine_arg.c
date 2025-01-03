#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
    //    printf("%s", raw);
    //    return;

    // 字符串中包含 "main_co done"
    char *str = "234\n"
                "result_sum = 700\n"
                "use time true\n"
                "async result is 25\n"
                "main co done\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}