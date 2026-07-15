#include "src/ld/ld.h"

#include <stdio.h>
#include <string.h>

static void report_diagnostic(void *context, ld_diag_level_t level,
                              const char *message) {
    (void) context;
    (void) level;
    fprintf(stderr, "%s\n", message);
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s <amd64|arm64|riscv64> <exec|pie> "
                "<none|dwarf> <output> <input>...\n",
                argv[0]);
        return 2;
    }

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    if (strcmp(argv[1], "amd64") == 0) {
        options.arch = LD_ARCH_AMD64;
    } else if (strcmp(argv[1], "arm64") == 0) {
        options.arch = LD_ARCH_ARM64;
    } else if (strcmp(argv[1], "riscv64") == 0) {
        options.arch = LD_ARCH_RISCV64;
    } else {
        fprintf(stderr, "unsupported integration architecture: %s\n",
                argv[1]);
        ld_options_deinit(&options);
        return 2;
    }
    options.pie = strcmp(argv[2], "pie") == 0;
    options.debug_mode = strcmp(argv[3], "dwarf") == 0
                                 ? LD_DEBUG_DWARF
                                 : LD_DEBUG_NONE;
    options.output_path = argv[4];
    options.entry_symbol = "_start";
    options.adhoc_codesign = false;
    options.diagnostic = report_diagnostic;

    for (int i = 5; i < argc; i++) {
        int status = ld_add_input(&options, argv[i]);
        if (status != LD_OK) {
            ld_options_deinit(&options);
            return status;
        }
    }
    int status = ld_link(&options);
    ld_options_deinit(&options);
    return status;
}
