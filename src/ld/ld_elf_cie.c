#include "ld_elf_cie.h"

#include <stdbool.h>
#include <string.h>

/*
 * This is a direct C translation of Cie.eql and the first-winner loop in
 * calcEhFrameSize from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. Nature keeps the comparison in a
 * target-independent module so the ELF backend can use the same deterministic
 * rule on any host. Zig is MIT licensed; see ZIG-LICENSE.txt.
 */

static bool ld_elf_cie_equal(const ld_elf_cie_entry_t *left,
                             const ld_elf_cie_entry_t *right) {
    if (left->bytes_size != right->bytes_size ||
        (left->bytes_size &&
         memcmp(left->bytes, right->bytes, left->bytes_size) != 0) ||
        left->relocation_count != right->relocation_count) {
        return false;
    }
    for (size_t i = 0; i < left->relocation_count; i++) {
        const ld_elf_cie_relocation_t *a = &left->relocations[i];
        const ld_elf_cie_relocation_t *b = &right->relocations[i];
        if (a->offset != b->offset || a->type != b->type ||
            a->addend != b->addend ||
            a->target_namespace != b->target_namespace ||
            a->target_index != b->target_index) {
            return false;
        }
    }
    return true;
}

ld_elf_cie_result_t ld_elf_cie_deduplicate(
        const ld_elf_cie_entry_t *entries, size_t entry_count,
        size_t *canonical_indices, size_t *error_entry_index) {
    if (error_entry_index) *error_entry_index = SIZE_MAX;
    if ((entry_count && !entries) || (entry_count && !canonical_indices))
        return LD_ELF_CIE_INVALID_ARGUMENT;

    for (size_t i = 0; i < entry_count; i++) {
        if ((entries[i].bytes_size && !entries[i].bytes) ||
            (entries[i].relocation_count && !entries[i].relocations)) {
            if (error_entry_index) *error_entry_index = i;
            return LD_ELF_CIE_INVALID_ENTRY;
        }
    }

    for (size_t i = 0; i < entry_count; i++) {
        canonical_indices[i] = i;
        for (size_t j = 0; j < i; j++) {
            if (ld_elf_cie_equal(&entries[i], &entries[j])) {
                canonical_indices[i] = canonical_indices[j];
                break;
            }
        }
    }
    return LD_ELF_CIE_OK;
}

const char *ld_elf_cie_result_string(ld_elf_cie_result_t result) {
    switch (result) {
        case LD_ELF_CIE_OK:
            return "success";
        case LD_ELF_CIE_INVALID_ARGUMENT:
            return "invalid CIE deduplication argument";
        case LD_ELF_CIE_INVALID_ENTRY:
            return "invalid CIE data or relocation view";
    }
    return "unknown CIE deduplication error";
}
