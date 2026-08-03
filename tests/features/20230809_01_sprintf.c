#include "tests/test.h"
#include "time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_basic() {
    char *raw = exec_output();
    //    printf("%s", raw);
    //    return;
    assert_string_equal(raw, "hello world, nice: 12, %, float: 3.141500\n"
                             "err msg=this is failed, count=253 len= 33\n"
                             "-12\n"
                             "12\n"
                             "|  -12|\n"
                             "|   12|\n"
                             "|-000000012|\n"
                             "|0000000012|\n");
}

int main(void) {
    TEST_BASIC
}
