#include "cmd/fmt.h"
#include "cmd/root.h"
#include "cmd/self_update.h"
#include "cmd/test.h"
#include "config/config.h"
#include "utils/helper.h"
#include "utils/log.h"
#include <stdio.h>

#define ARGS_BUILD "build"
#define ARGS_FMT "fmt"
#define ARGS_SELF_UPDATE "self-update"
#define ARGS_TEST "test"

void print_help() {
    printf("Nature Programming Language Compiler %s\n\n", BUILD_VERSION);
    printf("Usage:\n");
    printf("  nature [command] [flags] [arguments]\n\n");

    printf("Available Commands:\n");
    printf("  build       Build a Nature source file\n");
    printf("  fmt         Format one or more Nature source files\n");
    printf("  self-update Update Nature installation\n");
    printf("  test        Run tests in a Nature source file\n\n");

    printf("Build Command Usage:\n");
    printf("  nature build [flags] <source_file>\n\n");

    printf("Test Command Usage:\n");
    printf("  nature test [flags] <source_file>\n\n");

    printf("Self-Update Command Usage:\n");
    printf("  nature self-update [--check] [--yes] [--force]\n\n");

    printf("Build Flags:\n");
    printf("  -o <name>     Specify output filename (default: main)\n");
    printf("  --archive     Generate static library (output: lib<name>.a)\n");
    printf("  --target      Specify target platform for cross-compilation\n");
    printf("  --ld <path>   Specify the path to the linker\n");
    printf("  --ldflags <flags> Specify linker flags\n");
    printf("  --verbose     Enable verbose mode (show debug logs and keep temp dir)\n\n");

    printf("Test Flags:\n");
    printf("  --skip <name> Skip a test by name (repeatable)\n\n");
    printf("Fmt Flags:\n");
    printf("  -w, --write  Format files in place\n");
    printf("  --check      Exit with status 1 if any file would be reformatted\n");
    printf("  --diff, -d   Print a unified diff for files that would be reformatted\n");
    printf("  -l, --list   List files whose formatting differs from the canonical style\n");
    printf("  -e, --errors Report all lexer errors instead of just the first one\n\n");

    printf("Self-Update Flags:\n");
    printf("  --check      Check latest version without installing\n");
    printf("  --yes        Skip confirmation prompt\n");
    printf("  --force      Reinstall even when already up to date\n\n");

    printf("Cross Compilation:\n");
    printf("  nature build --target <platform> <source_file>\n\n");

    printf("  Supported Platforms:\n");
    printf("    - linux_amd64    Linux on x86-64 architecture\n");
    printf("    - linux_arm64    Linux on ARM 64-bit architecture\n");
    printf("    - linux_riscv64  Linux on RISCV 64-bit architecture\n");
    printf("    - darwin_amd64   macOS on x86-64 architecture\n");
    printf("    - darwin_arm64   macOS on ARM 64-bit architecture (Apple Silicon)\n\n");

    printf("Examples:\n");
    printf("  nature build main.n                         # Basic build\n");
    printf("  nature build -o test main.n                 # Custom output name\n");
    printf("  nature build --archive main.n               # Generate static library\n");
    printf("  nature build --target linux_arm64 main.n    # Cross-compile for Linux ARM64\n");
    printf("  nature build --ld /usr/bin/ld main.n  # Custom linker and flags\n\n");

    printf("  nature test main.n                          # Run tests\n");
    printf("  nature test --skip sum main.n               # Skip a specific test\n\n");
    printf("  nature fmt                                  # Read from stdin and write formatted output\n");
    printf("  nature fmt src/                             # Recursively format .n files under a directory\n");
    printf("  nature fmt main.n                           # Print formatted output\n");
    printf("  nature fmt --check main.n                   # Check formatting without writing\n");
    printf("  nature fmt --diff main.n                    # Show the formatting diff\n");
    printf("  nature fmt -l src/                          # List files that are not canonically formatted\n");
    printf("  nature fmt -w main.n                        # Rewrite the file in place\n\n");
    printf("  nature self-update --check                  # Check latest version\n");
    printf("  nature self-update --yes                    # Update without prompt\n");
    printf("  nature self-update --force --yes            # Reinstall latest version\n\n");

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

    if (str_equal(first, ARGS_FMT)) {
        argv[1] = argv[0];
        argv += 1;
        cmd_fmt_entry(argc - 1, argv);
        return 0;
    }

    if (str_equal(first, ARGS_TEST)) {
        argv[1] = argv[0];
        argv += 1;
        cmd_test_entry(argc - 1, argv);
        return 0;
    }

    if (str_equal(first, ARGS_SELF_UPDATE)) {
        argv[1] = argv[0];
        argv += 1;
        cmd_self_update_entry(argc - 1, argv);
        return 0;
    }

    printf("unknown command: %s\n", first);
    print_help();
    return 0;
}
