#include "config.h"
#include <stdlib.h>
#include "utils/helper.h"
#include <string.h>

char *BUILD_OS = "linux"; // BUILD_OS/ BUILD_ARCH / BUILD_OUTPUT/NATURE_ROOT
char *BUILD_ARCH = "amd64";
char *BUILD_OUTPUT = "main";
char *NATURE_ROOT = "/usr/local/nature"; // linux/darwin/freebsd default root

void env_init() {
    char *os = getenv("BUILD_OS");
    if (os != NULL) {
        BUILD_OS = os;
    }

    char *arch = getenv("BUILD_ARCH");
    if (arch != NULL) {
        BUILD_ARCH = arch;
    }

    char *root = getenv("NATURE");
    if (root != NULL) {
        NATURE_ROOT = root;
    }
}

char *parser_base_ns(char *dir) {
    char *result = dir;
    // 取最后一节
    char *trim_path = strrchr(dir, '/');
    if (trim_path != NULL) {
        result = trim_path + 1;
    }

    return result;
}

void config_init() {
    WORK_DIR = get_work_dir();
    BASE_NS = parser_base_ns(WORK_DIR);
}
