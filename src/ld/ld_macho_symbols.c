#include "ld_macho_symbols.h"

#include <stdlib.h>
#include <string.h>

/* Symbol ranking is translated from Zig MachO/file.zig getSymbolRank at
   commit 738d2be9d6b6ef3ff3559130c05159ef53336224.  C keeps the rank fields
   separate so very large input counts cannot collide with strength bits. */

ld_symbol_visibility_t ld_macho_nlist_visibility(uint8_t n_type,
                                                 uint16_t n_desc) {
    if ((n_type & LD_N_EXT) == 0) return LD_VISIBILITY_LOCAL;
    /* Zig leaves undefined nlists at the Symbol default visibility (local).
       Only a winning definition can make a resolver entry global or hidden.
       In particular, an ordinary undefined reference must not accidentally
       turn a private-external definition into an export. */
    if ((n_type & LD_N_TYPE) == LD_N_UNDF) return LD_VISIBILITY_LOCAL;
    if ((n_type & LD_N_PEXT) != 0 ||
        ((n_desc & (LD_N_WEAK_DEF | LD_N_WEAK_REF)) ==
         (LD_N_WEAK_DEF | LD_N_WEAK_REF))) {
        return LD_VISIBILITY_HIDDEN;
    }
    return LD_VISIBILITY_GLOBAL;
}

ld_symbol_visibility_t ld_macho_merge_visibility(
        ld_symbol_visibility_t left, ld_symbol_visibility_t right) {
    /* Zig's visibility rank is global < hidden < local and it retains the
       lowest rank observed across all definitions/references. */
    return left > right ? right : left;
}

bool ld_macho_symbol_is_exported(const ld_symbol_t *symbol) {
    if (!symbol || symbol->visibility != LD_VISIBILITY_GLOBAL ||
        symbol->objc_selector_stub) {
        return false;
    }
    if (symbol->execute_header) return true;
    if (symbol->kind == LD_SYMBOL_ABSOLUTE) {
        return !symbol->linker_defined;
    }
    return (symbol->kind == LD_SYMBOL_DEFINED ||
            symbol->kind == LD_SYMBOL_COMMON) &&
           symbol->output != NULL;
}

bool ld_macho_symbol_is_exported_weak(const ld_symbol_t *symbol) {
    return ld_macho_symbol_is_exported(symbol) && symbol->weak;
}

bool ld_macho_symbol_needs_stub(const ld_symbol_t *symbol) {
    return symbol && (symbol->kind == LD_SYMBOL_IMPORT ||
                      ld_macho_symbol_is_exported_weak(symbol));
}

ld_macho_symbol_rank_t ld_macho_object_symbol_rank(
        const ld_object_t *object, const ld_input_symbol_t *symbol,
        size_t order) {
    bool archive = object && object->archive_member;
    bool weak = symbol && (symbol->entry.n_desc & LD_N_WEAK_DEF) != 0;
    bool tentative = symbol &&
                     (symbol->entry.n_type & LD_N_TYPE) == LD_N_UNDF &&
                     symbol->entry.n_value != 0;
    uint32_t class_value;
    if (tentative) {
        class_value = archive ? LD_MACHO_RANK_ARCHIVE_TENTATIVE
                              : LD_MACHO_RANK_DIRECT_TENTATIVE;
    } else if (weak) {
        class_value = archive ? LD_MACHO_RANK_ARCHIVE_OR_DYLIB_WEAK
                              : LD_MACHO_RANK_DIRECT_WEAK;
    } else {
        class_value = archive ? LD_MACHO_RANK_ARCHIVE_OR_DYLIB_STRONG
                              : LD_MACHO_RANK_DIRECT_STRONG;
    }
    return (ld_macho_symbol_rank_t) {
            .class_value = class_value,
            .input_priority = object && object->file
                                      ? object->file->input_priority
                                      : SIZE_MAX,
            .order = order,
    };
}

ld_macho_symbol_rank_t ld_macho_dylib_symbol_rank(
        const ld_dylib_input_t *dylib, const ld_dylib_symbol_t *symbol,
        size_t order) {
    return (ld_macho_symbol_rank_t) {
            .class_value = symbol && symbol->weak
                                   ? LD_MACHO_RANK_ARCHIVE_OR_DYLIB_WEAK
                                   : LD_MACHO_RANK_ARCHIVE_OR_DYLIB_STRONG,
            .input_priority = dylib ? dylib->input_priority : SIZE_MAX,
            .order = order,
    };
}

bool ld_macho_symbol_rank_better(ld_macho_symbol_rank_t candidate,
                                 ld_macho_symbol_rank_t current) {
    if (candidate.class_value != current.class_value) {
        return candidate.class_value < current.class_value;
    }
    if (candidate.input_priority != current.input_priority) {
        return candidate.input_priority < current.input_priority;
    }
    return candidate.order < current.order;
}

const ld_dylib_symbol_t *ld_macho_dylib_find_symbol(
        ld_dylib_input_t *dylib, const char *name) {
    if (!dylib || !name || !dylib->symbol_index.mem) return NULL;
    uint64_t index = sc_map_get_s64(&dylib->symbol_index, name);
    if (!sc_map_found(&dylib->symbol_index) || index >= dylib->symbol_count) {
        return NULL;
    }
    return &dylib->symbols[index];
}

static char *ld_macho_symbol_strndup(const char *value, size_t length) {
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length);
    copy[length] = '\0';
    return copy;
}

int ld_macho_dylib_record_symbol(ld_context_t *ctx,
                                 ld_dylib_input_t *dylib,
                                 const char *name, size_t name_length,
                                 const char *import_name,
                                 size_t import_name_length,
                                 bool weak, bool absolute, bool tlv,
                                 bool reexport) {
    if (!dylib || !name || name_length == 0 || name_length > 4096U) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "invalid dynamic-library export name");
    }
    if (!dylib->symbol_index.mem &&
        !sc_map_init_s64(&dylib->symbol_index, 0, 0)) {
        return ld_fail(ctx, LD_IO_ERROR,
                       "out of memory recording dynamic-library export");
    }

    char lookup_name[4097];
    memcpy(lookup_name, name, name_length);
    lookup_name[name_length] = '\0';
    uint64_t existing_index =
            sc_map_get_s64(&dylib->symbol_index, lookup_name);
    if (sc_map_found(&dylib->symbol_index)) {
        if (existing_index >= dylib->symbol_count) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "invalid dynamic-library symbol index");
        }
        ld_dylib_symbol_t *existing = &dylib->symbols[existing_index];
        if (!weak && existing->weak) {
            existing->weak = false;
            existing->absolute = absolute;
            existing->tlv = tlv;
            existing->reexport = reexport;
        } else if (weak == existing->weak) {
            existing->absolute = existing->absolute || absolute;
            existing->tlv = existing->tlv || tlv;
            existing->reexport = existing->reexport || reexport;
        }
        if (import_name && import_name_length && !existing->import_name) {
            existing->import_name =
                    ld_macho_symbol_strndup(import_name, import_name_length);
            if (!existing->import_name) {
                return ld_fail(ctx, LD_IO_ERROR,
                               "out of memory recording dynamic-library export");
            }
        }
        return LD_OK;
    }
    if (dylib->symbol_count == dylib->symbol_capacity) {
        size_t next = dylib->symbol_capacity ? dylib->symbol_capacity * 2U
                                             : 128U;
        if (next < dylib->symbol_capacity) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "dynamic-library export table overflows");
        }
        ld_dylib_symbol_t *items = ld_realloc_array(
                dylib->symbols, dylib->symbol_capacity, next,
                sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "out of memory recording dynamic-library export");
        }
        dylib->symbols = items;
        dylib->symbol_capacity = next;
    }
    ld_dylib_symbol_t *symbol = &dylib->symbols[dylib->symbol_count];
    symbol->name = ld_macho_symbol_strndup(name, name_length);
    if (import_name && import_name_length) {
        symbol->import_name =
                ld_macho_symbol_strndup(import_name, import_name_length);
    }
    if (!symbol->name || (import_name && import_name_length &&
                          !symbol->import_name)) {
        free(symbol->name);
        free(symbol->import_name);
        memset(symbol, 0, sizeof(*symbol));
        return ld_fail(ctx, LD_IO_ERROR,
                       "out of memory recording dynamic-library export");
    }
    symbol->weak = weak;
    symbol->absolute = absolute;
    symbol->tlv = tlv;
    symbol->reexport = reexport;
    sc_map_put_s64(&dylib->symbol_index, symbol->name,
                   (uint64_t) dylib->symbol_count);
    if (sc_map_oom(&dylib->symbol_index)) {
        free(symbol->name);
        free(symbol->import_name);
        memset(symbol, 0, sizeof(*symbol));
        return ld_fail(ctx, LD_IO_ERROR,
                       "out of memory indexing dynamic-library export");
    }
    dylib->symbol_count++;
    return LD_OK;
}

void ld_macho_dylib_symbols_deinit(ld_dylib_input_t *dylib) {
    if (!dylib) return;
    if (dylib->symbol_index.mem) {
        sc_map_term_s64(&dylib->symbol_index);
    }
    for (size_t i = 0; i < dylib->symbol_count; i++) {
        free(dylib->symbols[i].name);
        free(dylib->symbols[i].import_name);
    }
    free(dylib->symbols);
    dylib->symbols = NULL;
    dylib->symbol_count = 0;
    dylib->symbol_capacity = 0;
}
