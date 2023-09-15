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
//    printf("%s", raw);
//    return;
    char *str = "a sub\n"
                "a sub/libucontext.a\n"
                "a sub/stat.txt\n"
                "a sub/sub2\n"
                "a sub/sub2/stat.txt\n"
                "a nice.n\n"
                "a test.txt\n"
                "encode success\n"
                "x mockdir/sub/\n"
                "x mockdir/sub/libucontext.a\n"
                "x mockdir/sub/stat.txt\n"
                "x mockdir/sub/sub2/\n"
                "x mockdir/sub/sub2/stat.txt\n"
                "x mockdir/nice.n\n"
                "x mockdir/test.txt\n"
                "decode success\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}