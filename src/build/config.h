/**
 * 各种编译相关参数
 */

#ifndef NATURE_BUILD_ENV_H
#define NATURE_BUILD_ENV_H

char *BUILD_OS; // linux
char *BUILD_ARCH; // x86_64
char *NATURE_ROOT; // nature root
char *BUILD_OUTPUT; // default is main

char *WORK_DIR; // 执行 shell 命令所在的目录
char *BASE_NS; // 最后一级目录的名称，也可以自定义

#define LIB_START_FILE "crt1.o"
#define LIB_RUNTIME_FILE "libruntime.a"
#define LIB_C_FILE "libc.a"
#define LINKER_ELF_NAME "ld"
#define LINKER_OUTPUT "a.out"

void env_init();

void config_init();

#endif //NATURE_ENV_H
