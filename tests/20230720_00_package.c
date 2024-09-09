#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    char *str;
    // 根据当前的编译平台进行处理
    if (BUILD_OS == OS_DARWIN) {
        str = "util.sum(1, 2) = 3\n"
              "this is other\n"
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
              "local.get_count() -> 333\n"
              "this is other\n"
              "hello local in cross darwin\n";

    } else if (BUILD_OS == OS_LINUX) {
        str = "util.sum(1, 2) = 3\n"
              "this is other\n"
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
              "local.get_count() -> 444\n"
              "this is other\n"
              "hello local in cross linux amd64\n";

    }


    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}