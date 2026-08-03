#ifndef NATURE_LD_MACHO_SYNTHETIC_H
#define NATURE_LD_MACHO_SYNTHETIC_H

#include "ld_internal.h"

#include <stdbool.h>
#include <stdint.h>

ld_macho_ref_t ld_macho_global_ref(ld_symbol_t *symbol);
ld_macho_ref_t ld_macho_local_ref(ld_object_t *object, uint32_t input_index);
bool ld_macho_ref_is_valid(const ld_macho_ref_t *ref);
bool ld_macho_ref_equal(const ld_macho_ref_t *left,
                        const ld_macho_ref_t *right);
ld_input_symbol_t *ld_macho_ref_input(const ld_macho_ref_t *ref);
const char *ld_macho_ref_name(const ld_macho_ref_t *ref);
uint32_t ld_macho_ref_section_type(const ld_macho_ref_t *ref);
bool ld_macho_ref_is_tlv(const ld_macho_ref_t *ref);
uint64_t ld_macho_ref_address(const ld_macho_ref_t *ref);
bool ld_macho_ref_symtab_index(const ld_macho_ref_t *ref, uint32_t *index);

int ld_macho_ref_list_add(ld_context_t *ctx, ld_macho_ref_list_t *list,
                          ld_macho_ref_t ref, uint32_t *index);
bool ld_macho_ref_list_find(const ld_macho_ref_list_t *list,
                            const ld_macho_ref_t *ref, uint32_t *index);

int ld_macho_got_add(ld_context_t *ctx, ld_macho_ref_t ref,
                     uint32_t *index);
bool ld_macho_got_find(const ld_context_t *ctx, const ld_macho_ref_t *ref,
                       uint32_t *index);
bool ld_macho_got_address(const ld_context_t *ctx,
                          const ld_macho_ref_t *ref, uint64_t *address);
int ld_macho_tlv_ptr_add(ld_context_t *ctx, ld_macho_ref_t ref,
                         uint32_t *index);
bool ld_macho_tlv_ptr_find(const ld_context_t *ctx,
                           const ld_macho_ref_t *ref, uint32_t *index);
bool ld_macho_tlv_ptr_address(const ld_context_t *ctx,
                              const ld_macho_ref_t *ref, uint64_t *address);

#endif
