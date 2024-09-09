#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    printf("%s", raw);
    char *str = "child process will run\n"
                "first exec err: No such file or directory\n"
                "hello world\n"
                "start child process complete, will exit\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}