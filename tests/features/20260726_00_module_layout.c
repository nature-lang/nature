#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();

    char *str = "root:modtest other\n"
                "enc-6 11\n"
                "codec.helper\n"
                "enc-4|d-e-c\n"
                "fmtx fmtx.utils\n"
                "req:http/1.1 resp\n"
                "modtest.same\n"
                "modtest.plat variant\n"
                "enc-10/fmtx\n"
                "v7 7\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}
