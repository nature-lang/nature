#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests/test.h"

static void test_basic() {
    char *raw = exec_output();
    char *str = "encode success\n"
                "decode success\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}