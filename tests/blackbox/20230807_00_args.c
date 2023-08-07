#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

static void test_basic() {
    slice_t *args = slice_new();
    slice_push(args, &"arg1");
    slice_push(args, &"-arg2");
    slice_push(args, &"--hualaka");
    slice_push(args, &"nice");
    char *raw = exec_with_args(args);


    char *str = "arg1\n"
                "-arg2\n"
                "--hualaka\n"
                "nice\n";

    str = str_connect_by(BUILD_OUTPUT, str, "\n");
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}