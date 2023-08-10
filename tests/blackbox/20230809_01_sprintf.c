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
    char *str = "hello world, nice: 12, %, float: 3.141500\n"
                "err msg=this is failed, count=253 len= 33\n";

    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}