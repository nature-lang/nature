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

    char *str = "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 4096\n"
                "gz write success, len: 524\n"
                "gz success full\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}