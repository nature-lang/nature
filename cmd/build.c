#include <stdio.h>
#include <unistd.h>
#include "build.h"
#include "src/module.h"
#include "utils/helper.h"
#include "src/semantic/infer.h"
#include "src/compiler.h"
#include "src/cfg.h"
#include "src/debug/debug.h"
#include "src/lower/amd64/amd64.h"
#include "src/assembler/amd64/register.h"
#include "src/assembler/amd64/opcode.h"
#include "src/assembler/linux_elf/elf.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "src/build/cross.h"
#include "src/build/config.h"
#include "src/build/build.h"

#define LINUX_BUILD_DIR  "/tmp/nature-build.XXXXXX"

void build_arg(int argc, char **argv) {
    char *build_file = argv[argc - 1];
    if (!ends_with(build_file, ".n")) {
        error_exit("[build_arg] named files must be .n files: %s", build_file);
        return;
    }

    // -o 参数解析
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        switch (c) {
            case 'o':
                output_name = optarg;
                break;
        }
    }

    build(build_file);
}

