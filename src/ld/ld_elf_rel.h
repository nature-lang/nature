#ifndef NATURE_LD_ELF_REL_H
#define NATURE_LD_ELF_REL_H

#include "ld_elf_internal.h"

/*
 * Decode the implicit addend stored at an ELF64 SHT_REL relocation site.
 * The parser converts REL and RELA records into the same internal relocation
 * representation, so allocation, symbol resolution, and application do not
 * need format-specific branches.
 */
int ld_elf_rel_decode_addend(ld_elf_context_t *ctx,
                             const ld_elf_object_t *object,
                             const ld_elf_section_t *target,
                             const ld_elf_relocation_t *relocation,
                             int64_t *addend);

#endif
