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
    char *str = "index of wo is  6\n"
                "index of wow is  -1\n"
                "index after 7 of wo is  12\n"
                "split len: 3\n"
                "hello\n"
                "world\n"
                "world\n"
                "split he\\noll len: 6\n"
                "he\n"
                "oll!!!\n"
                "join result: nice-to-meet-you !!!\n"
                "hell_test.exe ends with .exe?: true\n"
                "hell_test.exe ends with .elf?: false\n"
                "nice to meet you starts with nice?: true\n"
                "nice to meet you starts with where?: false\n"
                "nice to meet you starts with empty?: true\n"
                "ltrim + rtrim result: hello world !!!\n"
                "trim result: hello world !!!\n"
                "nicg to mggt you\n"
                "hualaka world hualaka nice to meet you hualaka\n"
                " world  nice to meet you \n"
                "helloworld reverse:  dlrowolleh\n"
                "rfind wo: 12\n"
                "rfind wo: 6\n"
                "rfind wo: 14\n";
    assert_string_equal(raw, str);
}

int main(void) {
    TEST_WITH_PACKAGE
}