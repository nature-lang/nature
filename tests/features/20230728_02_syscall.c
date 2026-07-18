#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "mkdir directory: true\n"
                "rename success\n"
                "rename directory: true\n"
                "chmod directory: true\n"
                "rmdir success\n"
                "missing stat failed\n"
                "getpid success\n"
                "getppid success\n"
                "getcwd success\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
