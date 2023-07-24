#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();

    assert_string_equal(raw, "this is other\n"
                             "this is pool\n"
                             "this is seed\n"
                             "rand.dump() -> 12\n"
                             "this is seed\n"
                             "this is other\n"
                             "this is pool\n"
                             "this is seed\n"
                             "this is pool\n"
                             "hello world syscall\n"
                             "this is other\n"
                             "this is pool\n"
                             "this is seed\n"
                             "this is other\n"
                             "this is pool\n"
                             "this is seed\n"
                             "this is pool\n"
                             "hello world syscall\n"
                             "hello os\n"
                             "local.get_count() -> 555\n"
                             "this is other\n"
                             "hello local\n");
}

int main(void) {
    TEST_WITH_PACKAGE
}