#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    char *str =
            "open mock/notfound.txt failed: No such file or directory\n"
            "open mock/open.txt successful\n"
            "actual read len: 20\n"
            "buf as string: hello world\n"
            "nature i\n"
            "second read len: 31, buf: s the best programming language\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}