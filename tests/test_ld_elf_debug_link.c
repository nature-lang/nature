#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_debug_link_write_header(uint8_t *bytes, uint16_t section_count,
                                         uint16_t section_names_index,
                                         size_t section_table_offset) {
    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_AARCH64);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, section_names_index);
}

static void test_debug_link_write_start_symbol(uint8_t *symbol,
                                               uint16_t section_index) {
    test_elf_write_u32(symbol, 1U);
    symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(symbol + 6U, section_index);
    test_elf_write_u64(symbol + 16U, sizeof(uint32_t));
}

/* zlib level-9 encoding of 256 zero bytes. */
static const uint8_t test_debug_link_compressed_zeros[12] = {
        0x78U,
        0xdaU,
        0x63U,
        0x60U,
        0x18U,
        0xd9U,
        0x00U,
        0x00U,
        0x01U,
        0x00U,
        0x00U,
        0x01U,
};

static void test_debug_link_write_compressed_zeros(uint8_t *bytes) {
    test_elf_write_u32(bytes, LD_ELF_COMPRESS_ZLIB);
    test_elf_write_u32(bytes + 4U, 0U);
    test_elf_write_u64(bytes + 8U, 256U);
    test_elf_write_u64(bytes + 16U, 1U);
    memcpy(bytes + LD_ELF64_CHDR_SIZE,
           test_debug_link_compressed_zeros,
           sizeof(test_debug_link_compressed_zeros));
}

static uint8_t *test_debug_link_make_stripped_relocation_object(
        size_t *result_size) {
    enum {
        text_index = 1,
        debug_index = 2,
        rela_index = 3,
        symtab_index = 4,
        strtab_index = 5,
        shstrtab_index = 6,
        section_count = 7,
        symbol_count = 2,
    };
    static const char symbol_names[] = "\0_start";
    char section_names[128] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t debug_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_info");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.debug_info");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t compressed_size =
            LD_ELF64_CHDR_SIZE + sizeof(test_debug_link_compressed_zeros);
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t debug_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t rela_offset =
            test_elf_align(debug_offset + compressed_size, 8U);
    const size_t symtab_offset =
            test_elf_align(rela_offset + LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    test_debug_link_write_header(bytes, section_count, shstrtab_index,
                                 section_table_offset);
    test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
    test_debug_link_write_compressed_zeros(bytes + debug_offset);
    test_elf_write_u64(bytes + rela_offset, 128U);
    test_elf_write_u64(bytes + rela_offset + 8U,
                       ((uint64_t) 1U << 32U) |
                               LD_ELF_R_AARCH64_ABS64);
    test_elf_write_u64(bytes + rela_offset + 16U, 0U);
    test_debug_link_write_start_symbol(
            bytes + symtab_offset + LD_ELF64_SYM_SIZE, text_index);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, 4U, 0U,
            0U, 4U, 0U);
    test_elf_write_section(
            sections + debug_index * LD_ELF64_SHDR_SIZE, debug_name,
            LD_ELF_SHT_PROGBITS, LD_ELF_SHF_COMPRESSED, debug_offset,
            compressed_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset, LD_ELF64_RELA_SIZE,
            symtab_index, debug_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *test_debug_link_make_unnamed_compressed_object(
        size_t *result_size) {
    enum {
        text_index = 1,
        compressed_index = 2,
        symtab_index = 3,
        strtab_index = 4,
        section_count = 5,
        symbol_count = 2,
    };
    static const char symbol_names[] = "\0_start";
    const size_t compressed_size =
            LD_ELF64_CHDR_SIZE + sizeof(test_debug_link_compressed_zeros);
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t compressed_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t symtab_offset =
            test_elf_align(compressed_offset + compressed_size, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_table_offset =
            test_elf_align(strtab_offset + sizeof(symbol_names), 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    test_debug_link_write_header(bytes, section_count, LD_ELF_SHN_UNDEF,
                                 section_table_offset);
    test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
    test_debug_link_write_compressed_zeros(bytes + compressed_offset);
    test_debug_link_write_start_symbol(
            bytes + symtab_offset + LD_ELF64_SYM_SIZE, text_index);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, 0U,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, 4U, 0U,
            0U, 4U, 0U);
    test_elf_write_section(
            sections + compressed_index * LD_ELF64_SHDR_SIZE, 0U,
            LD_ELF_SHT_PROGBITS, LD_ELF_SHF_COMPRESSED, compressed_offset,
            compressed_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, 0U,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, 0U,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *test_debug_link_make_named_section_object(
        const char *section_name, uint32_t section_type,
        uint64_t section_flags, size_t section_alignment, uint32_t fill,
        bool define_start, size_t *result_size) {
    enum {
        text_index = 1,
        named_index = 2,
        symtab_index = 3,
        strtab_index = 4,
        shstrtab_index = 5,
        section_count = 6,
    };
    static const char symbol_names[] = "\0_start";
    char section_names[128] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t named_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            section_name);
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_size = define_start ? sizeof(uint32_t) : 0U;
    const size_t named_file_size =
            section_type == LD_ELF_SHT_NOBITS ? 0U : sizeof(uint32_t);
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t named_offset = test_elf_align(
            text_offset + text_size, section_alignment);
    const size_t symbol_count = define_start ? 2U : 1U;
    const size_t symtab_offset =
            test_elf_align(named_offset + named_file_size, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    test_debug_link_write_header(bytes, section_count, shstrtab_index,
                                 section_table_offset);
    if (define_start) {
        test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
        test_debug_link_write_start_symbol(
                bytes + symtab_offset + LD_ELF64_SYM_SIZE, text_index);
    }
    if (named_file_size != 0U)
        test_elf_write_u32(bytes + named_offset, fill);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, text_size,
            0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + named_index * LD_ELF64_SHDR_SIZE, named_name,
            section_type, section_flags, named_offset, sizeof(uint32_t), 0U,
            0U, section_alignment, 0U);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *test_debug_link_make_empty_none_relocation_object(
        uint16_t machine, uint32_t none_type, size_t *result_size) {
    enum {
        text_index = 1,
        debug_index = 2,
        rela_index = 3,
        symtab_index = 4,
        strtab_index = 5,
        shstrtab_index = 6,
        section_count = 7,
        symbol_count = 2,
    };
    static const char symbol_names[] = "\0_start";
    char section_names[128] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t debug_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_info");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.debug_info");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t debug_offset = text_offset + sizeof(uint32_t);
    const size_t rela_offset = test_elf_align(debug_offset, 8U);
    const size_t symtab_offset = rela_offset + LD_ELF64_RELA_SIZE;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    test_debug_link_write_header(bytes, section_count, shstrtab_index,
                                 section_table_offset);
    test_elf_write_u16(bytes + 18U, machine);
    test_debug_link_write_start_symbol(
            bytes + symtab_offset + LD_ELF64_SYM_SIZE, text_index);
    test_elf_write_u64(bytes + rela_offset, 0U);
    test_elf_write_u64(bytes + rela_offset + 8U, none_type);
    test_elf_write_u64(bytes + rela_offset + 16U, UINT64_MAX);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + debug_index * LD_ELF64_SHDR_SIZE, debug_name,
            LD_ELF_SHT_PROGBITS, 0U, debug_offset, 0U, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset, LD_ELF64_RELA_SIZE,
            symtab_index, debug_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

typedef struct {
    const uint8_t *header;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint64_t alignment;
} test_debug_link_output_section_t;

static size_t test_debug_link_find_output_sections(
        const uint8_t *image, size_t image_size, const char *name,
        test_debug_link_output_section_t *results, size_t capacity) {
    assert(image_size >= LD_ELF64_EHDR_SIZE);
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t entry_size = test_elf_read_u16(image + 58U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    uint16_t names_index = test_elf_read_u16(image + 62U);
    assert(entry_size == LD_ELF64_SHDR_SIZE);
    assert(names_index < section_count);
    assert(table_offset <= image_size);
    assert((uint64_t) section_count * entry_size <=
           image_size - table_offset);
    const uint8_t *table = image + (size_t) table_offset;
    const uint8_t *names_header =
            table + (size_t) names_index * entry_size;
    uint64_t names_offset = test_elf_read_u64(names_header + 24U);
    uint64_t names_size = test_elf_read_u64(names_header + 32U);
    assert(names_offset <= image_size &&
           names_size <= image_size - names_offset);
    const char *names = (const char *) image + (size_t) names_offset;

    size_t count = 0U;
    for (uint16_t i = 1U; i < section_count; i++) {
        const uint8_t *section = table + (size_t) i * entry_size;
        uint32_t name_offset = test_elf_read_u32(section);
        assert(name_offset < names_size);
        assert(memchr(names + name_offset, '\0',
                      (size_t) names_size - name_offset) != NULL);
        if (strcmp(names + name_offset, name) != 0) continue;
        assert(count < capacity);
        results[count].header = section;
        results[count].type = test_elf_read_u32(section + 4U);
        results[count].flags = test_elf_read_u64(section + 8U);
        results[count].address = test_elf_read_u64(section + 16U);
        results[count].offset = test_elf_read_u64(section + 24U);
        results[count].size = test_elf_read_u64(section + 32U);
        results[count].alignment = test_elf_read_u64(section + 48U);
        count++;
    }
    return count;
}

static uint64_t test_debug_link_loaded_file_end(
        const uint8_t *image, size_t image_size) {
    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t entry_size = test_elf_read_u16(image + 54U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    assert(entry_size == LD_ELF64_PHDR_SIZE);
    assert(program_offset <= image_size);
    assert((uint64_t) program_count * entry_size <=
           image_size - program_offset);
    uint64_t result = 0U;
    for (uint16_t i = 0U; i < program_count; i++) {
        const uint8_t *program = image + (size_t) program_offset +
                                 (size_t) i * entry_size;
        if (test_elf_read_u32(program) != LD_ELF_PT_LOAD) continue;
        uint64_t offset = test_elf_read_u64(program + 8U);
        uint64_t size = test_elf_read_u64(program + 32U);
        assert(offset <= UINT64_MAX - size);
        uint64_t end = offset + size;
        if (end > result) result = end;
    }
    return result;
}

static bool test_debug_link_intersects_load_segment(
        const uint8_t *image, size_t image_size,
        const test_debug_link_output_section_t *section) {
    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t entry_size = test_elf_read_u16(image + 54U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    assert(entry_size == LD_ELF64_PHDR_SIZE);
    assert(program_offset <= image_size);
    assert((uint64_t) program_count * entry_size <=
           image_size - program_offset);
    uint64_t section_end = section->offset + section->size;
    assert(section_end >= section->offset);
    for (uint16_t i = 0U; i < program_count; i++) {
        const uint8_t *program = image + (size_t) program_offset +
                                 (size_t) i * entry_size;
        if (test_elf_read_u32(program) != LD_ELF_PT_LOAD) continue;
        uint64_t load_offset = test_elf_read_u64(program + 8U);
        uint64_t load_size = test_elf_read_u64(program + 32U);
        uint64_t load_end = load_offset + load_size;
        assert(load_end >= load_offset);
        if (section->offset < load_end && load_offset < section_end)
            return true;
    }
    return false;
}

static void test_debug_link_default_strip_skips_relocations(void) {
    size_t object_size;
    uint8_t *object =
            test_debug_link_make_stripped_relocation_object(&object_size);
    char object_path[] = "/tmp/nature-ld-debug-strip-reloc-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-debug-strip-reloc-output";
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(test_elf_find_output_section(image, image_size, ".debug_info") ==
           NULL);
    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_debug_link_dwarf_decompresses_and_lays_out(void) {
    size_t object_size;
    uint8_t *object =
            test_debug_link_make_stripped_relocation_object(&object_size);
    char object_path[] = "/tmp/nature-ld-debug-dwarf-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-debug-dwarf-output";
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs_configured(
                   output_path, inputs, 1U, LD_ARCH_ARM64, false,
                   LD_DEBUG_DWARF, true, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    test_debug_link_output_section_t debug;
    test_debug_link_output_section_t symtab;
    assert(test_debug_link_find_output_sections(
                   image, image_size, ".debug_info", &debug, 1U) == 1U);
    assert(test_debug_link_find_output_sections(
                   image, image_size, ".symtab", &symtab, 1U) == 1U);
    assert(debug.type == LD_ELF_SHT_PROGBITS);
    assert((debug.flags &
            (LD_ELF_SHF_ALLOC | LD_ELF_SHF_COMPRESSED)) == 0U);
    assert(debug.address == 0U && debug.size == 256U &&
           debug.alignment == 1U);
    assert(debug.offset >= test_debug_link_loaded_file_end(image,
                                                           image_size));
    assert(debug.offset <= UINT64_MAX - debug.size);
    assert(debug.offset + debug.size <= symtab.offset);
    assert(!test_debug_link_intersects_load_segment(image, image_size,
                                                    &debug));
    assert(debug.offset <= image_size &&
           debug.size <= image_size - debug.offset);
    assert(test_elf_read_u64(image + (size_t) debug.offset + 128U) ==
           test_elf_read_u64(image + 24U));

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_debug_link_merges_same_name_with_max_alignment(void) {
    size_t first_size, second_size;
    uint8_t *first = test_debug_link_make_named_section_object(
            ".debug_info", LD_ELF_SHT_PROGBITS, 0U, 1U,
            UINT32_C(0x44332211), true, &first_size);
    uint8_t *second = test_debug_link_make_named_section_object(
            ".debug_info", LD_ELF_SHT_PROGBITS, 0U, 16U,
            UINT32_C(0x88776655), false, &second_size);
    char first_path[] = "/tmp/nature-ld-debug-align-first-XXXXXX";
    char second_path[] = "/tmp/nature-ld-debug-align-second-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path = "/tmp/nature-ld-debug-align-output";
    const char *inputs[] = {first_path, second_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs_configured(
                   output_path, inputs, 2U, LD_ARCH_ARM64, false,
                   LD_DEBUG_DWARF, true, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    test_debug_link_output_section_t debug[2];
    assert(test_debug_link_find_output_sections(
                   image, image_size, ".debug_info", debug, 2U) == 1U);
    assert(debug[0].type == LD_ELF_SHT_PROGBITS);
    assert((debug[0].flags & LD_ELF_SHF_ALLOC) == 0U);
    assert(debug[0].address == 0U);
    assert(debug[0].alignment == 16U);
    assert(debug[0].size == 20U);
    assert(debug[0].offset >= test_debug_link_loaded_file_end(
                                      image, image_size));
    assert(!test_debug_link_intersects_load_segment(
            image, image_size, &debug[0]));
    assert(debug[0].offset <= image_size &&
           debug[0].size <= image_size - debug[0].offset);

    const uint8_t *contents = image + (size_t) debug[0].offset;
    assert(test_elf_read_u32(contents) == UINT32_C(0x44332211));
    for (size_t i = sizeof(uint32_t); i < 16U; i++)
        assert(contents[i] == 0U);
    assert(test_elf_read_u32(contents + 16U) == UINT32_C(0x88776655));

    free(image);
    unlink(first_path);
    unlink(second_path);
    unlink(output_path);
}

static void test_debug_link_none_relocation_on_empty_section(void) {
    static const uint8_t sentinel[] = {
            0x4eU,
            0x4fU,
            0x4eU,
            0x45U,
    };
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t none_type;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64,
             LD_ELF_R_X86_64_NONE},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
             LD_ELF_R_AARCH64_NONE},
            {LD_ARCH_RISCV64, LD_ELF_EM_RISCV,
             LD_ELF_R_RISCV_NONE},
    };

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object =
                test_debug_link_make_empty_none_relocation_object(
                        cases[i].machine, cases[i].none_type,
                        &object_size);
        char object_path[] = "/tmp/nature-ld-debug-none-XXXXXX";
        char output_path[] = "/tmp/nature-ld-debug-none-out-XXXXXX";
        write_fixture(object_path, object, object_size);
        write_fixture(output_path, sentinel, sizeof(sentinel));
        free(object);

        const char *inputs[] = {object_path};
        diagnostic_capture_t capture = {0};
        assert(link_test_elf_inputs_configured(
                       output_path, inputs, 1U, cases[i].arch, false,
                       LD_DEBUG_DWARF, false, &capture) == LD_OK);
        assert(capture.count == 0U);

        size_t image_size;
        mode_t image_mode;
        uint8_t *image = read_test_fixture(
                output_path, &image_size, &image_mode);
        assert(image_size >= LD_ELF64_EHDR_SIZE);
        assert(image[0] == LD_ELF_MAGIC_0 &&
               image[1] == LD_ELF_MAGIC_1 &&
               image[2] == LD_ELF_MAGIC_2 &&
               image[3] == LD_ELF_MAGIC_3);
        assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_EXEC);
        assert(test_elf_read_u16(image + 18U) == cases[i].machine);
        assert((image_mode & 0777) == 0755);
        assert(image_size != sizeof(sentinel) ||
               memcmp(image, sentinel, sizeof(sentinel)) != 0);

        test_debug_link_output_section_t debug;
        assert(test_debug_link_find_output_sections(
                       image, image_size, ".debug_info", &debug, 1U) ==
               1U);
        assert(debug.type == LD_ELF_SHT_PROGBITS);
        assert((debug.flags & LD_ELF_SHF_ALLOC) == 0U);
        assert(debug.address == 0U && debug.size == 0U);
        assert(debug.offset <= image_size);

        free(image);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_debug_link_invalid_chdr_is_atomic(void) {
    static const uint8_t sentinel[] = {
            0x44U,
            0x57U,
            0x41U,
            0x52U,
            0x46U,
            0xa5U,
    };
    static const struct {
        int expected;
        const char *message;
    } cases[] = {
            {LD_UNSUPPORTED, "compression type 2"},
            {LD_INVALID_INPUT, "invalid ch_addralign 3"},
            {LD_INVALID_INPUT, "invalid ch_addralign 0"},
            {LD_INVALID_INPUT, "truncated Elf64_Chdr"},
            {LD_INVALID_INPUT, "non-zero ch_reserved"},
    };
    for (unsigned case_index = 0U;
         case_index < sizeof(cases) / sizeof(cases[0]); case_index++) {
        size_t object_size;
        uint8_t *object = test_debug_link_make_stripped_relocation_object(
                &object_size);
        uint64_t section_table_offset = test_elf_read_u64(object + 40U);
        uint8_t *debug_header =
                object + (size_t) section_table_offset +
                2U * LD_ELF64_SHDR_SIZE;
        uint64_t debug_offset = test_elf_read_u64(debug_header + 24U);
        assert(debug_offset <= object_size &&
               LD_ELF64_CHDR_SIZE <= object_size - debug_offset);
        if (case_index == 0U)
            test_elf_write_u32(object + (size_t) debug_offset, 2U);
        else if (case_index == 1U)
            test_elf_write_u64(object + (size_t) debug_offset + 16U, 3U);
        else if (case_index == 2U)
            test_elf_write_u64(object + (size_t) debug_offset + 16U, 0U);
        else if (case_index == 3U)
            test_elf_write_u64(debug_header + 32U,
                               LD_ELF64_CHDR_SIZE - 1U);
        else
            test_elf_write_u32(object + (size_t) debug_offset + 4U, 1U);

        char object_path[] = "/tmp/nature-ld-debug-chdr-XXXXXX";
        char output_path[] = "/tmp/nature-ld-debug-chdr-out-XXXXXX";
        write_fixture(object_path, object, object_size);
        write_fixture(output_path, sentinel, sizeof(sentinel));
        free(object);

        const char *inputs[] = {object_path};
        diagnostic_capture_t capture = {0};
        assert(link_test_elf_inputs_configured(
                       output_path, inputs, 1U, LD_ARCH_ARM64, false,
                       LD_DEBUG_DWARF, false, &capture) ==
               cases[case_index].expected);
        assert(capture.count > 0U);
        assert(strstr(capture.message, cases[case_index].message) != NULL);
        size_t output_size;
        uint8_t *output =
                read_test_fixture(output_path, &output_size, NULL);
        assert(output_size == sizeof(sentinel));
        assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
        free(output);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_debug_link_skips_filtered_compressed_sections(void) {
    static const uint8_t sentinel[] = {
            0x53U,
            0x4bU,
            0x49U,
            0x50U,
    };
    for (unsigned case_index = 0U; case_index < 2U; case_index++) {
        size_t object_size;
        uint8_t *object = test_debug_link_make_stripped_relocation_object(
                &object_size);
        uint64_t section_table_offset = test_elf_read_u64(object + 40U);
        uint16_t section_names_index = test_elf_read_u16(object + 62U);
        assert(section_table_offset <= object_size);

        uint8_t *debug_header =
                object + (size_t) section_table_offset +
                2U * LD_ELF64_SHDR_SIZE;
        uint64_t debug_offset = test_elf_read_u64(debug_header + 24U);
        assert(debug_offset <= object_size &&
               LD_ELF64_CHDR_SIZE <= object_size - debug_offset);
        test_elf_write_u32(object + (size_t) debug_offset, 2U);

        if (case_index == 0U) {
            uint64_t flags = test_elf_read_u64(debug_header + 8U);
            test_elf_write_u64(debug_header + 8U,
                               flags | LD_ELF_SHF_EXCLUDE);
        } else {
            const uint8_t *section_names_header =
                    object + (size_t) section_table_offset +
                    (size_t) section_names_index * LD_ELF64_SHDR_SIZE;
            uint64_t section_names_offset =
                    test_elf_read_u64(section_names_header + 24U);
            uint32_t debug_name_offset = test_elf_read_u32(debug_header);
            assert(section_names_offset <= object_size &&
                   debug_name_offset <=
                           object_size - (size_t) section_names_offset);
            char *debug_name =
                    (char *) object + (size_t) section_names_offset +
                    debug_name_offset;
            assert(strcmp(debug_name, ".debug_info") == 0);
            memcpy(debug_name, ".note.skip", sizeof(".note.skip"));
        }

        char object_path[] = "/tmp/nature-ld-debug-skip-XXXXXX";
        char output_path[] = "/tmp/nature-ld-debug-skip-out-XXXXXX";
        write_fixture(object_path, object, object_size);
        write_fixture(output_path, sentinel, sizeof(sentinel));
        free(object);

        const char *inputs[] = {object_path};
        diagnostic_capture_t capture = {0};
        assert(link_test_elf_inputs_configured(
                       output_path, inputs, 1U, LD_ARCH_ARM64, false,
                       LD_DEBUG_DWARF, false, &capture) == LD_OK);
        assert(capture.count == 0U);

        size_t output_size;
        uint8_t *output =
                read_test_fixture(output_path, &output_size, NULL);
        assert(output_size >= LD_ELF64_EHDR_SIZE);
        assert(output[0] == LD_ELF_MAGIC_0 &&
               output[1] == LD_ELF_MAGIC_1 &&
               output[2] == LD_ELF_MAGIC_2 &&
               output[3] == LD_ELF_MAGIC_3);
        assert(test_elf_find_output_section(
                       output, output_size,
                       case_index == 0U ? ".debug_info" : ".note.skip") ==
               NULL);
        free(output);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_debug_link_unnamed_compressed_section_is_rejected(void) {
    size_t object_size;
    uint8_t *object =
            test_debug_link_make_unnamed_compressed_object(&object_size);
    char object_path[] = "/tmp/nature-ld-debug-unnamed-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-debug-unnamed-output";
    unlink(output_path);
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) ==
           LD_UNSUPPORTED);
    assert(capture.count != 0U);
    assert(strstr(capture.message, "unsupported compressed ELF section") !=
           NULL);
    errno = 0;
    assert(access(output_path, F_OK) != 0 && errno == ENOENT);

    unlink(object_path);
}

static void test_debug_link_output_section_keys_do_not_collide(void) {
    size_t alloc_size, nonalloc_size, progbits_size, nobits_size;
    uint8_t *alloc = test_debug_link_make_named_section_object(
            ".collision", LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC, 4U,
            UINT32_C(0x11111111), true, &alloc_size);
    uint8_t *nonalloc = test_debug_link_make_named_section_object(
            ".collision", LD_ELF_SHT_PROGBITS, 0U, 4U,
            UINT32_C(0x22222222), false, &nonalloc_size);
    uint8_t *progbits = test_debug_link_make_named_section_object(
            ".typed", LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, 4U,
            UINT32_C(0x33333333), false, &progbits_size);
    uint8_t *nobits = test_debug_link_make_named_section_object(
            ".typed", LD_ELF_SHT_NOBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, 4U, 0U, false,
            &nobits_size);

    char alloc_path[] = "/tmp/nature-ld-debug-key-alloc-XXXXXX";
    char nonalloc_path[] = "/tmp/nature-ld-debug-key-nonalloc-XXXXXX";
    char progbits_path[] = "/tmp/nature-ld-debug-key-progbits-XXXXXX";
    char nobits_path[] = "/tmp/nature-ld-debug-key-nobits-XXXXXX";
    write_fixture(alloc_path, alloc, alloc_size);
    write_fixture(nonalloc_path, nonalloc, nonalloc_size);
    write_fixture(progbits_path, progbits, progbits_size);
    write_fixture(nobits_path, nobits, nobits_size);
    free(alloc);
    free(nonalloc);
    free(progbits);
    free(nobits);

    static const char *output_path = "/tmp/nature-ld-debug-key-output";
    const char *inputs[] = {
            alloc_path,
            nonalloc_path,
            progbits_path,
            nobits_path,
    };
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 4U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    test_debug_link_output_section_t collision[2];
    assert(test_debug_link_find_output_sections(
                   image, image_size, ".collision", collision, 2U) == 2U);
    const test_debug_link_output_section_t *allocated = NULL;
    const test_debug_link_output_section_t *nonallocated = NULL;
    for (size_t i = 0U; i < 2U; i++) {
        assert(collision[i].type == LD_ELF_SHT_PROGBITS);
        if ((collision[i].flags & LD_ELF_SHF_ALLOC) != 0U)
            allocated = &collision[i];
        else
            nonallocated = &collision[i];
    }
    assert(allocated != NULL && nonallocated != NULL);
    assert(allocated->address != 0U);
    assert(nonallocated->address == 0U);
    assert(!test_debug_link_intersects_load_segment(
            image, image_size, nonallocated));

    test_debug_link_output_section_t typed[2];
    assert(test_debug_link_find_output_sections(
                   image, image_size, ".typed", typed, 2U) == 2U);
    bool found_progbits = false;
    bool found_nobits = false;
    for (size_t i = 0U; i < 2U; i++) {
        assert((typed[i].flags &
                (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE)) ==
               (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE));
        found_progbits |= typed[i].type == LD_ELF_SHT_PROGBITS;
        found_nobits |= typed[i].type == LD_ELF_SHT_NOBITS;
    }
    assert(found_progbits && found_nobits);

    free(image);
    unlink(alloc_path);
    unlink(nonalloc_path);
    unlink(progbits_path);
    unlink(nobits_path);
    unlink(output_path);
}

void test_ld_elf_debug_link(void) {
    test_debug_link_default_strip_skips_relocations();
    test_debug_link_dwarf_decompresses_and_lays_out();
    test_debug_link_merges_same_name_with_max_alignment();
    test_debug_link_none_relocation_on_empty_section();
    test_debug_link_invalid_chdr_is_atomic();
    test_debug_link_skips_filtered_compressed_sections();
    test_debug_link_unnamed_compressed_section_is_rejected();
    test_debug_link_output_section_keys_do_not_collide();
}

#ifdef LD_ELF_DEBUG_LINK_STANDALONE_TEST
int main(void) {
    test_ld_elf_debug_link();
    return 0;
}
#endif
