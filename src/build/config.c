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
#elif __ARM64
build_param_t BUILD_ARCH = ARCH_ARM64;
#elif __RISCV64
build_param_t BUILD_ARCH = ARCH_RISCV64;
#else
build_param_t BUILD_ARCH = 0;
#endif

char *NATURE_ROOT = "/usr/local/nature"; // linux/darwin/freebsd default root
char *NATURE_PATH = "~/.nature";
char BUILD_OUTPUT_NAME[PATH_MAX] = "main";
char SOURCE_PATH[PATH_MAX] = "";
char BUILD_OUTPUT_DIR[PATH_MAX] = "";
char BUILD_OUTPUT[PATH_MAX] = "";

char USE_LD[1024] = "";
char LDFLAGS[1024] = "";

bool VERBOSE = false; // 是否开启 verbose 模式

char *WORKDIR; // 执行 shell 命令所在的目录(import 搜索将会基于该目录进行文件搜索)
char *BASE_NS; // 最后一级目录的名称，也可以自定义
char *TEMP_DIR; // 链接临时目录

char *BUILD_ENTRY; // nature build {test/main.n} 花括号包起来的这部分
