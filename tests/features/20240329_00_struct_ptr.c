#include <stdio.h>

#include "string.h"
#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "hualaka 23 true\n"
                "hualaka 23 true\n"
                "hualaka\n"
                "hualaka 23 true hualaka 30 true\n"
                "in test_self hualaka 30 true\n"
                "hualaka 32 true\n";
    //                "invalid memory address or nil pointer dereference\n"
    //                "panic: 'invalid memory address or nil pointer dereference' at 20240329_00_struct_ptr/main.n:33:5\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}