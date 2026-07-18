#ifndef NATURE_BUILD_WINDOWS_LINKER_H
#define NATURE_BUILD_WINDOWS_LINKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    const char *name;
    bool required;
} windows_sysroot_library_t;

const windows_sysroot_library_t *windows_sysroot_libraries(size_t *count);

bool windows_linker_write_default_libraries(FILE *response,
                                            const char *sysroot);

bool build_command_exists(const char *command);

/* Environment-explicit entry point for deterministic host-path tests. */
bool build_command_exists_in_path(const char *command, const char *path,
                                  const char *pathext, bool windows_host);

#endif
