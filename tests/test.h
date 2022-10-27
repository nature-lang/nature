#ifndef NATURE_TESTS_H
#define NATURE_TESTS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "src/build/build.h"


static inline int stub_setup(void **state) {
    char* nature_root = getenv("NATURE_ROOT");
    assert(nature_root != NULL);


    // 从环境变量中读取 build entry
    char *entry = getenv("ENTRY_FILE");
    assert(entry && "entry file is null");

    build(entry);

    return 0;
}

static inline int stub_teardown() {
    return 0;
}

#define TEST_BASIC \
    const struct CMUnitTest tests[] = {\
        cmocka_unit_test(test_basic),\
    };\
    return cmocka_run_group_tests(tests, stub_setup, stub_teardown);


#endif //NATURE_TEST_H
