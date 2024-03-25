#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str = "st.mode: 16877\n"
                "rename success\n"
                "st2.mode: 16877\n"
                "st2.mode: 16868\n"
                "rmdir success\n"
                "stat err: No such file or directory\n"
                "getpid success\n"
                "getppid success\n"
                "getcwd success\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}