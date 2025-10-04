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
    char *str = "hello world\n"
                "101\n"
                "104 101 108 108 111 32 119 111 114 108 100 \n"
                "0 104\n"
                "1 101\n"
                "2 108\n"
                "3 108\n"
                "4 111\n"
                "5 32\n"
                "6 119\n"
                "7 111\n"
                "8 114\n"
                "9 108\n"
                "10 100\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}