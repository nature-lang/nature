#ifndef NATURE_LDD_H
#define NATURE_LDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LDD_OS_DARWIN = 1,
    LDD_OS_LINUX,
    LDD_OS_WINDOWS,
} ldd_os_t;

typedef enum {
    LDD_ARCH_ARM64 = 1,
    LDD_ARCH_AMD64,
    LDD_ARCH_RISCV64,
} ldd_arch_t;

typedef enum {
    LDD_DIAG_NOTE = 0,
    LDD_DIAG_WARNING,
    LDD_DIAG_ERROR,
} ldd_diag_level_t;

typedef void (*ldd_diagnostic_fn)(void *context, ldd_diag_level_t level, const char *message);

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ldd_string_list_t;

typedef struct {
    ldd_os_t os;
    ldd_arch_t arch;
    const char *output_path;
    const char *entry_symbol;
    const char *sysroot;
    uint32_t min_os_version;
    uint32_t sdk_version;
    bool pie;
    bool adhoc_codesign;

    ldd_string_list_t inputs;
    ldd_string_list_t library_paths;
    ldd_string_list_t framework_paths;
    ldd_string_list_t libraries;
    ldd_string_list_t frameworks;

    ldd_diagnostic_fn diagnostic;
    void *diagnostic_context;
} ldd_options_t;

enum {
    LDD_OK = 0,
    LDD_INVALID_ARGUMENT = 1,
    LDD_IO_ERROR = 2,
    LDD_INVALID_INPUT = 3,
    LDD_UNSUPPORTED = 4,
    LDD_SYMBOL_ERROR = 5,
    LDD_RELOCATION_ERROR = 6,
    LDD_OUTPUT_ERROR = 7,
};

void ldd_options_init(ldd_options_t *options);
void ldd_options_deinit(ldd_options_t *options);
int ldd_add_input(ldd_options_t *options, const char *path);
int ldd_add_library_path(ldd_options_t *options, const char *path);
int ldd_parse_flags(ldd_options_t *options, const char *flags);
int ldd_link(const ldd_options_t *options);

static inline uint32_t ldd_macos_version(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 16U) | (minor << 8U) | patch;
}

#endif
