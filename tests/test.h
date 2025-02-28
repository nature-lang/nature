#ifndef NATURE_TESTS_H
#define NATURE_TESTS_H

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/build/build.h"
#include "utils/exec.h"
#include "src/error.h"

#ifdef __LINUX
#define ATOMIC
#else
#define ATOMIC _Atomic
#endif


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

static inline char *exec_output_status(int *status) {
    assert(status);
    return exec(WORKDIR, BUILD_OUTPUT, slice_new(), NULL, status);
}

static inline char *exec_output() {
    return exec(WORKDIR, BUILD_OUTPUT, slice_new(), NULL, NULL);
}

static inline char *exec_output_with_pid(int32_t *pid) {
    return exec(WORKDIR, BUILD_OUTPUT, slice_new(), pid, NULL);
}

static inline char *exec_with_args(slice_t *args) {
    return exec(WORKDIR, BUILD_OUTPUT, args, NULL, NULL);
}

static inline int feature_test_build() {
    char *nature_root = getenv("NATURE_ROOT");
    assert_true(nature_root != NULL);

    // 从环境变量中读取 build entry
    char *entry = getenv("ENTRY_FILE");
    assert_true(entry && "entry file is null");

    strcpy(BUILD_OUTPUT_DIR, getenv("BUILD_OUTPUT_DIR"));

    COMPILER_TRY {
        build(entry, false);
    } else {
        assertf(false, "%s", (char *) test_error_msg);
        exit(1);
    }

    return 0;
}

typedef struct {
    char *name; // 文件名称
    uint8_t *content; // 文件内容
    uint64_t length; // 文件长度
} testar_case_file_t;

typedef struct {
    char *name; // 测试用例名称
    slice_t *files; // testar_case_file_t
} testar_case_t;

/**
 * 解析单个文件内容
 */
static inline testar_case_file_t *parse_file(char *content, size_t *offset) {
    testar_case_file_t *file = malloc(sizeof(testar_case_file_t));
    char *start = content + *offset;

    // 跳过空行
    while (*start == '\n') {
        start++;
        (*offset)++;
    }

    // 跳过 "--- " 前缀
    if (strncmp(start, "---", 3) != 0) {
        free(file);
        return NULL;
    }
    start += 4; // 跳过 "--- "

    // 读取文件名
    char *name_end = strchr(start, '\n');
    if (!name_end) {
        free(file);
        return NULL;
    }
    size_t name_len = name_end - start;
    file->name = malloc(name_len + 1);
    strncpy(file->name, start, name_len);
    file->name[name_len] = '\0';

    // 移动到文件内容开始处
    start = name_end + 1;

    // 查找下一个文件标记或测试用例标记
    char *next_file = strstr(start, "\n---");
    char *next_case = strstr(start, "\n===");
    char *content_end;

    if (next_file && (!next_case || next_file < next_case)) {
        content_end = next_file;
    } else if (next_case) {
        content_end = next_case;
    } else {
        content_end = start + strlen(start);
    }

    // 计算文件内容长度
    size_t content_len = content_end - start;
    // 去除尾部空行，但保留至少一个换行符
    while (content_len > 1 &&
           (start[content_len - 1] == '\n' || start[content_len - 1] == '\r') &&
           (start[content_len - 2] == '\n' || start[content_len - 2] == '\r')) {
        content_len--;
    }

    file->content = malloc(content_len + 1);
    memcpy(file->content, start, content_len);
    file->content[content_len] = '\0';
    file->length = content_len;

    // 更新偏移量到下一个文件或测试用例的开始处
    *offset += (content_end - (content + *offset));

    return file;
}

/**
 * 基于 ===xxx 进行分隔, 进行多次测试用例测试
 */
static inline slice_t *testar_decompress(char *content) {
    slice_t *cases = slice_new();
    size_t offset = 0;

    while (content[offset]) {
        // 查找测试用例开始标记
        if (strncmp(content + offset, "===", 3) != 0) {
            offset++;
            continue;
        }

        // 创建新的测试用例
        testar_case_t *test_case = malloc(sizeof(testar_case_t));
        test_case->files = slice_new();

        // 读取测试用例名称
        char *name_start = content + offset + 4; // 跳过 "=== "
        char *name_end = strchr(name_start, '\n');
        size_t name_len = name_end - name_start;
        test_case->name = malloc(name_len + 1);
        strncpy(test_case->name, name_start, name_len);
        test_case->name[name_len] = '\0';

        // 更新偏移量到文件内容开始处
        offset = name_end - content + 1;

        // 解析所有文件
        while (1) {
            testar_case_file_t *file = parse_file(content, &offset);
            if (!file) break;
            slice_push(test_case->files, file);
        }

        slice_push(cases, test_case);
    }

    return cases;
}

static inline void feature_testar_test(char *custom_target) {
    char *testar_file = getenv("TESTAR_FILE");
    assert_true(testar_file && "testar file is null");

    // TEST_EXEC_IMM
    char *content = file_read(testar_file);
    slice_t *cases = testar_decompress(content);

    // 选定测试目录 /tmp/nature_test
    char *test_dir = "/tmp/nature-test";

    // 创建测试目录
    if (!dir_exists(test_dir)) {
        system("mkdir -p /tmp/nature-test");
    }

    // 设置工作目录为测试目录
    if (chdir(test_dir) != 0) {
        assertf(false, "cannot change working directory to %s", test_dir);
    }

    // 遍历测试用例进行逐个测试，并进行输出
    for (int i = 0; i < cases->count; i++) {
        testar_case_t *test_case = cases->take[i];
        if (custom_target && !str_equal(custom_target, test_case->name)) {
            continue;
        }

        printf("test case start=== %s\n", test_case->name);

        assertf(test_case->files->count > 0, "test case '%s' must have main.n", test_case->name);

        testar_case_file_t *output_file = NULL;
        bool has_entry_main = false;

        for (int j = 0; j < test_case->files->count; j++) {
            testar_case_file_t *file = test_case->files->take[j];

            if (str_equal(file->name, "output.txt")) {
                output_file = file;
            }

            if (str_equal(file->name, "main.n")) {
                has_entry_main = true;
            }

            // 构建完整的文件路径
            char *full_path = path_join(test_dir, file->name);

            char *dir = path_dir(full_path);
            if (!dir_exists(dir)) {
                system(str_connect("mkdir -p ", dir));
            }

            // 写入文件内容
            FILE *fp = fopen(full_path, "w");
            assert(fp);
            fwrite(file->content, 1, file->length, fp);
            fclose(fp);
        }

        assertf(has_entry_main, "test case '%s' must have main.n", test_case->name);

        // 固定格式生命
        char *entry = "main.n";

        COMPILER_TRY {
            build(entry, false);

            // 执行并测试
            if (output_file) {
                char *output = exec_output();
                assertf(str_equal(output, (char *) output_file->content), "n %s failed\nexpect: %sactual: %s",
                        test_case->name, output_file->content, output);
            } else {
                int32_t status = 0;
                char *output = exec_output_status(&status);
                if (status != 0) {
                    assertf(false, "%s failed: %s", test_case->name, output);
                } else {
                    printf("%s", output);
                }
            }
        } else {
            // 编译错误处理
            if (output_file) {
                assertf(str_equal(test_error_msg, (char *) output_file->content), "in %s\nexpect: %sactual: %s",
                        test_case->name, output_file->content, test_error_msg);
            } else {
                assertf(false, "%s failed: %s", test_case->name, test_error_msg);
            }
        };


        // 进行编译
        printf("test case success === %s\n", test_case->name);
    }
}

static inline void feature_test_package_sync() {
    // 环境变量下查找 package 可执行文件 npkg
    char *workdir = get_workdir();
    char *npkg_path = getenv("NPKG_PATH");
    char *output = command_output(workdir, str_connect(npkg_path, PACKAGE_SYNC_COMMAND));
    log_debug("npkg sync:%s", output);
}

#define TEST_EXEC_IMM   \
    feature_test_build(); \
    exec_imm_param();

#define TEST_BASIC    \
    feature_test_build(); \
    test_basic();

#define TEST_WITH_PACKAGE    \
    feature_test_package_sync(); \
    feature_test_build();        \
    test_basic();

#endif // NATURE_TEST_H
