#include "test.h"
#include <stdio.h>

int setup(void **state) {
    printf("setup\n");
    return 0;
}

int teardown(void **state) {
    printf("teardown\n");
    return 0;
}

static void test_hello() {
    printf("hello\n");
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_hello),
    };
    return cmocka_run_group_tests(tests, setup, teardown);
}