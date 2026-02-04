#ifndef NATURE_CMD_TEST_H
#define NATURE_CMD_TEST_H

#include "src/build/build.h"
#include "src/build/config.h"
#include "utils/exec.h"
#include "utils/helper.h"
#include "utils/slice.h"
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

void cmd_test_entry(int argc, char **argv) {
    struct option long_options[] = {
            {"skip", required_argument, NULL, 1},
            {NULL, 0, NULL, 0}};

    int option_index = 0;
    int c;

    BUILD_TEST = true;

    while ((c = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
        switch (c) {
            case 1: {
                if (!TEST_SKIP_LIST) {
                    TEST_SKIP_LIST = slice_new();
                }
                slice_push(TEST_SKIP_LIST, optarg);
                break;
            }
            default:
                break;
        }
    }

    const char *build_file = argv[optind];

    if (!build_file || !ends_with(build_file, ".n")) {
        assertf(false, "must specify the compile target with suffix n, example: nature test main.n");
        return;
    }

    build((char *) build_file, false);

    slice_t *args = slice_new();
    exec_imm(NULL, BUILD_OUTPUT, args);
}

#endif //NATURE_CMD_TEST_H
