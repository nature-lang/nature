#include "config.h"
#include "utils/helper.h"
#include "utils/error.h"
#include <string.h>
#include <stdlib.h>

uint8_t BUILD_OS = OS_LINUX; // BUILD_OS/ BUILD_ARCH / BUILD_OUTPUT/NATURE_ROOT
uint8_t BUILD_ARCH = ARCH_AMD64;
char *BUILD_OUTPUT = "main";
char *NATURE_ROOT = "/usr/local/nature"; // linux/darwin/freebsd default root

static char *temp_dir() {
    char *tmp_dir;
    if (BUILD_OS == OS_LINUX) {
        char temp[] = LINUX_BUILD_TMP_DIR;
        mkdtemp(temp);

        size_t size = strlen(LINUX_BUILD_TMP_DIR) + 1;
        tmp_dir = malloc(size);
        memset(tmp_dir, '\0', size);
        strcpy(tmp_dir, temp);
    } else {
        goto ERROR;
    }

    return tmp_dir;
    ERROR:
    error_exit("[cross_tmp_dir] unsupported BUILD_OS/BUILD_ARCH pair %s/%s", BUILD_OS, BUILD_ARCH);
}

void env_init() {
    char *os = getenv("BUILD_OS");
    if (os != NULL) {
        BUILD_OS = os_to_uint8(os);
    }

    char *arch = getenv("BUILD_ARCH");
    if (arch != NULL) {
        BUILD_ARCH = arch_to_uint8(arch);
    }

    char *root = getenv("NATURE_ROOT");
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
    TEMP_DIR = temp_dir();
}

char *os_to_string(uint8_t os) {
    if (os == OS_LINUX) {
        return "linux";
    }
    return NULL;
}

char *arch_to_string(uint8_t arch) {
    if (arch == ARCH_AMD64) {
        return "amd64";
    }
    return NULL;
}

uint8_t os_to_uint8(char *os) {
    if (str_equal(os, "linux")) {
        return OS_LINUX;
    }
    return 0;
}

uint8_t arch_to_uint8(char *arch) {
    if (str_equal(arch, "amd64")) {
        return ARCH_AMD64;
    }
    return 0;
}
