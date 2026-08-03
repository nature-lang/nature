#ifndef NATURE_LD_ELF_SYMTAB_H
#define NATURE_LD_ELF_SYMTAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint8_t binding;
    uint8_t type;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} ld_elf_symtab_entry_t;

typedef struct {
    uint8_t *symbols;
    size_t symbols_size;
    char *strings;
    size_t strings_size;
    size_t symbol_count;
    uint32_t first_global;
} ld_elf_symtab_t;

typedef enum {
    LD_ELF_SYMTAB_OK = 0,
    LD_ELF_SYMTAB_INVALID_ARGUMENT,
    LD_ELF_SYMTAB_SYMBOL_COUNT_OVERFLOW,
    LD_ELF_SYMTAB_SYMBOL_SIZE_OVERFLOW,
    LD_ELF_SYMTAB_STRING_SIZE_OVERFLOW,
    LD_ELF_SYMTAB_INVALID_INFO,
    LD_ELF_SYMTAB_OUT_OF_MEMORY,
} ld_elf_symtab_result_t;

void ld_elf_symtab_init(ld_elf_symtab_t *table);
void ld_elf_symtab_deinit(ld_elf_symtab_t *table);

/*
 * Builds a little-endian ELF64 SHT_SYMTAB payload and its linked string
 * table. A null symbol is inserted first, followed by every local entry and
 * then every global entry. error_entry_index refers to the combined
 * locals/globals input sequence and receives SIZE_MAX for non-entry errors.
 * The destination table remains empty on failure.
 */
ld_elf_symtab_result_t ld_elf_symtab_build(
        const ld_elf_symtab_entry_t *locals, size_t local_count,
        const ld_elf_symtab_entry_t *globals, size_t global_count,
        ld_elf_symtab_t *table, size_t *error_entry_index);

const char *ld_elf_symtab_result_string(ld_elf_symtab_result_t result);

#endif
