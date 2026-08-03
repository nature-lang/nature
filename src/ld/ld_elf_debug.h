#ifndef NATURE_LD_ELF_DEBUG_H
#define NATURE_LD_ELF_DEBUG_H

#include "elf_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Classification for non-SHF_ALLOC input sections.  SKIP also covers input
 * metadata which the linker consumes itself (relocations, symbol/string
 * tables and groups), excluded sections, and stripped DWARF sections.
 */
typedef enum {
    LD_ELF_DEBUG_SECTION_SKIP = 0,
    LD_ELF_DEBUG_SECTION_DWARF,
    LD_ELF_DEBUG_SECTION_COMMENT,
    LD_ELF_DEBUG_SECTION_OTHER,
} ld_elf_debug_section_kind_t;

ld_elf_debug_section_kind_t ld_elf_debug_classify_nonalloc_section(
        const char *name, uint32_t type, uint64_t flags, bool keep_dwarf);

/*
 * A relocation from a debug section to a discarded COMDAT target uses a
 * tombstone.  Zig uses one for .debug_loc/.debug_ranges and zero for every
 * other .debug* section.
 */
typedef enum {
    LD_ELF_DEBUG_TOMBSTONE_NONE = 0,
    LD_ELF_DEBUG_TOMBSTONE_ZERO,
    LD_ELF_DEBUG_TOMBSTONE_ONE,
} ld_elf_debug_tombstone_t;

ld_elf_debug_tombstone_t ld_elf_debug_tombstone(
        const char *relocating_section_name, bool target_discarded);

typedef struct {
    uint32_t type;
    int64_t addend;
} ld_elf_debug_relocation_t;

/*
 * Values used by the non-allocated relocation formulae:
 *
 *   S   symbol_value
 *   Z   symbol_size
 *   GOT got_address
 *   DTP dynamic_thread_pointer
 *
 * The backend resolves discarded-target policy separately with
 * ld_elf_debug_tombstone(), then sets has_tombstone/tombstone_value here.
 */
typedef struct {
    uint64_t symbol_value;
    uint64_t symbol_size;
    uint64_t got_address;
    uint64_t dynamic_thread_pointer;
    uint64_t tombstone_value;
    bool has_tombstone;
} ld_elf_debug_reloc_values_t;

typedef enum {
    LD_ELF_DEBUG_RELOC_OK = 0,
    LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT,
    LD_ELF_DEBUG_RELOC_UNSUPPORTED_MACHINE,
    LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION,
    LD_ELF_DEBUG_RELOC_TRUNCATED,
    LD_ELF_DEBUG_RELOC_OVERFLOW,
    LD_ELF_DEBUG_RELOC_INVALID_PAIR,
    LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128,
} ld_elf_debug_reloc_result_t;

/*
 * Apply one Zig-supported non-allocated ELF64 relocation. machine is an
 * LD_ELF_EM_* value. place points at the relocation site, not the beginning
 * of its section. On every failure, place is left byte-for-byte unchanged.
 * written_size is optional and is reset to zero on entry.
 */
ld_elf_debug_reloc_result_t ld_elf_debug_apply_nonalloc_relocation(
        uint16_t machine, const ld_elf_debug_relocation_t *relocation,
        const ld_elf_debug_reloc_values_t *values, uint8_t *place,
        size_t place_size, size_t *written_size);

/*
 * Convenience helper which atomically applies an ordered
 * R_RISCV_SET_ULEB128/R_RISCV_SUB_ULEB128 sequence to one field. Each kind
 * remains independently supported by ld_elf_debug_apply_nonalloc_relocation.
 * The existing ULEB128 byte width is preserved.
 * In accordance with Zig 738d2be9, the resulting expression is:
 *
 *   (set S + set A) - (sub S - sub A)
 */
ld_elf_debug_reloc_result_t ld_elf_debug_apply_riscv_uleb_pair(
        const ld_elf_debug_relocation_t *set_relocation,
        const ld_elf_debug_reloc_values_t *set_values,
        const ld_elf_debug_relocation_t *sub_relocation,
        const ld_elf_debug_reloc_values_t *sub_values, uint8_t *place,
        size_t place_size, size_t *written_size);

const char *ld_elf_debug_reloc_result_string(
        ld_elf_debug_reloc_result_t result);

#endif
