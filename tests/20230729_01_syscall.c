#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "hello world!buf len: 100, buf cap: 100\n"
                "read result: 11\n"
                "buf: hello world!!!\n"
                "sec: true nsec: true\n"
                "env: hello man\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}