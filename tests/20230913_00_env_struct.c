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

    char *str = "900.000000\n"
                "800.000000\n"
                "900.000000\n"
                "800.000000\n"
                "100.000000\n"
                "200.000000\n"
                "300.000000\n"
                "400.000000\n"
                "100.000000\n"
                "200.000000\n"
                "300.000000\n"
                "400.000000\n"
                "100.000000\n"
                "0.000000\n"
                "0.000000\n"
                "400.000000\n"
                "0.000000\n"
                "600.000000\n"
                "0.000000\n"
                "800.000000\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_BASIC
}