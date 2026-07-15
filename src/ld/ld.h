#ifndef NATURE_LD_H
#define NATURE_LD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LD_OS_DARWIN = 1,
    LD_OS_LINUX,
    LD_OS_WINDOWS,
} ld_os_t;

typedef enum {
    LD_ARCH_ARM64 = 1,
    LD_ARCH_AMD64,
    LD_ARCH_RISCV64,
} ld_arch_t;

typedef enum {
    LD_DIAG_NOTE = 0,
    LD_DIAG_WARNING,
    LD_DIAG_ERROR,
} ld_diag_level_t;

typedef enum {
    LD_DEBUG_NONE = 0,
    LD_DEBUG_DWARF,
} ld_debug_mode_t;

typedef void (*ld_diagnostic_fn)(void *context, ld_diag_level_t level, const char *message);

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ld_string_list_t;

typedef struct {
    ld_os_t os;
    ld_arch_t arch;
    const char *output_path;
    const char *entry_symbol;
    const char *sysroot;
    uint32_t min_os_version;
    uint32_t sdk_version;
    ld_debug_mode_t debug_mode;
    bool pie;
    bool adhoc_codesign;
    /* Match ld64's -ObjC archive-member extraction policy. */
    bool objc_load;

    ld_string_list_t inputs;
    ld_string_list_t library_paths;
    ld_string_list_t framework_paths;
    ld_string_list_t rpaths;
    ld_string_list_t libraries;
    ld_string_list_t frameworks;

    ld_diagnostic_fn diagnostic;
    void *diagnostic_context;
} ld_options_t;

enum {
    LD_OK = 0,
    LD_INVALID_ARGUMENT = 1,
    LD_IO_ERROR = 2,
    LD_INVALID_INPUT = 3,
    LD_UNSUPPORTED = 4,
    LD_SYMBOL_ERROR = 5,
    LD_RELOCATION_ERROR = 6,
    LD_OUTPUT_ERROR = 7,
};

void ld_options_init(ld_options_t *options);
void ld_options_deinit(ld_options_t *options);
int ld_add_input(ld_options_t *options, const char *path);
int ld_add_library_path(ld_options_t *options, const char *path);
int ld_add_rpath(ld_options_t *options, const char *path);
int ld_parse_flags(ld_options_t *options, const char *flags);
int ld_link(const ld_options_t *options);

static inline uint32_t ld_macos_version(uint32_t major, uint32_t minor, uint32_t patch) {
    return (major << 16U) | (minor << 8U) | patch;
}

#endif
