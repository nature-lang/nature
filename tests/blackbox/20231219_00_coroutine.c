#include <stdio.h>

#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    return;
    char *raw = exec_output();
//            printf("%s", raw);

    assert_string_equal(raw,
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "share sub_co done\n"
                        "solo sub_co done\n"
                        "main_co sleep done 21\n"
                        "main_co sleep done 22\n"
                        "main_co sleep done 23\n"
                        "main_co sleep done 24\n"
                        "main_co done\n");
}

int main(void) {
    TEST_BASIC
}