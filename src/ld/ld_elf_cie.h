#ifndef NATURE_LD_ELF_CIE_H
#define NATURE_LD_ELF_CIE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t offset;
    uint32_t type;
    int64_t addend;
    const void *target_namespace;
    uint64_t target_index;
} ld_elf_cie_relocation_t;

typedef struct {
    const uint8_t *bytes;
    size_t bytes_size;
    const ld_elf_cie_relocation_t *relocations;
    size_t relocation_count;
} ld_elf_cie_entry_t;

typedef enum {
    LD_ELF_CIE_OK = 0,
    LD_ELF_CIE_INVALID_ARGUMENT,
    LD_ELF_CIE_INVALID_ENTRY,
} ld_elf_cie_result_t;

/*
 * Assigns every CIE to the first byte-for-byte and relocation-equivalent CIE
 * in input order. canonical_indices must contain entry_count elements. The
 * output is left unchanged if validation fails, and error_entry_index receives
 * the invalid input entry or SIZE_MAX for a top-level argument error.
 */
ld_elf_cie_result_t ld_elf_cie_deduplicate(
        const ld_elf_cie_entry_t *entries, size_t entry_count,
        size_t *canonical_indices, size_t *error_entry_index);

const char *ld_elf_cie_result_string(ld_elf_cie_result_t result);

#endif
