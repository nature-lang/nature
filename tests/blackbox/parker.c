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
    char *noded_path = path_join(WORKDIR, "noded");

    int res = remove(noded_path);
//    assert_true(res == 0);

    setenv("RUNNER_PATH", runner_path, 1);
//    setenv("PARKER_VERBOSE", "true", 1);
    setenv("REPEAT_COUNT", "5", 1); // 5 次可以触发 gc, 确认是否有错误

    slice_t *args = slice_new();
    slice_push(args, &"node");

    exec_with_args(args); // exec parker with args

    char *raw = exec(WORKDIR, noded_path, slice_new());
//    printf("%s", raw);
//    return
    char *str = "read env count success: 5\n"
                "0 hello world\n"
                "1 hello world\n"
                "2 hello world\n"
                "3 hello world\n"
                "4 hello world\n";
    assert_string_equal(raw, str);
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
    char *entry = "parker.n";

    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));
    strcpy(BUILD_OUTPUT_NAME, "main");

    build(entry);

    printf("parker %s build successful", BUILD_OUTPUT_DIR);
}

int main(void) {
    blackbox_package_sync();

    build_test_node();
    build_runner();
    build_parker();

    test_basic();
}