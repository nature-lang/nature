#ifndef NATURE_TESTS_H
#define NATURE_TESTS_H

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/build/build.h"
#include "utils/exec.h"

#define assert_string_equal(_actual, _expect) (assertf(str_equal(_actual, _expect), "%s", _actual))
#define assert_int_equal(_actual, _expect) (assertf((_actual) == (_expect), "%d", _actual))
#define assert_true(_expr) (assertf(_expr, "not true"))

#define PACKAGE_SYNC_COMMAND " sync -v"

static inline void exec_no_output(slice_t *args) {
    exec_process(WORKDIR, BUILD_OUTPUT, args);
}

static inline int exec_imm_param() {
    return exec_imm(WORKDIR, BUILD_OUTPUT, slice_new());
}

static inline char *exec_output() {
    return exec(WORKDIR, BUILD_OUTPUT, slice_new());
}

static inline char *exec_with_args(slice_t *args) {
    return exec(WORKDIR, BUILD_OUTPUT, args);
}

static inline int blackbox_setup() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = getenv("ENTRY_FILE");
    assert_true(entry && "entry file is null");

    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));

    build(entry, false);

    return 0;
}

static inline void blackbox_package_sync() {
    // 环境变量下查找 package 可执行文件 npkg
    char *workdir = get_workdir();
    char *npkg_path = getenv("NPKG_PATH");
    char *output = command_output(workdir, str_connect(npkg_path, PACKAGE_SYNC_COMMAND));
    log_debug("npkg sync:%s", output);
}

#define TEST_EXEC_IMM   \
    blackbox_setup(); \
    exec_imm_param();

#define TEST_BASIC    \
    blackbox_setup(); \
    test_basic();

#define TEST_WITH_PACKAGE    \
    blackbox_package_sync(); \
    blackbox_setup();        \
    test_basic();

#endif // NATURE_TEST_H
