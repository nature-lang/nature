#include <stdio.h>
#include "cmd/root.h"
#include "utils/helper.h"

#define ARG_BUILD "build"

/**
 * nature build main.n [-o hello]
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {
    // set binary_path
    if (argc == 1) {
        printf("unknown command, recommend to use 'nature build main.n'");
        return 0;
    }

    char *first = argv[1];
    if (str_equal(first, ARG_BUILD)) {
        argv[1] = argv[0];
        argv += 1;
        cmd_entry(argc - 1, argv);
        return 0;
    }
    printf("unknown command: %s\n", first);
    return 0;
}
