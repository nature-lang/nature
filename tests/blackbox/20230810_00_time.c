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

    char *str = "51 36 8 10 7 123 4 221 0 0 UTC\n"
                "timestamp: 1660066252\n"
                "datetime: 2022-08-09 17:30:52\n"
                "true\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}