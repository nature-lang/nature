#ifndef NATURE_LD_MACHO_DYLIB_PATHS_H
#define NATURE_LD_MACHO_DYLIB_PATHS_H

#include "ld_internal.h"

#include <limits.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ld_macho_checked_paths_t;

void ld_macho_checked_paths_deinit(ld_macho_checked_paths_t *paths);

/* Resolve one LC_REEXPORT_DYLIB install-name.  Search order follows Zig's
   Mach-O dependency resolver: -F/-L basename lookup, sysrooted absolute
   install-names, token expansion, and finally a relative CWD lookup. */
int ld_macho_resolve_reexport_path(
        ld_context_t *ctx, const ld_dylib_input_t *parent,
        const char *install_name, char resolved[PATH_MAX], bool *found,
        ld_macho_checked_paths_t *checked);

#endif
