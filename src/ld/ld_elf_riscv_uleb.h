#ifndef NATURE_LD_ELF_RISCV_ULEB_H
#define NATURE_LD_ELF_RISCV_ULEB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LD_ELF_RISCV_ULEB_OK = 0,
    LD_ELF_RISCV_ULEB_INVALID_ARGUMENT,
    LD_ELF_RISCV_ULEB_TRUNCATED,
    LD_ELF_RISCV_ULEB_FIELD_OVERFLOW,
    LD_ELF_RISCV_ULEB_SET_OVERFLOW,
    LD_ELF_RISCV_ULEB_SUB_OVERFLOW,
} ld_elf_riscv_uleb_result_t;

/*
 * Apply one relocation while preserving the existing ULEB128 field width.
 * SET writes S + A. SUB reads the current field and subtracts S - A, matching
 * Zig 738d2be9's writeSetUleb/writeSubUleb relocation-order semantics.
 */
ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_apply(
        uint8_t *place, size_t place_size, bool subtract,
        uint64_t symbol_value, int64_t addend, size_t *encoded_size);

/*
 * Replace one existing ULEB128 field with
 *
 *   (set_symbol + set_addend) - (sub_symbol - sub_addend)
 *
 * without changing its encoded byte length.  The destination is not modified
 * unless all validation succeeds.
 */
ld_elf_riscv_uleb_result_t ld_elf_riscv_uleb_apply_pair(
        uint8_t *place, size_t place_size, uint64_t set_symbol,
        int64_t set_addend, uint64_t sub_symbol, int64_t sub_addend,
        size_t *encoded_size);

const char *ld_elf_riscv_uleb_result_string(
        ld_elf_riscv_uleb_result_t result);

#endif
