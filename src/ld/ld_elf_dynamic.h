#ifndef NATURE_LD_ELF_DYNAMIC_H
#define NATURE_LD_ELF_DYNAMIC_H

#include "ld.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Minimal static-PIE dynamic metadata.  The ordering and tag selection follow
 * Zig's ELF linker at commit 738d2be9d6b6ef3ff3559130c05159ef53336224.
 * Nature deliberately omits DSO imports, symbol versioning and PT_INTERP.
 */

typedef enum {
    LD_ELF_DYNAMIC_OK = 0,
    LD_ELF_DYNAMIC_INVALID_ARGUMENT,
    LD_ELF_DYNAMIC_OVERFLOW,
    LD_ELF_DYNAMIC_BUFFER_TOO_SMALL,
    LD_ELF_DYNAMIC_UNSUPPORTED_ARCH,
} ld_elf_dynamic_result_t;

typedef enum {
    LD_ELF_DYNAMIC_RELOC_RELATIVE = 0,
    LD_ELF_DYNAMIC_RELOC_ORDINARY = 1,
    LD_ELF_DYNAMIC_RELOC_IRELATIVE = 2,
} ld_elf_dynamic_reloc_kind_t;

typedef struct {
    uint64_t offset;
    uint32_t symbol;
    uint32_t type;
    int64_t addend;
    ld_elf_dynamic_reloc_kind_t kind;
} ld_elf_dynamic_relocation_t;

typedef struct {
    bool has_init;
    bool has_fini;
    bool has_init_array;
    bool has_fini_array;
    bool has_rela;
    bool has_static_tls;
    uint64_t init_address;
    uint64_t fini_address;
    uint64_t init_array_address;
    uint64_t init_array_size;
    uint64_t fini_array_address;
    uint64_t fini_array_size;
    uint64_t rela_address;
    uint64_t rela_size;
    uint64_t hash_address;
    uint64_t gnu_hash_address;
    uint64_t dynsym_address;
    uint64_t dynstr_address;
    uint64_t dynstr_size;
} ld_elf_dynamic_metadata_t;

#define LD_ELF_DYNAMIC_MIN_DYNSYM_SIZE 24U
#define LD_ELF_DYNAMIC_MIN_DYNSTR_SIZE 1U
#define LD_ELF_DYNAMIC_MIN_HASH_SIZE 16U
#define LD_ELF_DYNAMIC_MIN_GNU_HASH_SIZE 28U

uint32_t ld_elf_dynamic_relative_type(ld_arch_t arch);
uint32_t ld_elf_dynamic_irelative_type(ld_arch_t arch);

void ld_elf_dynamic_sort_relocations(
        ld_elf_dynamic_relocation_t *relocations, size_t count);

ld_elf_dynamic_result_t ld_elf_dynamic_encode_relocations(
        const ld_elf_dynamic_relocation_t *relocations, size_t count,
        uint8_t *output, size_t output_size);

ld_elf_dynamic_result_t ld_elf_dynamic_encode_minimal_tables(
        uint8_t *dynsym, size_t dynsym_size, uint8_t *dynstr,
        size_t dynstr_size, uint8_t *hash, size_t hash_size,
        uint8_t *gnu_hash, size_t gnu_hash_size);

ld_elf_dynamic_result_t ld_elf_dynamic_metadata_size(
        const ld_elf_dynamic_metadata_t *metadata, size_t *size);

ld_elf_dynamic_result_t ld_elf_dynamic_encode_metadata(
        const ld_elf_dynamic_metadata_t *metadata, uint8_t *output,
        size_t output_size);

const char *ld_elf_dynamic_result_string(ld_elf_dynamic_result_t result);

#endif
