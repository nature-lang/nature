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
            {"archive", no_argument,       NULL, 0},
            {"output",  required_argument, NULL, 'o'},
            {NULL, 0,                      NULL, 0}};

    int option_index = 0;
    int c;

    bool is_archive = false;

    // -o 参数解析
    // --archive 参数解析     int c;
    while ((c = getopt_long(argc, argv, "o:", long_options, &option_index)) != -1) {
        switch (c) {
            case 'o': {
                char *o_arg = optarg;
                
                // 如果包含路径分隔符，则解析目录和文件名
                if (strchr(o_arg, '/') != NULL) {
                    // 解析出目录路径
                    char *output_dir = path_dir(o_arg);
                    if (strlen(output_dir) > 0) {
                        char temp_path[PATH_MAX] = "";
                        if (realpath(output_dir, temp_path) == NULL) {
                            // assertf(false, "output dir='%s' not exists", output_dir);
                            printf("output dir '%s' not exists\n", output_dir);
                            exit(EXIT_FAILURE);
                        }
                        strcpy(BUILD_OUTPUT_DIR, temp_path);
                        if (!dir_exists(BUILD_OUTPUT_DIR)) {
                            printf("build output dir '%s' not exists\n", BUILD_OUTPUT_DIR);
                            exit(EXIT_FAILURE);
                        }
                        // assertf(dir_exists(BUILD_OUTPUT_DIR), "build output dir='%s' not exists", BUILD_OUTPUT_DIR);
                    }
                    
                    // 解析出文件名称
                    char *output_name = file_name(o_arg);
                    if (strlen(output_name) > 0) {
                        strcpy(BUILD_OUTPUT_NAME, output_name);
                    }
                } else {
                    // 如果没有路径分隔符，直接作为输出文件名
                    strcpy(BUILD_OUTPUT_NAME, o_arg);
                }
                break;
            }
            case 0: {
                assert(strcmp(long_options[option_index].name, "archive") == 0);
                is_archive = true;
                break;
            }
            default:
                break;
        }
    }


    build(build_file, is_archive);
}

#endif//NATURE_ROOT_H
