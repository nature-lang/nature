#include "ld_elf_relro.h"

#include "elf_format.h"

#include <stddef.h>
#include <string.h>

static bool ld_elf_relro_add_overflow(uint64_t left, uint64_t right,
                                      uint64_t *result) {
    if (left > UINT64_MAX - right) return true;
    *result = left + right;
    return false;
}

static bool ld_elf_relro_align(uint64_t value, uint64_t alignment,
                               uint64_t *result) {
    if (alignment == 0U) return false;
    uint64_t mask = alignment - 1U;
    if ((alignment & mask) != 0U || value > UINT64_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static bool ld_elf_relro_name_family(const char *name, const char *base) {
    size_t length = strlen(base);
    return strcmp(name, base) == 0 ||
           (strncmp(name, base, length) == 0 && name[length] == '.');
}

void ld_elf_relro_plan_init(ld_elf_relro_plan_t *plan) {
    if (plan) memset(plan, 0, sizeof(*plan));
}

bool ld_elf_relro_section_is_protected(const char *name, uint32_t type,
                                       uint64_t flags, uint64_t size) {
    if (!name || size == 0U ||
        (flags & (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE)) !=
                (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE)) {
        return false;
    }
    if ((flags & LD_ELF_SHF_TLS) != 0U)
        return type != LD_ELF_SHT_NOBITS;
    if (type == LD_ELF_SHT_PREINIT_ARRAY ||
        type == LD_ELF_SHT_INIT_ARRAY || type == LD_ELF_SHT_FINI_ARRAY) {
        return true;
    }
    return ld_elf_relro_name_family(name, ".data.rel.ro") ||
           ld_elf_relro_name_family(name, ".bss.rel.ro") ||
           strcmp(name, ".got") == 0 || strcmp(name, ".dynamic") == 0;
}

bool ld_elf_relro_section_is_transparent(const char *name, uint32_t type,
                                         uint64_t flags, uint64_t size) {
    (void) name;
    return size != 0U && type == LD_ELF_SHT_NOBITS &&
           (flags & (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE |
                     LD_ELF_SHF_TLS)) ==
                   (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE |
                    LD_ELF_SHF_TLS);
}

ld_elf_relro_result_t ld_elf_relro_add_section(
        ld_elf_relro_plan_t *plan, const char *name, uint32_t type,
        uint64_t flags, uint64_t file_offset, uint64_t address,
        uint64_t size, uint64_t file_size) {
    if (!plan || !name)
        return LD_ELF_RELRO_INVALID_ARGUMENT;
    if (plan->finalized) return LD_ELF_RELRO_FINALIZED;
    if (!ld_elf_relro_section_is_protected(name, type, flags, size))
        return LD_ELF_RELRO_SECTION_NOT_PROTECTED;
    if (file_size > size)
        return LD_ELF_RELRO_INVALID_ARGUMENT;

    uint64_t file_end;
    uint64_t address_end;
    if (ld_elf_relro_add_overflow(file_offset, file_size, &file_end) ||
        ld_elf_relro_add_overflow(address, size, &address_end)) {
        return LD_ELF_RELRO_LAYOUT_OVERFLOW;
    }
    if (!plan->present) {
        plan->file_offset = file_offset;
        plan->address = address;
        plan->present = true;
    } else if (file_offset < plan->file_end ||
               address < plan->address_end) {
        return LD_ELF_RELRO_NON_MONOTONIC_LAYOUT;
    }
    plan->file_end = file_end;
    plan->address_end = address_end;
    return LD_ELF_RELRO_OK;
}

ld_elf_relro_result_t ld_elf_relro_finalize(
        ld_elf_relro_plan_t *plan, uint64_t page_size,
        uint64_t file_cursor, uint64_t address_cursor,
        uint64_t *next_file_cursor, uint64_t *next_address_cursor) {
    if (!plan || !next_file_cursor || !next_address_cursor)
        return LD_ELF_RELRO_INVALID_ARGUMENT;
    if (plan->finalized) return LD_ELF_RELRO_FINALIZED;
    if (page_size == 0U || (page_size & (page_size - 1U)) != 0U)
        return LD_ELF_RELRO_INVALID_ALIGNMENT;
    if (!plan->present) {
        plan->finalized = true;
        *next_file_cursor = file_cursor;
        *next_address_cursor = address_cursor;
        return LD_ELF_RELRO_OK;
    }
    if (file_cursor < plan->file_end || address_cursor < plan->address_end)
        return LD_ELF_RELRO_NON_MONOTONIC_LAYOUT;
    if ((plan->file_offset & (page_size - 1U)) !=
        (plan->address & (page_size - 1U))) {
        return LD_ELF_RELRO_INCONGRUENT_LAYOUT;
    }

    uint64_t aligned_file;
    uint64_t aligned_address;
    if (!ld_elf_relro_align(file_cursor, page_size, &aligned_file) ||
        !ld_elf_relro_align(address_cursor, page_size, &aligned_address)) {
        return LD_ELF_RELRO_LAYOUT_OVERFLOW;
    }
    if (plan->file_end < plan->file_offset ||
        aligned_address < plan->address) {
        return LD_ELF_RELRO_LAYOUT_OVERFLOW;
    }
    plan->file_size = plan->file_end - plan->file_offset;
    plan->memory_size = aligned_address - plan->address;
    plan->protection_end = aligned_address;
    plan->finalized = true;
    *next_file_cursor = aligned_file;
    *next_address_cursor = aligned_address;
    return LD_ELF_RELRO_OK;
}

const char *ld_elf_relro_result_string(ld_elf_relro_result_t result) {
    switch (result) {
        case LD_ELF_RELRO_OK:
            return "ok";
        case LD_ELF_RELRO_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_RELRO_INVALID_ALIGNMENT:
            return "invalid page alignment";
        case LD_ELF_RELRO_SECTION_NOT_PROTECTED:
            return "section is not eligible for GNU RELRO";
        case LD_ELF_RELRO_NON_MONOTONIC_LAYOUT:
            return "non-monotonic section layout";
        case LD_ELF_RELRO_INCONGRUENT_LAYOUT:
            return "file offset and virtual address are incongruent";
        case LD_ELF_RELRO_LAYOUT_OVERFLOW:
            return "layout overflow";
        case LD_ELF_RELRO_FINALIZED:
            return "RELRO plan is already finalized";
    }
    return "unknown GNU RELRO error";
}
