#include "ld_elf_symtab.h"

#include "elf_format.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * The local-before-global ordering, section-symbol convention, sh_info
 * boundary, and linked string-table model follow Elf.updateSymtabSize and
 * Elf.writeSymtab from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. Nature encodes the ELF64 wire
 * bytes explicitly so the module also works on a non-Linux or big-endian
 * host. Zig is MIT licensed; see ZIG-LICENSE.txt.
 */

static void ld_elf_symtab_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void ld_elf_symtab_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_symtab_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_symtab_write_u32(bytes, (uint32_t) value);
    ld_elf_symtab_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static bool ld_elf_symtab_add_size(size_t left, size_t right,
                                   size_t *result) {
    if (left > SIZE_MAX - right) return false;
    *result = left + right;
    return true;
}

void ld_elf_symtab_init(ld_elf_symtab_t *table) {
    if (table) memset(table, 0, sizeof(*table));
}

void ld_elf_symtab_deinit(ld_elf_symtab_t *table) {
    if (!table) return;
    free(table->symbols);
    free(table->strings);
    ld_elf_symtab_init(table);
}

static ld_elf_symtab_result_t ld_elf_symtab_measure_strings(
        const ld_elf_symtab_entry_t *entries, size_t count,
        size_t input_index_base, size_t *strings_size,
        size_t *error_entry_index) {
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].name) {
            if (error_entry_index) *error_entry_index = input_index_base + i;
            return LD_ELF_SYMTAB_INVALID_ARGUMENT;
        }
        if (!entries[i].name[0]) continue;
        size_t name_size = strlen(entries[i].name);
        if (name_size == SIZE_MAX) {
            if (error_entry_index) *error_entry_index = input_index_base + i;
            return LD_ELF_SYMTAB_STRING_SIZE_OVERFLOW;
        }
        name_size++;
        size_t next_size;
        if (!ld_elf_symtab_add_size(*strings_size, name_size, &next_size) ||
            next_size > UINT32_MAX) {
            if (error_entry_index) *error_entry_index = input_index_base + i;
            return LD_ELF_SYMTAB_STRING_SIZE_OVERFLOW;
        }
        *strings_size = next_size;
    }
    return LD_ELF_SYMTAB_OK;
}

static ld_elf_symtab_result_t ld_elf_symtab_write_entries(
        const ld_elf_symtab_entry_t *entries, size_t count,
        size_t input_index_base, size_t symbol_index_base,
        ld_elf_symtab_t *table, size_t *string_cursor,
        size_t *error_entry_index) {
    for (size_t i = 0; i < count; i++) {
        const ld_elf_symtab_entry_t *entry = &entries[i];
        if (entry->binding > 0x0fU || entry->type > 0x0fU) {
            if (error_entry_index) *error_entry_index = input_index_base + i;
            return LD_ELF_SYMTAB_INVALID_INFO;
        }
        uint32_t name_offset = 0U;
        if (entry->name[0]) {
            name_offset = (uint32_t) *string_cursor;
            size_t name_size = strlen(entry->name) + 1U;
            memcpy(table->strings + *string_cursor, entry->name, name_size);
            *string_cursor += name_size;
        }
        uint8_t *symbol = table->symbols +
                          (symbol_index_base + i) * LD_ELF64_SYM_SIZE;
        ld_elf_symtab_write_u32(symbol, name_offset);
        symbol[4] = (uint8_t) ((entry->binding << 4U) | entry->type);
        symbol[5] = entry->other;
        ld_elf_symtab_write_u16(symbol + 6U, entry->section_index);
        ld_elf_symtab_write_u64(symbol + 8U, entry->value);
        ld_elf_symtab_write_u64(symbol + 16U, entry->size);
    }
    return LD_ELF_SYMTAB_OK;
}

ld_elf_symtab_result_t ld_elf_symtab_build(
        const ld_elf_symtab_entry_t *locals, size_t local_count,
        const ld_elf_symtab_entry_t *globals, size_t global_count,
        ld_elf_symtab_t *table, size_t *error_entry_index) {
    if (error_entry_index) *error_entry_index = SIZE_MAX;
    if (!table || (local_count && !locals) || (global_count && !globals))
        return LD_ELF_SYMTAB_INVALID_ARGUMENT;
    ld_elf_symtab_init(table);
    if (local_count > (size_t) UINT32_MAX - 1U ||
        global_count > SIZE_MAX - 1U ||
        local_count > SIZE_MAX - 1U - global_count) {
        return LD_ELF_SYMTAB_SYMBOL_COUNT_OVERFLOW;
    }
    size_t symbol_count = local_count + global_count + 1U;
    if (symbol_count > SIZE_MAX / LD_ELF64_SYM_SIZE)
        return LD_ELF_SYMTAB_SYMBOL_SIZE_OVERFLOW;

    size_t strings_size = 1U;
    ld_elf_symtab_result_t result = ld_elf_symtab_measure_strings(
            locals, local_count, 0U, &strings_size, error_entry_index);
    if (result == LD_ELF_SYMTAB_OK) {
        result = ld_elf_symtab_measure_strings(
                globals, global_count, local_count, &strings_size,
                error_entry_index);
    }
    if (result != LD_ELF_SYMTAB_OK) return result;

    size_t symbols_size = symbol_count * LD_ELF64_SYM_SIZE;
    uint8_t *symbols = calloc(1, symbols_size);
    char *strings = calloc(1, strings_size);
    if (!symbols || !strings) {
        free(symbols);
        free(strings);
        return LD_ELF_SYMTAB_OUT_OF_MEMORY;
    }
    table->symbols = symbols;
    table->symbols_size = symbols_size;
    table->strings = strings;
    table->strings_size = strings_size;
    table->symbol_count = symbol_count;
    table->first_global = (uint32_t) local_count + 1U;

    size_t string_cursor = 1U;
    result = ld_elf_symtab_write_entries(
            locals, local_count, 0U, 1U, table, &string_cursor,
            error_entry_index);
    if (result == LD_ELF_SYMTAB_OK) {
        result = ld_elf_symtab_write_entries(
                globals, global_count, local_count, 1U + local_count, table,
                &string_cursor, error_entry_index);
    }
    if (result != LD_ELF_SYMTAB_OK) {
        ld_elf_symtab_deinit(table);
        return result;
    }
    return LD_ELF_SYMTAB_OK;
}

const char *ld_elf_symtab_result_string(ld_elf_symtab_result_t result) {
    switch (result) {
        case LD_ELF_SYMTAB_OK:
            return "success";
        case LD_ELF_SYMTAB_INVALID_ARGUMENT:
            return "invalid symbol-table argument";
        case LD_ELF_SYMTAB_SYMBOL_COUNT_OVERFLOW:
            return "ELF symbol count overflow";
        case LD_ELF_SYMTAB_SYMBOL_SIZE_OVERFLOW:
            return "ELF64 symbol-table size overflow";
        case LD_ELF_SYMTAB_STRING_SIZE_OVERFLOW:
            return "ELF string-table size exceeds the 32-bit st_name limit";
        case LD_ELF_SYMTAB_INVALID_INFO:
            return "ELF symbol binding or type exceeds four bits";
        case LD_ELF_SYMTAB_OUT_OF_MEMORY:
            return "out of memory building ELF symbol table";
    }
    return "unknown ELF symbol-table error";
}
