#ifndef NATURE_LD_ELF_RELRO_H
#define NATURE_LD_ELF_RELRO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LD_ELF_RELRO_OK = 0,
    LD_ELF_RELRO_INVALID_ARGUMENT,
    LD_ELF_RELRO_INVALID_ALIGNMENT,
    LD_ELF_RELRO_SECTION_NOT_PROTECTED,
    LD_ELF_RELRO_NON_MONOTONIC_LAYOUT,
    LD_ELF_RELRO_INCONGRUENT_LAYOUT,
    LD_ELF_RELRO_LAYOUT_OVERFLOW,
    LD_ELF_RELRO_FINALIZED,
} ld_elf_relro_result_t;

typedef struct {
    uint64_t file_offset;
    uint64_t address;
    uint64_t file_end;
    uint64_t address_end;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t protection_end;
    bool present;
    bool finalized;
} ld_elf_relro_plan_t;

void ld_elf_relro_plan_init(ld_elf_relro_plan_t *plan);

bool ld_elf_relro_section_is_protected(const char *name, uint32_t type,
                                       uint64_t flags, uint64_t size);

bool ld_elf_relro_section_is_transparent(const char *name, uint32_t type,
                                         uint64_t flags, uint64_t size);

ld_elf_relro_result_t ld_elf_relro_add_section(
        ld_elf_relro_plan_t *plan, const char *name, uint32_t type,
        uint64_t flags, uint64_t file_offset, uint64_t address,
        uint64_t size, uint64_t file_size);

ld_elf_relro_result_t ld_elf_relro_finalize(
        ld_elf_relro_plan_t *plan, uint64_t page_size,
        uint64_t file_cursor, uint64_t address_cursor,
        uint64_t *next_file_cursor, uint64_t *next_address_cursor);

const char *ld_elf_relro_result_string(ld_elf_relro_result_t result);

#endif
