#include "config.h"

// 编译目标默认与构建平台架构一致
#ifdef __LINUX
build_param_t BUILD_OS = OS_LINUX;
#elif __DARWIN
build_param_t BUILD_OS = OS_DARWIN;
#else
build_param_t BUILD_OS = 0;
#endif

#ifdef __AMD64
build_param_t BUILD_ARCH = ARCH_AMD64;
#elif __RISCV64
build_param_t BUILD_ARCH = ARCH_RISCV64;
#else
build_param_t BUILD_ARCH = 0;
#endif

char *BUILD_OUTPUT_NAME = "main";
char *NATURE_ROOT = "/usr/local/nature"; // linux/darwin/freebsd default root
