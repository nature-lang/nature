#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void debug_merge_write_header(uint8_t *bytes, uint16_t machine,
                                     uint16_t section_count,
                                     uint16_t names_index,
                                     size_t section_table_offset) {
    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, machine);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, names_index);
}

static void debug_merge_write_symbol(uint8_t *symbol, uint32_t name,
                                     uint8_t binding, uint8_t type,
                                     uint16_t section, uint64_t value,
                                     uint64_t size) {
    test_elf_write_u32(symbol, name);
    symbol[4] = LD_ELF_SYM_INFO(binding, type);
    test_elf_write_u16(symbol + 6U, section);
    test_elf_write_u64(symbol + 8U, value);
    test_elf_write_u64(symbol + 16U, size);
}

static uint8_t *debug_merge_make_x86_object(size_t *result_size) {
    enum {
        text_index = 1,
        debug_index = 2,
        merge_index = 3,
        rela_index = 4,
        symtab_index = 5,
        strtab_index = 6,
        shstrtab_index = 7,
        section_count = 8,
        symbol_count = 4,
        relocation_count = 4,
    };
    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");
    uint32_t resolver_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "resolver");

    char section_names[160] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t debug_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_info");
    uint32_t merge_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_str");
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

    static const uint8_t text[] = {0xc3U, 0xc3U};
    static const char merge[] = "abcdefghijklmnopqrstuvwxyz";
    const size_t debug_size = 32U;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t debug_offset = test_elf_align(
            text_offset + sizeof(text), 8U);
    const size_t merge_offset = debug_offset + debug_size;
    const size_t rela_offset = test_elf_align(
            merge_offset + sizeof(merge), 8U);
    const size_t symtab_offset = test_elf_align(
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);

    debug_merge_write_header(bytes, LD_ELF_EM_X86_64, section_count,
                             shstrtab_index, section_table_offset);
    memcpy(bytes + text_offset, text, sizeof(text));
    memcpy(bytes + merge_offset, merge, sizeof(merge));
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *symbols = bytes + symtab_offset;
    debug_merge_write_symbol(symbols + LD_ELF64_SYM_SIZE, 0U,
                             LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION,
                             merge_index, 0U, 0U);
    debug_merge_write_symbol(symbols + 2U * LD_ELF64_SYM_SIZE,
                             start_name, LD_ELF_STB_GLOBAL,
                             LD_ELF_STT_FUNC, text_index, 0U, 1U);
    debug_merge_write_symbol(symbols + 3U * LD_ELF64_SYM_SIZE,
                             resolver_name, LD_ELF_STB_GLOBAL,
                             LD_ELF_STT_GNU_IFUNC, text_index, 1U, 1U);

    static const uint64_t offsets[relocation_count] = {0U, 8U, 12U, 24U};
    static const uint32_t symbols_index[relocation_count] = {1U, 1U, 1U,
                                                             3U};
    static const uint32_t types[relocation_count] = {
            LD_ELF_R_X86_64_GOTPC64,
            LD_ELF_R_X86_64_SIZE32,
            LD_ELF_R_X86_64_SIZE64,
            LD_ELF_R_X86_64_64,
    };
    static const int64_t addends[relocation_count] = {7, 11, 13, 0};
    for (size_t i = 0U; i < relocation_count; i++) {
        uint8_t *relocation = bytes + rela_offset +
                              i * LD_ELF64_RELA_SIZE;
        test_elf_write_u64(relocation, offsets[i]);
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(symbols_index[i], types[i]));
        test_elf_write_u64(relocation + 16U, (uint64_t) addends[i]);
    }

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            sizeof(text), 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + debug_index * LD_ELF64_SHDR_SIZE, debug_name,
            LD_ELF_SHT_PROGBITS, 0U, debug_offset, debug_size, 0U, 0U, 8U,
            0U);
    test_elf_write_section(
            sections + merge_index * LD_ELF64_SHDR_SIZE, merge_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS, merge_offset,
            sizeof(merge), 0U, 0U, 1U, 1U);
    test_elf_write_section(
            sections + rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset,
            relocation_count * LD_ELF64_RELA_SIZE, symtab_index,
            debug_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, symbol_names_size, 0U, 0U,
            1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint64_t debug_merge_output_symbol_value(
        const uint8_t *image, size_t image_size, const char *name) {
    const uint8_t *symtab = test_elf_find_output_section(
            image, image_size, ".symtab");
    assert(symtab != NULL);
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t section_size = test_elf_read_u16(image + 58U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    uint32_t strings_index = test_elf_read_u32(symtab + 40U);
    assert(strings_index < section_count);
    const uint8_t *strings = image + (size_t) table_offset +
                             (size_t) strings_index * section_size;
    uint64_t strings_offset = test_elf_read_u64(strings + 24U);
    uint64_t strings_size = test_elf_read_u64(strings + 32U);
    uint64_t symbols_offset = test_elf_read_u64(symtab + 24U);
    uint64_t symbols_size = test_elf_read_u64(symtab + 32U);
    assert(strings_offset <= image_size &&
           strings_size <= image_size - strings_offset);
    assert(symbols_offset <= image_size &&
           symbols_size <= image_size - symbols_offset &&
           symbols_size % LD_ELF64_SYM_SIZE == 0U);
    for (uint64_t offset = 0U; offset < symbols_size;
         offset += LD_ELF64_SYM_SIZE) {
        const uint8_t *symbol = image + (size_t) symbols_offset +
                                (size_t) offset;
        uint32_t name_offset = test_elf_read_u32(symbol);
        assert(name_offset < strings_size);
        const char *symbol_name =
                (const char *) image + (size_t) strings_offset + name_offset;
        assert(memchr(symbol_name, '\0',
                      (size_t) strings_size - name_offset) != NULL);
        if (strcmp(symbol_name, name) == 0)
            return test_elf_read_u64(symbol + 8U);
    }
    assert(false && "missing output symbol");
    return 0U;
}

static void debug_merge_test_x86_formulae_and_ifunc(void) {
    size_t object_size;
    uint8_t *object = debug_merge_make_x86_object(&object_size);
    char object_path[] = "/tmp/nature-ld-debug-merge-x86-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-debug-merge-x86-output";
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs_configured(
                   output_path, inputs, 1U, LD_ARCH_AMD64, false,
                   LD_DEBUG_DWARF, true, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *debug = test_elf_find_output_section(
            image, image_size, ".debug_info");
    assert(debug != NULL);
    uint64_t debug_offset = test_elf_read_u64(debug + 24U);
    uint64_t debug_size = test_elf_read_u64(debug + 32U);
    assert(debug_offset <= image_size && debug_size == 32U &&
           debug_size <= image_size - debug_offset);
    const uint8_t *contents = image + (size_t) debug_offset;
    assert(test_elf_read_u64(contents) == 7U);
    assert(test_elf_read_u32(contents + 8U) == 11U);
    assert(test_elf_read_u64(contents + 12U) == 13U);
    assert(test_elf_read_u64(contents + 24U) ==
           debug_merge_output_symbol_value(image, image_size, "resolver"));
    assert(test_elf_find_output_section(image, image_size, ".plt.got") ==
           NULL);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static uint8_t *debug_merge_make_aarch64_comdat_winner(
        size_t *result_size) {
    enum {
        group_index = 1,
        text_index = 2,
        symtab_index = 3,
        strtab_index = 4,
        shstrtab_index = 5,
        section_count = 6,
        symbol_count = 3,
    };
    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    uint32_t signature_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "debug.comdat.signature");
    uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    uint32_t group_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".group");
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.debug.comdat.winner");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t group_offset = LD_ELF64_EHDR_SIZE;
    const size_t group_size = 2U * sizeof(uint32_t);
    const size_t text_offset = test_elf_align(group_offset + group_size, 4U);
    const size_t text_size = sizeof(uint32_t);
    const size_t symtab_offset =
            test_elf_align(text_offset + text_size, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);

    debug_merge_write_header(bytes, LD_ELF_EM_AARCH64, section_count,
                             shstrtab_index, section_table_offset);
    test_elf_write_u32(bytes + group_offset, LD_ELF_GRP_COMDAT);
    test_elf_write_u32(bytes + group_offset + sizeof(uint32_t), text_index);
    test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *symbols = bytes + symtab_offset;
    debug_merge_write_symbol(symbols + LD_ELF64_SYM_SIZE, signature_name,
                             LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE,
                             text_index, 0U, 0U);
    debug_merge_write_symbol(symbols + 2U * LD_ELF64_SYM_SIZE, start_name,
                             LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC,
                             text_index, 0U, text_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + group_index * LD_ELF64_SHDR_SIZE, group_name,
            LD_ELF_SHT_GROUP, 0U, group_offset, group_size, symtab_index, 1U,
            sizeof(uint32_t), sizeof(uint32_t));
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_GROUP,
            text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, symbol_names_size, 0U, 0U,
            1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *debug_merge_make_aarch64_comdat_loser(
        size_t *result_size) {
    enum {
        group_index = 1,
        text_index = 2,
        debug_info_index = 3,
        debug_loc_index = 4,
        rela_info_index = 5,
        rela_loc_index = 6,
        symtab_index = 7,
        strtab_index = 8,
        shstrtab_index = 9,
        section_count = 10,
        symbol_count = 3,
    };
    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    uint32_t signature_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "debug.comdat.signature");

    char section_names[256] = {0};
    size_t section_names_size = 1U;
    uint32_t group_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".group");
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.debug.comdat.loser");
    uint32_t debug_info_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_info");
    uint32_t debug_loc_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".debug_loc");
    uint32_t rela_info_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.debug_info");
    uint32_t rela_loc_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.debug_loc");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t group_offset = LD_ELF64_EHDR_SIZE;
    const size_t group_size = 2U * sizeof(uint32_t);
    const size_t text_offset = test_elf_align(group_offset + group_size, 4U);
    const size_t text_size = sizeof(uint32_t);
    const size_t debug_info_offset =
            test_elf_align(text_offset + text_size, 8U);
    const size_t debug_size = sizeof(uint64_t);
    const size_t debug_loc_offset = debug_info_offset + debug_size;
    const size_t rela_info_offset =
            test_elf_align(debug_loc_offset + debug_size, 8U);
    const size_t rela_loc_offset = rela_info_offset + LD_ELF64_RELA_SIZE;
    const size_t symtab_offset =
            test_elf_align(rela_loc_offset + LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);

    debug_merge_write_header(bytes, LD_ELF_EM_AARCH64, section_count,
                             shstrtab_index, section_table_offset);
    test_elf_write_u32(bytes + group_offset, LD_ELF_GRP_COMDAT);
    test_elf_write_u32(bytes + group_offset + sizeof(uint32_t), text_index);
    test_elf_write_u32(bytes + text_offset, UINT32_C(0xdeadc0de));
    memset(bytes + debug_info_offset, 0xa5, debug_size);
    memset(bytes + debug_loc_offset, 0x5a, debug_size);
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *symbols = bytes + symtab_offset;
    debug_merge_write_symbol(symbols + LD_ELF64_SYM_SIZE, signature_name,
                             LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE,
                             text_index, 0U, 0U);
    debug_merge_write_symbol(symbols + 2U * LD_ELF64_SYM_SIZE, 0U,
                             LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION,
                             text_index, 0U, text_size);

    uint8_t *rela_info = bytes + rela_info_offset;
    test_elf_write_u64(rela_info, 0U);
    test_elf_write_u64(
            rela_info + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(rela_info + 16U, 0U);
    uint8_t *rela_loc = bytes + rela_loc_offset;
    test_elf_write_u64(rela_loc, 0U);
    test_elf_write_u64(
            rela_loc + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(rela_loc + 16U, 0U);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + group_index * LD_ELF64_SHDR_SIZE, group_name,
            LD_ELF_SHT_GROUP, 0U, group_offset, group_size, symtab_index, 1U,
            sizeof(uint32_t), sizeof(uint32_t));
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_GROUP,
            text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + debug_info_index * LD_ELF64_SHDR_SIZE,
            debug_info_name, LD_ELF_SHT_PROGBITS, 0U, debug_info_offset,
            debug_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + debug_loc_index * LD_ELF64_SHDR_SIZE, debug_loc_name,
            LD_ELF_SHT_PROGBITS, 0U, debug_loc_offset, debug_size, 0U, 0U,
            8U, 0U);
    test_elf_write_section(
            sections + rela_info_index * LD_ELF64_SHDR_SIZE,
            rela_info_name, LD_ELF_SHT_RELA, 0U, rela_info_offset,
            LD_ELF64_RELA_SIZE, symtab_index, debug_info_index, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + rela_loc_index * LD_ELF64_SHDR_SIZE, rela_loc_name,
            LD_ELF_SHT_RELA, 0U, rela_loc_offset, LD_ELF64_RELA_SIZE,
            symtab_index, debug_loc_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, symbol_count, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, symbol_names_size, 0U, 0U,
            1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void debug_merge_test_comdat_tombstones(void) {
    size_t winner_size;
    size_t loser_size;
    uint8_t *winner =
            debug_merge_make_aarch64_comdat_winner(&winner_size);
    uint8_t *loser = debug_merge_make_aarch64_comdat_loser(&loser_size);
    char winner_path[] =
            "/tmp/nature-ld-debug-comdat-winner-XXXXXX";
    char loser_path[] = "/tmp/nature-ld-debug-comdat-loser-XXXXXX";
    write_fixture(winner_path, winner, winner_size);
    write_fixture(loser_path, loser, loser_size);
    free(winner);
    free(loser);

    static const char *output_path =
            "/tmp/nature-ld-debug-comdat-output";
    const char *inputs[] = {winner_path, loser_path};
    diagnostic_capture_t capture = {0};
    int status = link_test_elf_inputs_configured(
            output_path, inputs, 2U, LD_ARCH_ARM64, false,
            LD_DEBUG_DWARF, true, &capture);
    assert(status == LD_OK);
    assert(capture.count == 0U);
    assert(!test_elf_file_contains_u32(output_path, UINT32_C(0xdeadc0de)));

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *debug_info = test_elf_find_output_section(
            image, image_size, ".debug_info");
    const uint8_t *debug_loc = test_elf_find_output_section(
            image, image_size, ".debug_loc");
    assert(debug_info != NULL && debug_loc != NULL);
    assert(test_elf_read_u64(debug_info + 16U) == 0U);
    assert(test_elf_read_u64(debug_loc + 16U) == 0U);
    uint64_t debug_info_offset = test_elf_read_u64(debug_info + 24U);
    uint64_t debug_info_size = test_elf_read_u64(debug_info + 32U);
    uint64_t debug_loc_offset = test_elf_read_u64(debug_loc + 24U);
    uint64_t debug_loc_size = test_elf_read_u64(debug_loc + 32U);
    assert(debug_info_offset <= image_size && debug_info_size == 8U &&
           debug_info_size <= image_size - debug_info_offset);
    assert(debug_loc_offset <= image_size && debug_loc_size == 8U &&
           debug_loc_size <= image_size - debug_loc_offset);
    assert(test_elf_read_u64(image + (size_t) debug_info_offset) == 0U);
    assert(test_elf_read_u64(image + (size_t) debug_loc_offset) == 1U);

    free(image);
    unlink(winner_path);
    unlink(loser_path);
    unlink(output_path);
}

static const char debug_merge_riscv_target[] =
        "target-abcdefghijklmnopqrstuvwxyz-0123456789-ABCDEFGH";

static uint8_t *debug_merge_make_riscv_owner(size_t *result_size) {
    enum {
        text_index = 1,
        merge_index = 2,
        symtab_index = 3,
        strtab_index = 4,
        shstrtab_index = 5,
        section_count = 6,
        symbol_count = 3,
    };
    static const char prefix[] = "aaaaaaaaaaaaaaaaaaa";
    char merge[sizeof(prefix) + sizeof(debug_merge_riscv_target)];
    memcpy(merge, prefix, sizeof(prefix));
    memcpy(merge + sizeof(prefix), debug_merge_riscv_target,
           sizeof(debug_merge_riscv_target));
    static const char symbols[] = "\0_start";
    char names[128] = {0};
    size_t names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".text");
    uint32_t merge_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".debug_str");
    uint32_t symtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".shstrtab");

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t merge_offset = text_offset + 4U;
    const size_t symtab_offset = test_elf_align(
            merge_offset + sizeof(merge), 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t names_offset = strtab_offset + sizeof(symbols);
    const size_t section_table_offset = test_elf_align(
            names_offset + names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);
    debug_merge_write_header(bytes, LD_ELF_EM_RISCV, section_count,
                             shstrtab_index, section_table_offset);
    test_elf_write_u32(bytes + text_offset, UINT32_C(0x00000073));
    memcpy(bytes + merge_offset, merge, sizeof(merge));
    memcpy(bytes + strtab_offset, symbols, sizeof(symbols));
    memcpy(bytes + names_offset, names, names_size);
    debug_merge_write_symbol(bytes + symtab_offset + LD_ELF64_SYM_SIZE,
                             0U, LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION,
                             merge_index, 0U, 0U);
    debug_merge_write_symbol(bytes + symtab_offset +
                                     2U * LD_ELF64_SYM_SIZE,
                             1U, LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC,
                             text_index, 0U, 4U);
    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, 4U, 0U,
            0U, 4U, 0U);
    test_elf_write_section(
            sections + merge_index * LD_ELF64_SHDR_SIZE, merge_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS, merge_offset,
            sizeof(merge), 0U, 0U, 1U, 1U);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbols), 0U, 0U,
            1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, names_offset, names_size,
            0U, 0U, 1U, 0U);
    *result_size = size;
    return bytes;
}

static uint8_t *debug_merge_make_riscv_relocator(size_t *result_size) {
    enum {
        debug_index = 1,
        merge_index = 2,
        rela_index = 3,
        symtab_index = 4,
        strtab_index = 5,
        shstrtab_index = 6,
        section_count = 7,
        symbol_count = 3,
        relocation_count = 2,
    };
    static const char prefix[] = "bbbbbbbbb";
    char merge[sizeof(prefix) + sizeof(debug_merge_riscv_target)];
    memcpy(merge, prefix, sizeof(prefix));
    memcpy(merge + sizeof(prefix), debug_merge_riscv_target,
           sizeof(debug_merge_riscv_target));
    static const char symbols[] = "\0";
    char names[160] = {0};
    size_t names_size = 1U;
    uint32_t debug_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".debug_info");
    uint32_t merge_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".debug_str");
    uint32_t rela_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".rela.debug_info");
    uint32_t symtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            names, sizeof(names), &names_size, ".shstrtab");
    const size_t debug_offset = LD_ELF64_EHDR_SIZE;
    const size_t merge_offset = debug_offset + 2U;
    const size_t rela_offset = test_elf_align(
            merge_offset + sizeof(merge), 8U);
    const size_t symtab_offset = test_elf_align(
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t names_offset = strtab_offset + sizeof(symbols);
    const size_t section_table_offset = test_elf_align(
            names_offset + names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);
    debug_merge_write_header(bytes, LD_ELF_EM_RISCV, section_count,
                             shstrtab_index, section_table_offset);
    bytes[debug_offset] = 0x80U;
    bytes[debug_offset + 1U] = 0U;
    memcpy(bytes + merge_offset, merge, sizeof(merge));
    memcpy(bytes + strtab_offset, symbols, sizeof(symbols));
    memcpy(bytes + names_offset, names, names_size);
    debug_merge_write_symbol(bytes + symtab_offset + LD_ELF64_SYM_SIZE,
                             0U, LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION,
                             merge_index, 0U, 0U);
    debug_merge_write_symbol(bytes + symtab_offset +
                                     2U * LD_ELF64_SYM_SIZE,
                             0U, LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE,
                             LD_ELF_SHN_ABS, 100U, 0U);
    uint8_t *set = bytes + rela_offset;
    test_elf_write_u64(set, 0U);
    test_elf_write_u64(
            set + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_RISCV_SET_ULEB128));
    uint8_t *sub = set + LD_ELF64_RELA_SIZE;
    test_elf_write_u64(sub, 0U);
    test_elf_write_u64(
            sub + 8U,
            LD_ELF_RELA_INFO(1U, LD_ELF_R_RISCV_SUB_ULEB128));
    test_elf_write_u64(sub + 16U, sizeof(prefix));
    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + debug_index * LD_ELF64_SHDR_SIZE, debug_name,
            LD_ELF_SHT_PROGBITS, 0U, debug_offset, 2U, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + merge_index * LD_ELF64_SHDR_SIZE, merge_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS, merge_offset,
            sizeof(merge), 0U, 0U, 1U, 1U);
    test_elf_write_section(
            sections + rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset,
            relocation_count * LD_ELF64_RELA_SIZE, symtab_index,
            debug_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, symbol_count,
            8U, LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbols), 0U, 0U,
            1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, names_offset, names_size,
            0U, 0U, 1U, 0U);
    *result_size = size;
    return bytes;
}

static size_t debug_merge_find_bytes(const uint8_t *bytes, size_t size,
                                     const uint8_t *needle,
                                     size_t needle_size) {
    assert(needle_size != 0U && needle_size <= size);
    for (size_t i = 0U; i <= size - needle_size; i++) {
        if (memcmp(bytes + i, needle, needle_size) == 0) return i;
    }
    assert(false && "missing merged string");
    return 0U;
}

static void debug_merge_test_riscv_sub_uleb(void) {
    size_t owner_size;
    size_t relocator_size;
    uint8_t *owner = debug_merge_make_riscv_owner(&owner_size);
    uint8_t *relocator = debug_merge_make_riscv_relocator(&relocator_size);
    char owner_path[] = "/tmp/nature-ld-debug-merge-rv-owner-XXXXXX";
    char relocator_path[] = "/tmp/nature-ld-debug-merge-rv-rel-XXXXXX";
    write_fixture(owner_path, owner, owner_size);
    write_fixture(relocator_path, relocator, relocator_size);
    free(owner);
    free(relocator);
    static const char *output_path =
            "/tmp/nature-ld-debug-merge-rv-output";
    const char *inputs[] = {owner_path, relocator_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs_configured(
                   output_path, inputs, 2U, LD_ARCH_RISCV64, false,
                   LD_DEBUG_DWARF, true, &capture) == LD_OK);
    assert(capture.count == 0U);
    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *debug = test_elf_find_output_section(
            image, image_size, ".debug_info");
    const uint8_t *strings = test_elf_find_output_section(
            image, image_size, ".debug_str");
    assert(debug != NULL && strings != NULL);
    uint64_t debug_offset = test_elf_read_u64(debug + 24U);
    uint64_t debug_size = test_elf_read_u64(debug + 32U);
    uint64_t strings_offset = test_elf_read_u64(strings + 24U);
    uint64_t strings_size = test_elf_read_u64(strings + 32U);
    assert(debug_offset <= image_size && debug_size == 2U &&
           debug_size <= image_size - debug_offset);
    assert(strings_offset <= image_size &&
           strings_size <= image_size - strings_offset);
    size_t mapped = debug_merge_find_bytes(
            image + (size_t) strings_offset, (size_t) strings_size,
            (const uint8_t *) debug_merge_riscv_target,
            sizeof(debug_merge_riscv_target));
    const uint64_t addend = sizeof("bbbbbbbbb");
    assert(mapped >= 2U * addend && mapped <= 100U + 2U * addend);
    uint64_t expected = 100U - ((uint64_t) mapped - 2U * addend);
    const uint8_t *encoded = image + (size_t) debug_offset;
    uint64_t actual = (uint64_t) (encoded[0] & 0x7fU) |
                      ((uint64_t) (encoded[1] & 0x7fU) << 7U);
    assert((encoded[0] & 0x80U) != 0U &&
           (encoded[1] & 0x80U) == 0U);
    assert(actual == expected);
    free(image);
    unlink(owner_path);
    unlink(relocator_path);
    unlink(output_path);
}

void test_ld_elf_debug_merge(void) {
    debug_merge_test_x86_formulae_and_ifunc();
    debug_merge_test_comdat_tombstones();
    debug_merge_test_riscv_sub_uleb();
}

#ifdef LD_ELF_DEBUG_MERGE_STANDALONE_TEST
int main(void) {
    test_ld_elf_debug_merge();
    return 0;
}
#endif
