#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

static void test_basic() {
    char *raw = exec_output();
    char *str = "err msg: index out of vec [24] with length 12\n"
                "err trace len: 2\n"
                "err trace first: 20230828_00_trace_error/foo/bar.n 20230828_00_trace_error.foo.bar.car 18 19\n"
                "\n"
                "---------------------------------------------------------\n"
                "\n"
                "catch error: 'crash and burn' at 20230828_00_trace_error/foo/bar.n:12:24\n"
                "stack backtrace:\n"
                "0:\t20230828_00_trace_error.foo.bar.nice\n"
                "\t\tat 20230828_00_trace_error/foo/bar.n:12:24\n"
                "1:\t20230828_00_trace_error.foo.bar.test\n"
                "\t\tat 20230828_00_trace_error/foo/bar.n:4:4\n"
                "2:\tmain\n"
                "\t\tat 20230828_00_trace_error/main.n:15:15\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}