#include "tests/test.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

static void test_basic() {
    // 添加环境变量
    char *runner_path = path_join(WORKDIR, "tests/runnerdir/runner");

    WORKDIR = path_join(WORKDIR, "tests/mockdir");

    setenv("RUNNER_PATH", runner_path, 1);
    setenv("PARKER_VERBOSE", "true", 1);
    setenv("REPEAT_COUNT", "true", 3);

    slice_t *args = slice_new();
    slice_push(args, &"node");

    char *raw = exec_with_args(args); // exec parker with args

//    printf("%s", raw);
//    char *str = "";
//    assert_string_equal(raw, str);
}

static void build_test_node() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    char *entry = "tests/mockdir/node.n";
    char *workdir = get_workdir();

    strcpy(BUILD_OUTPUT_DIR, path_join(workdir, "tests/mockdir"));
    strcpy(BUILD_OUTPUT_NAME, "node");

    build(entry);
    printf("node %s build successful", BUILD_OUTPUT_DIR);
}

static void build_runner() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = "runner.n";
    char *workdir = get_workdir();

    strcpy(BUILD_OUTPUT_DIR, path_join(workdir, "tests/runnerdir"));
    strcpy(BUILD_OUTPUT_NAME, "runner");

    build(entry);
    printf("runner %s build successful", BUILD_OUTPUT_DIR);
}

static void build_parker() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = "main.n";
    assert_true(entry && "entry file is null");

    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));
    strcpy(BUILD_OUTPUT_NAME, "main");

    build(entry);

    printf("parker %s build successful", BUILD_OUTPUT_DIR);
}

int main(void) {
    blackbox_package_sync();

//    build_test_node();
    build_runner();
//    build_parker();

//    test_basic();
}