#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

static void test_basic() {
    // 添加环境变量
    char *runner_dir = path_join(WORKDIR, "tests/runner_path");

    WORKDIR = path_join(WORKDIR, "tests/mockdir");

    setenv("RUNNER_DIR", runner_dir, 1);

    slice_t *args = slice_new();
    slice_push(args, &"node");
    slice_push(args, &"-v");
    char *raw = exec_with_args(args);

    printf("%s", raw);
//    char *str = "";
//    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}