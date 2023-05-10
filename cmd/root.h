#ifndef NATURE_ROOT_H
#define NATURE_ROOT_H

#include "utils/helper.h"
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
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
        assertf(false, "must specify the compile target with suffix n, example: nature build main.n");
        return;
    }

    // -o 参数解析
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        switch (c) {
            case 'o': {
                char *o_arg = optarg; // o_arg 指向字符串 "./haha/test"

                // 解析出一个相对路径
                char *output_dir = path_dir(o_arg);
                if (strlen(output_dir) > 0) {
                    char temp_path[PATH_MAX] = "";
                    if (realpath(output_dir, temp_path) == NULL) {
                        assertf(false, "output dir='%s' not created", output_dir);
                    }

                    strcpy(BUILD_OUTPUT_DIR, temp_path);
                    assertf(dir_exists(BUILD_OUTPUT_DIR), "build output dir='%s' cannot be a file", BUILD_OUTPUT_DIR);
                }

                // 解析出文件名称
                char *output_name = file_name(o_arg);
                if (strlen(output_name) > 0) {
                    strcpy(BUILD_OUTPUT_NAME, output_name);
                }

                break;
            }
        }
    }

    build(build_file);
}

#endif //NATURE_ROOT_H
