#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
//    printf("%s", raw);
    char *str = "hello world\n"
                "0xa000200f30\n"
                "32\n"
                "32\n"
                "25\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}