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
    char *str = "in closure fn\n"
                "fut.await catch err: main.CLOSURE_ERROR\n"
                "dive catch err: main.DIVISOR_ZERO\n"
                "div result 10\n"
                "coroutine 3 uncaught error: main.DIVISOR_ZERO at cases/20240318_00_coroutine_error.n:9:26\n"
                "stack backtrace:\n"
                "0:\tmain.div\n"
                "\t\tat cases/20240318_00_coroutine_error.n:9:26\n"
                "1:\tmain.main\n"
                "\t\tat cases/20240318_00_coroutine_error.n:37:11\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}