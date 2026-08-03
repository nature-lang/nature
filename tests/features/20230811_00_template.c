#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    char *str = "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 524\n"
                "gz success full\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
