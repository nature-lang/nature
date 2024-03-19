#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "foo.a = 22\n"
                "foo.b = true\n"
                "foo.c = 11\n"
                "foo.list[0] = 1\n"
                "foo.list[1] = 2\n"
                "foo.list[2] = 3\n"
                "foo.list[3] = 4\n"
                "foo.list[4] = 5\n"
                "3.550000\n"
                "3.625000\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}