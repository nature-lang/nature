#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    int status = 0;
    char *raw = exec_output_status(&status);
    assert(status != 0);
    //    printf("%s", raw);
    //    return;

    // 字符串中包含 "main_co done"
    char *str = "in closure fn\n"
                "fut.await catch err: in closure err\n"
                "dive catch err: divisor cannot be 0\n"
                "div result 10\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}