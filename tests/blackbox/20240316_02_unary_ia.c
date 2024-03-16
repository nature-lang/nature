#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "12 0x4000200f58 0x4000200f58 12\n"
                "1.200000 666.599976 3.400000 0x4000200f10 0x4000200f10 1.200000 666.599976 3.400000\n"
                "1 2 3 0x4000200fa8 0x4000200fa8 1 666 3\n"
                "100000 50000 25000 12500\n"
                "20 John false 0x4000200f78 0x4000200f78 30 John true\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}