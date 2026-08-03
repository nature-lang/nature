#include "ld_elf_dynamic.h"

#include "elf_format.h"

#include <stdlib.h>
#include <string.h>

static void ld_elf_dynamic_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_dynamic_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_dynamic_write_u32(bytes, (uint32_t) value);
    ld_elf_dynamic_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

uint32_t ld_elf_dynamic_relative_type(ld_arch_t arch) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return LD_ELF_R_X86_64_RELATIVE;
        case LD_ARCH_ARM64:
            return LD_ELF_R_AARCH64_RELATIVE;
        case LD_ARCH_RISCV64:
            return LD_ELF_R_RISCV_RELATIVE;
        default:
            return 0U;
    }
}

uint32_t ld_elf_dynamic_irelative_type(ld_arch_t arch) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return LD_ELF_R_X86_64_IRELATIVE;
        case LD_ARCH_ARM64:
            return LD_ELF_R_AARCH64_IRELATIVE;
        case LD_ARCH_RISCV64:
            return LD_ELF_R_RISCV_IRELATIVE;
        default:
            return 0U;
    }
}

static int ld_elf_dynamic_compare_relocations(const void *left,
                                              const void *right) {
    const ld_elf_dynamic_relocation_t *a = left;
    const ld_elf_dynamic_relocation_t *b = right;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->symbol != b->symbol) return a->symbol < b->symbol ? -1 : 1;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    if (a->type != b->type) return a->type < b->type ? -1 : 1;
    if (a->addend != b->addend) return a->addend < b->addend ? -1 : 1;
    return 0;
}

void ld_elf_dynamic_sort_relocations(
        ld_elf_dynamic_relocation_t *relocations, size_t count) {
    if (!relocations || count < 2U) return;
    qsort(relocations, count, sizeof(*relocations),
          ld_elf_dynamic_compare_relocations);
}

ld_elf_dynamic_result_t ld_elf_dynamic_encode_relocations(
        const ld_elf_dynamic_relocation_t *relocations, size_t count,
        uint8_t *output, size_t output_size) {
    if ((count != 0U && !relocations) || (output_size != 0U && !output))
        return LD_ELF_DYNAMIC_INVALID_ARGUMENT;
    if (count > SIZE_MAX / LD_ELF64_RELA_SIZE)
        return LD_ELF_DYNAMIC_OVERFLOW;
    size_t required = count * LD_ELF64_RELA_SIZE;
    if (output_size != required) return LD_ELF_DYNAMIC_BUFFER_TOO_SMALL;
    for (size_t i = 0; i < count; i++) {
        uint8_t *entry = output + i * LD_ELF64_RELA_SIZE;
        ld_elf_dynamic_write_u64(entry, relocations[i].offset);
        ld_elf_dynamic_write_u64(
                entry + 8U,
                LD_ELF_RELA_INFO(relocations[i].symbol,
                                 relocations[i].type));
        ld_elf_dynamic_write_u64(entry + 16U,
                                 (uint64_t) relocations[i].addend);
    }
    return LD_ELF_DYNAMIC_OK;
}

ld_elf_dynamic_result_t ld_elf_dynamic_encode_minimal_tables(
        uint8_t *dynsym, size_t dynsym_size, uint8_t *dynstr,
        size_t dynstr_size, uint8_t *hash, size_t hash_size,
        uint8_t *gnu_hash, size_t gnu_hash_size) {
    if (!dynsym || !dynstr || !hash || !gnu_hash)
        return LD_ELF_DYNAMIC_INVALID_ARGUMENT;
    if (dynsym_size != LD_ELF_DYNAMIC_MIN_DYNSYM_SIZE ||
        dynstr_size != LD_ELF_DYNAMIC_MIN_DYNSTR_SIZE ||
        hash_size != LD_ELF_DYNAMIC_MIN_HASH_SIZE ||
        gnu_hash_size != LD_ELF_DYNAMIC_MIN_GNU_HASH_SIZE) {
        return LD_ELF_DYNAMIC_BUFFER_TOO_SMALL;
    }

    memset(dynsym, 0, dynsym_size);
    dynstr[0] = 0U;

    /* SysV hash: one empty bucket and the mandatory null-symbol chain. */
    memset(hash, 0, hash_size);
    ld_elf_dynamic_write_u32(hash, 1U);
    ld_elf_dynamic_write_u32(hash + 4U, 1U);

    /*
     * GNU hash: one bucket, symbols start at dynsym index one, one 64-bit
     * bloom word, and no chain entries because only STN_UNDEF exists.
     */
    memset(gnu_hash, 0, gnu_hash_size);
    ld_elf_dynamic_write_u32(gnu_hash, 1U);
    ld_elf_dynamic_write_u32(gnu_hash + 4U, 1U);
    ld_elf_dynamic_write_u32(gnu_hash + 8U, 1U);
    /* Match Zig's ELF64 GNU-hash bloom shift even for an empty table. */
    ld_elf_dynamic_write_u32(gnu_hash + 12U, 26U);
    return LD_ELF_DYNAMIC_OK;
}

static size_t ld_elf_dynamic_entry_count(
        const ld_elf_dynamic_metadata_t *metadata) {
    size_t count = 9U; /* hash, gnu-hash, symtab/syment, strtab/strsz,
                         flags_1, debug, null */
    if (metadata->has_init) count++;
    if (metadata->has_fini) count++;
    if (metadata->has_init_array) count += 2U;
    if (metadata->has_fini_array) count += 2U;
    if (metadata->has_rela) count += 3U;
    if (metadata->has_static_tls) count++;
    return count;
}

ld_elf_dynamic_result_t ld_elf_dynamic_metadata_size(
        const ld_elf_dynamic_metadata_t *metadata, size_t *size) {
    if (!metadata || !size) return LD_ELF_DYNAMIC_INVALID_ARGUMENT;
    size_t count = ld_elf_dynamic_entry_count(metadata);
    if (count > SIZE_MAX / LD_ELF64_DYN_SIZE)
        return LD_ELF_DYNAMIC_OVERFLOW;
    *size = count * LD_ELF64_DYN_SIZE;
    return LD_ELF_DYNAMIC_OK;
}

static void ld_elf_dynamic_write_entry(uint8_t **cursor, uint64_t tag,
                                       uint64_t value) {
    ld_elf_dynamic_write_u64(*cursor, tag);
    ld_elf_dynamic_write_u64(*cursor + 8U, value);
    *cursor += LD_ELF64_DYN_SIZE;
}

ld_elf_dynamic_result_t ld_elf_dynamic_encode_metadata(
        const ld_elf_dynamic_metadata_t *metadata, uint8_t *output,
        size_t output_size) {
    if (!metadata || !output) return LD_ELF_DYNAMIC_INVALID_ARGUMENT;
    size_t required;
    ld_elf_dynamic_result_t result =
            ld_elf_dynamic_metadata_size(metadata, &required);
    if (result != LD_ELF_DYNAMIC_OK) return result;
    if (output_size != required) return LD_ELF_DYNAMIC_BUFFER_TOO_SMALL;

    uint8_t *cursor = output;
    if (metadata->has_init)
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_INIT,
                                   metadata->init_address);
    if (metadata->has_fini)
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_FINI,
                                   metadata->fini_address);
    if (metadata->has_init_array) {
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_INIT_ARRAY,
                                   metadata->init_array_address);
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_INIT_ARRAYSZ,
                                   metadata->init_array_size);
    }
    if (metadata->has_fini_array) {
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_FINI_ARRAY,
                                   metadata->fini_array_address);
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_FINI_ARRAYSZ,
                                   metadata->fini_array_size);
    }
    if (metadata->has_rela) {
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_RELA,
                                   metadata->rela_address);
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_RELASZ,
                                   metadata->rela_size);
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_RELAENT,
                                   LD_ELF64_RELA_SIZE);
    }
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_HASH,
                               metadata->hash_address);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_GNU_HASH,
                               metadata->gnu_hash_address);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_SYMTAB,
                               metadata->dynsym_address);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_SYMENT,
                               LD_ELF64_SYM_SIZE);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_STRTAB,
                               metadata->dynstr_address);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_STRSZ,
                               metadata->dynstr_size);
    if (metadata->has_static_tls)
        ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_FLAGS,
                                   LD_ELF_DF_STATIC_TLS);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_FLAGS_1,
                               LD_ELF_DF_1_PIE);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_DEBUG, 0U);
    ld_elf_dynamic_write_entry(&cursor, LD_ELF_DT_NULL, 0U);
    return (size_t) (cursor - output) == required
                   ? LD_ELF_DYNAMIC_OK
                   : LD_ELF_DYNAMIC_OVERFLOW;
}

const char *ld_elf_dynamic_result_string(ld_elf_dynamic_result_t result) {
    switch (result) {
        case LD_ELF_DYNAMIC_OK:
            return "success";
        case LD_ELF_DYNAMIC_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_DYNAMIC_OVERFLOW:
            return "integer overflow";
        case LD_ELF_DYNAMIC_BUFFER_TOO_SMALL:
            return "output buffer has the wrong size";
        case LD_ELF_DYNAMIC_UNSUPPORTED_ARCH:
            return "unsupported architecture";
        default:
            return "unknown dynamic metadata error";
    }
}
