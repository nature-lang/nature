#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "hello world\n"
                "catch panic:  index out of range\n"
                "panic: 'in here' at cases/20241214_00_panic.n:18:17\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
