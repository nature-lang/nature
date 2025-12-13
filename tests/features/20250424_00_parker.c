#include "tests/test.h"

static void test_run() {
    // 添加环境变量
    char *runner_path = path_join(WORKDIR, "tests/runnerdir/runner");
    WORKDIR = path_join(WORKDIR, "tests/mockdir");
    char *noded_path = path_join(WORKDIR, "node-c");
    remove(noded_path);

    setenv("RUNNER_PATH", runner_path, 1);
    //    setenv("PARKER_VERBOSE", "true", 1);

    slice_t *args = slice_new();
    slice_push(args, &"node");
    // run parker
    exec_with_args(args);

    setenv("REPEAT_COUNT", "5", 1); // 5 次可以触发 gc, 确认是否有错误
    int pid = 0;
    int status = 0;
    char *actual = exec(WORKDIR, noded_path, slice_new(), &pid, &status);
    char *expect = "read env count success: 5\n"
                   "0 hello nature\n"
                   "1 hello nature\n"
                   "2 hello nature\n"
                   "3 hello nature\n"
                   "4 hello nature\n";
    printf("actual: %s", actual);
    assert_string_equal(actual, expect);
}

static void build_test_node() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    char *entry = "tests/mockdir/node.n";
    char *workdir = get_workdir();

    strcpy(BUILD_OUTPUT_DIR, path_join(workdir, "tests/mockdir"));
    strcpy(BUILD_OUTPUT_NAME, "node");

    build(entry, false);
    printf("node %s build successful\n", BUILD_OUTPUT);
}


static void build_parker() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = "main.n";
    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));
    strcpy(BUILD_OUTPUT_NAME, "main");

    build(entry, false);
    printf("parker %s build successful", BUILD_OUTPUT_DIR);
}

static void build_runner() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = "runner.n";
    char *workdir = get_workdir();

    strcpy(BUILD_OUTPUT_DIR, path_join(workdir, "tests/runnerdir"));
    strcpy(BUILD_OUTPUT_NAME, "runner");

    build(entry, false);
    printf("runner %s build successful", BUILD_OUTPUT_DIR);
}

int main(void) {
#ifdef __LINUX
    COMPILER_TRY {
        LOGF("build start\n");
        feature_test_package_sync();
        build_test_node();
        build_runner();
        build_parker();

        test_run();
        LOGF("build success\n");
    }
    else {
        assertf(false, "%s", (char *) test_error_msg);
        exit(1);
    }

#endif
}
