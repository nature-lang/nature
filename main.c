#include <stdio.h>
#include "cmd/root.h"
#include "utils/helper.h"
#include "config/config.h"
#include "utils/log.h"

#define ARGS_BUILD "build"

void print_help() {
    printf("Nature Programming Language Compiler %s\n\n", BUILD_VERSION);
    printf("Usage:\n");
    printf("  nature [command] [flags] [arguments]\n\n");

    printf("Available Commands:\n");
    printf("  build       Build a Nature source file\n\n");

    printf("Build Command Usage:\n");
    printf("  nature build [flags] <source_file>\n\n");

    printf("Build Flags:\n");
    printf("  -o <name>     Specify output filename (default: main)\n");
    printf("  --archive     Generate static library (output: lib<name>.a)\n");
    printf("  --target      Specify target platform for cross-compilation\n\n");

    printf("Cross Compilation:\n");
    printf("  nature build --target <platform> <source_file>\n\n");

    printf("  Supported Platforms:\n");
    printf("    - linux_amd64    Linux on x86-64 architecture\n");
    printf("    - linux_arm64    Linux on ARM 64-bit architecture\n");
    printf("    - darwin_amd64   macOS on x86-64 architecture\n");
    printf("    - darwin_arm64   macOS on ARM 64-bit architecture (Apple Silicon)\n\n");

    printf("Examples:\n");
    printf("  nature build main.n                         # Basic build\n");
    printf("  nature build -o test main.n                 # Custom output name\n");
    printf("  nature build --archive main.n               # Generate static library\n");
    printf("  nature build --target linux_arm64 main.n    # Cross-compile for Linux ARM64\n\n");

    printf("Global Flags:\n");
    printf("  --help, -h    Show help information\n");
    printf("  --version, -v     Show version information\n");
}

/**
 * nature build main.n [-o hello]
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[]) {
    // show help if no arguments
    if (argc <= 1) {
        print_help();
        return 0;
    }

    char *first = argv[1];
    if (str_equal(first, "--help") || str_equal(first, "-h")) {
        print_help();
        return 0;
    }

    if (str_equal(first, "--version") || str_equal(first, "-v")) {
        printf("nature %s - %s build %s\n", BUILD_VERSION, BUILD_TYPE, BUILD_TIME);
        return 0;
    }

    if (str_equal(first, ARGS_BUILD)) {
        argv[1] = argv[0];
        argv += 1;
        cmd_entry(argc - 1, argv);
        return 0;
    }

    printf("unknown command: %s\n", first);
    print_help();
    return 0;
}
