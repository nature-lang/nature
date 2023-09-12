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

    char *str = "1 china hualaka\n"
                "24\n"
                "wanglufei japan 2 19\n"
                "hualaka china 1 24 24\n"
                "in test fn, p.name is hualaka\n"
                "in test fn, p3.name is hualaka\n"
                "rewrite by p3 rewrite by p3\n"
                "55  !\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}