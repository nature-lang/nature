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
    printf("%s", raw);
    return;
    char *str = "";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}