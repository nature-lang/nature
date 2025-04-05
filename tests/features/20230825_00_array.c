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

    char *str = "11 12\n"
                "index out of range [5] with length 5\n"
                "11 0 11\n"
                "100 12 0 11\n"
                "11 1111 3333\n"
                "1 2 3 11 12 13\n"
                "index out of range [5] with length 3\n"
                "0 0 0.000000 0 0 false\n"
                "789 987 3.141500 456 1 true\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}