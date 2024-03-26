#ifndef NATURE_ROOT_H
#define NATURE_ROOT_H

#include "root.h"
#include "src/build/build.h"
#include "src/build/config.h"
#include "src/module.h"
#include "utils/error.h"
#include "utils/exec.h"
#include "utils/helper.h"
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

void cmd_entry(int argc, char **argv) {
    // 读取最后一个参数
    char *build_file = argv[argc - 1];

    if (!ends_with(build_file, ".n")) {
        assertf(false, "must specify the compile target with suffix n, example: nature build main.n");
        return;
    }

    struct option long_options[] = {
            {"archive", no_argument, NULL, 0},
            {"output", required_argument, NULL, 'o'},
            {NULL, 0, NULL, 0}};

    int option_index = 0;
    int c;

    bool libmain = false;

    // -o 参数解析
    // --archive 参数解析     int c;
    while ((c = getopt_long(argc, argv, "o:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'o': {
                char *o_arg = optarg;// o_arg 指向字符串 "./haha/test"

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
            case 0: {
                assert(strcmp(long_options[option_index].name, "archive") == 0);
                libmain = true;
                break;
            }
            default:
                break;
        }
    }

    if (libmain) {
        build_libmain(build_file);
    } else {
        build(build_file);
    }
}

#endif//NATURE_ROOT_H
