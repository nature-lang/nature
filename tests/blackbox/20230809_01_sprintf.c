#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include "time.h"

static void test_basic() {
    time_t rawtime;
    char time_buf[100];
    time(&rawtime);
    struct tm *timeinfo = localtime(&rawtime);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", timeinfo);

    char *raw = exec_output();
    char actual[4096] = {0};
    memmove(actual, raw, 4096);

    char expect[4096] = {0};
    sprintf(expect, "hello world, nice: 12, %%, float: 3.141500\n"
                    "err msg=this is failed, count=253 len= 33\n"
                    "err msg=time in, %s, count=253\n", time_buf);

    int res = memcmp(expect, actual, 4096);
    assert_true(res == 0);
}

int main(void) {
    TEST_BASIC
}