#ifndef NATURE_LD_MACHO_SYMBOLS_H
#define NATURE_LD_MACHO_SYMBOLS_H

#include "ld_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LD_MACHO_RANK_DIRECT_STRONG = 0,
    LD_MACHO_RANK_ARCHIVE_OR_DYLIB_STRONG = 1,
    LD_MACHO_RANK_DIRECT_WEAK = 2,
    LD_MACHO_RANK_ARCHIVE_OR_DYLIB_WEAK = 3,
    LD_MACHO_RANK_DIRECT_TENTATIVE = 4,
    LD_MACHO_RANK_ARCHIVE_TENTATIVE = 5,
    LD_MACHO_RANK_UNCLAIMED = 6,
};

typedef struct {
    uint32_t class_value;
    size_t input_priority;
    size_t order;
} ld_macho_symbol_rank_t;

ld_symbol_visibility_t ld_macho_nlist_visibility(uint8_t n_type,
                                                 uint16_t n_desc);
ld_symbol_visibility_t ld_macho_merge_visibility(
        ld_symbol_visibility_t left, ld_symbol_visibility_t right);
bool ld_macho_symbol_is_exported(const ld_symbol_t *symbol);
bool ld_macho_symbol_is_exported_weak(const ld_symbol_t *symbol);
bool ld_macho_symbol_needs_stub(const ld_symbol_t *symbol);

ld_macho_symbol_rank_t ld_macho_object_symbol_rank(
        const ld_object_t *object, const ld_input_symbol_t *symbol,
        size_t order);
ld_macho_symbol_rank_t ld_macho_dylib_symbol_rank(
        const ld_dylib_input_t *dylib, const ld_dylib_symbol_t *symbol,
        size_t order);
bool ld_macho_symbol_rank_better(ld_macho_symbol_rank_t candidate,
                                 ld_macho_symbol_rank_t current);

const ld_dylib_symbol_t *ld_macho_dylib_find_symbol(
        ld_dylib_input_t *dylib, const char *name);
int ld_macho_dylib_record_symbol(ld_context_t *ctx,
                                 ld_dylib_input_t *dylib,
                                 const char *name, size_t name_length,
                                 const char *import_name,
                                 size_t import_name_length,
                                 bool weak, bool absolute, bool tlv,
                                 bool reexport);
void ld_macho_dylib_symbols_deinit(ld_dylib_input_t *dylib);

#endif
