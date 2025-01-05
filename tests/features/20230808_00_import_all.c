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


    char *str = "i am log\n"
                "cond is true\n"
                "cond is false\n"
                "1 2\n"
                "i am pool.log\n"
                "1 2\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}