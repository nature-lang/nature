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
    char *build_file = argv[optind];

    if (!ends_with(build_file, ".n")) {
        assertf(false, "must specify the compile target with suffix n, example: nature build main.n");
        return;
    }

    struct option long_options[] = {
            {"archive", no_argument, NULL, 0},
            {"output", required_argument, NULL, 'o'},
            {"target", required_argument, NULL, 1},
            {"ld", required_argument, NULL, 2},
            {"ldflags", required_argument, NULL, 3},
            {NULL, 0, NULL, 0}};

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
                        if (realpath(output_dir, BUILD_OUTPUT_DIR) == NULL) {
                            // assertf(false, "output dir='%s' not exists", output_dir);
                            printf("output dir '%s' not exists\n", output_dir);
                            free(output_dir);
                            exit(EXIT_FAILURE);
                        }
                        if (!dir_exists(BUILD_OUTPUT_DIR)) {
                            printf("output path '%s' is not a directory\n", BUILD_OUTPUT_DIR);
                            free(output_dir);
                            exit(EXIT_FAILURE);
                        }
                        // assertf(dir_exists(BUILD_OUTPUT_DIR), "build output dir='%s' not exists", BUILD_OUTPUT_DIR);
                    }
                    free(output_dir);

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
            case 1: {
                char *target = optarg;
                if (str_equal(target, "linux_amd64")) {
                    BUILD_OS = OS_LINUX;
                    BUILD_ARCH = ARCH_AMD64;
                } else if (str_equal(target, "linux_arm64")) {
                    BUILD_OS = OS_LINUX;
                    BUILD_ARCH = ARCH_ARM64;
                } else if (str_equal(target, "linux_riscv64")) {
                    BUILD_OS = OS_LINUX;
                    BUILD_ARCH = ARCH_RISCV64;
                } else if (str_equal(target, "darwin_amd64")) {
                    BUILD_OS = OS_DARWIN;
                    BUILD_ARCH = ARCH_AMD64;
                } else if (str_equal(target, "darwin_arm64")) {
                    BUILD_OS = OS_DARWIN;
                    BUILD_ARCH = ARCH_ARM64;
                } else {
                    printf("Invalid target: %s\n", target);
                    printf("Available targets: linux_amd64, linux_arm64, darwin_amd64, darwin_arm64\n");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 0: {
                assert(strcmp(long_options[option_index].name, "archive") == 0);
                is_archive = true;
                break;
            }
            case 2: {
                // 处理 --ld 参数
                strcpy(USE_LD, optarg);
                break;
            }
            case 3: {
                // 处理 --ldflags 参数
                strcpy(LDFLAGS, optarg);
                break;
            }
            default:
                break;
        }
    }


    build(build_file, is_archive);
}

#endif //NATURE_ROOT_H
