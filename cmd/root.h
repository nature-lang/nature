#ifndef NATURE_ROOT_H
#define NATURE_ROOT_H

#include "utils/value.h"
#include <stdio.h>
#include <unistd.h>
#include "root.h"
#include "src/module.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "src/build/config.h"
#include "src/build/build.h"

void cmd_entry(int argc, char **argv) {
    // 读取最后一个参数
    char *build_file = argv[argc - 1];
    if (!ends_with(build_file, ".n")) {
        error_exit("[cmd_entry] named files must be .n files: %s", build_file);
        return;
    }

    // -o 参数解析
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        switch (c) {
            case 'o':
                BUILD_OUTPUT_NAME = optarg;
                break;
        }
    }

    build(build_file);
}

#endif //NATURE_ROOT_H
