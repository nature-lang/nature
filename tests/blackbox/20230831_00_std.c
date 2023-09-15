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
    char *str = "/test/hello/world/nice.tar.gz\n"
                "/test/hello/world/nice.tar.gz\n"
                "/test/hello/world/nice.tar.gz\n"
                "sub/sub2\n"
                "dirent_t size 268\n"
                "os.listdir of mock len 4\n"
                "item:  test3.txt\n"
                "item:  file\n"
                "item:  stat.txt\n"
                "item:  dir1\n"
                "/test\n"
                "/test/hello\n"
                "/\n"
                ".\n"
                ".\n"
                "true\n"
                "true\n"
                "false\n"
                "mock/file elf arch is amd64\n"
                "path.base----------------------\n"
                "hello\n"
                "hello\n"
                "hello\n"
                "test\n"
                "test\n"
                ".\n"
                "/\n"
                "/\n"
                "!test str!hello";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}