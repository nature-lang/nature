#ifndef NATURE_FMT_H
#define NATURE_FMT_H

#include "root.h"
#include "utils/exec.h"
#include "utils/helper.h"
#include "utils/slice.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

void cmd_fmt_entry(int argc, char **argv) {
    struct option long_options[] = {
            {"write", no_argument, NULL, 'w'},
            {"check", no_argument, NULL, 'c'},
            {"diff", no_argument, NULL, 'd'},
            {"list", no_argument, NULL, 'l'},
            {"errors", no_argument, NULL, 'e'},
            {NULL, 0, NULL, 0}};

    int option_index = 0;
    int c;
    bool write_back = false;
    bool check_only = false;
    bool diff_only = false;
    bool list_only = false;
    bool report_all_errors = false;

    while ((c = getopt_long(argc, argv, "wcdle", long_options, &option_index)) != -1) {
        switch (c) {
            case 'w':
                write_back = true;
                break;
            case 'c':
                check_only = true;
                break;
            case 'd':
                diff_only = true;
                break;
            case 'l':
                list_only = true;
                break;
            case 'e':
                report_all_errors = true;
                break;
            default:
                break;
        }
    }

    if (check_only && write_back) {
        printf("usage: nature fmt [-w|--write] [--check] [--diff|-d] [-l|--list] [-e|--errors] [path ...]\n");
        printf("cannot use --write with --check or --diff\n");
        exit(EXIT_FAILURE);
    }

    slice_t *args = slice_new();
    slice_push(args, (void *)"fmt");
    if (write_back) {
        slice_push(args, (void *)"-w");
    }
    if (check_only) {
        slice_push(args, (void *)"--check");
    }
    if (diff_only) {
        slice_push(args, (void *)"--diff");
    }
    if (list_only) {
        slice_push(args, (void *)"-l");
    }
    if (report_all_errors) {
        slice_push(args, (void *)"-e");
    }

    for (int i = optind; i < argc; ++i) {
        slice_push(args, argv[i]);
    }

    exec_imm(NULL, "nls", args);
}

#endif // NATURE_FMT_H
