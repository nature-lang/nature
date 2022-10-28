#ifndef NATURE_BUILD_ENV_H
#define NATURE_BUILD_ENV_H

#include <stdbool.h>
#include <stdint.h>

#define ARCH_AMD64 1
#define OS_LINUX 1

uint8_t BUILD_OS; // linux
uint8_t BUILD_ARCH; // amd64
char *NATURE_ROOT; // nature root
char *BUILD_OUTPUT_NAME; // default is main
char *BUILD_OUTPUT_DIR; // default is work_dir test 使用，指定编译路径输出文件
char *BUILD_OUTPUT; // default = BUILD_OUTPUT_DIR/BUILD_OUTPUT_NAME

char *WORK_DIR; // 执行 shell 命令所在的目录
char *BASE_NS; // 最后一级目录的名称，也可以自定义
char *TEMP_DIR; // 链接临时目录

#define LINUX_BUILD_TMP_DIR  "/tmp/nature-build.XXXXXX"
#define DARWIN_BUILD_TMP_DIR  ""

#define LIB_START_FILE "crt1.o"
#define LIB_RUNTIME_FILE "libruntime.a"
#define LIB_C_FILE "libc.a"
#define LINKER_OUTPUT "a.out"

void env_init();

void config_init();

char *os_to_string(uint8_t os);

char *arch_to_string(uint8_t arch);

uint8_t os_to_uint8(char *os);

uint8_t arch_to_uint8(char *arch);


#endif //NATURE_ENV_H
