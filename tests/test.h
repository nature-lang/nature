#ifndef NATURE_TESTS_H
#define NATURE_TESTS_H

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "src/build/build.h"
#include "utils/exec.h"

#define assert_string_equal(_actual, _expect) (assertf(str_equal(_actual, _expect), "%s", _actual))
#define assert_int_equal(_actual, _expect) (assert((_actual) == (_expect)))

static inline char *exec_output() {
    return exec(BUILD_OUTPUT_DIR, BUILD_OUTPUT, slice_new());
}

static inline int blackbox_setup() {
    char *nature_root = getenv("NATURE_ROOT");
    assert(nature_root != NULL);


    // 从环境变量中读取 build entry
    char *entry = getenv("ENTRY_FILE");
    assert(entry && "entry file is null");

    build(entry);

    return 0;
}


#define TEST_BASIC \
    blackbox_setup();              \
    test_basic();\


#endif //NATURE_TEST_H
