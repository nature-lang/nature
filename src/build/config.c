#include "config.h"
#ifdef __DARWIN
#include <mach-o/dyld.h>
#endif

// 编译目标默认与构建平台架构一致
#ifdef __LINUX
build_param_t BUILD_OS = OS_LINUX;
#elif __DARWIN
build_param_t BUILD_OS = OS_DARWIN;
#elif __WINDOWS
build_param_t BUILD_OS = OS_WINDOWS;
#else
build_param_t BUILD_OS = 0;
#endif

#ifdef __AMD64
build_param_t BUILD_ARCH = ARCH_AMD64;
#elif __ARM64
build_param_t BUILD_ARCH = ARCH_ARM64;
#elif __RISCV64
build_param_t BUILD_ARCH = ARCH_RISCV64;
#else
build_param_t BUILD_ARCH = 0;
#endif

char *NATURE_ROOT = NULL;
char *NATURE_PATH = "~/.nature";
char BUILD_OUTPUT_NAME[PATH_MAX] = "main";
bool BUILD_OUTPUT_EXPLICIT = false;
char SOURCE_PATH[PATH_MAX] = "";
char BUILD_OUTPUT_DIR[PATH_MAX] = "";
char BUILD_OUTPUT[PATH_MAX] = "";

char USE_LD[1024] = "";
char LDFLAGS[1024] = "";

bool VERBOSE = false; // 是否开启 verbose 模式

// test mode
bool BUILD_TEST = false;
slice_t *TEST_SKIP_LIST = NULL;

char *WORKDIR; // 执行 shell 命令所在的目录(import 搜索将会基于该目录进行文件搜索)
char *BASE_NS; // 最后一级目录的名称，也可以自定义
char *TEMP_DIR; // 链接临时目录

char *BUILD_ENTRY; // nature build {test/main.n} 花括号包起来的这部分

char *nature_root_from_executable_path(const char *executable_path) {
    if (executable_path == NULL || strlen(executable_path) == 0) {
        return NULL;
    }

    char *executable_dir = path_dir((char *) executable_path);
    char *candidate = path_dir(executable_dir);
    free(executable_dir);
    if (strlen(candidate) == 0) {
        free(candidate);
        return NULL;
    }

    for (int i = 0; i < 2; ++i) {
        char *builtin_dir = dsprintf("%s/std/builtin", candidate);
        bool exists = dir_exists(builtin_dir);
        free(builtin_dir);
        if (exists) {
            return candidate;
        }

        char *parent = path_dir(candidate);
        if (strlen(parent) == 0) {
            free(parent);
            break;
        }
        free(candidate);
        candidate = parent;
    }

    free(candidate);
    return NULL;
}

char *nature_root_from_running_executable() {
    char executable_path[PATH_MAX];
#ifdef __WINDOWS
    uint32_t length = GetModuleFileNameA(NULL, executable_path, PATH_MAX);
    if (length == 0 || length == PATH_MAX) {
        return NULL;
    }
    str_replace_char(executable_path, '\\', '/');
#elif __LINUX
    ssize_t length = readlink("/proc/self/exe", executable_path,
                              PATH_MAX - 1);
    if (length < 0 || length == PATH_MAX - 1) {
        return NULL;
    }
    executable_path[length] = '\0';
#elif __DARWIN
    uint32_t size = PATH_MAX;
    if (_NSGetExecutablePath(executable_path, &size) != 0) {
        return NULL;
    }
    char resolved_path[PATH_MAX];
    if (realpath(executable_path, resolved_path) != NULL) {
        strcpy(executable_path, resolved_path);
    }
#else
    return NULL;
#endif
    return nature_root_from_executable_path(executable_path);
}
