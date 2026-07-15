#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *make_init_fini_object(uint32_t init_word,
                                      uint32_t text_word,
                                      uint32_t fini_word, bool define_start,
                                      size_t *result_size) {
    static const char symbol_names[] = "\0_start";
    static const char section_names[] =
            "\0.init\0.text\0.fini\0.symtab\0.strtab\0.shstrtab";
    const size_t init_size = init_word == 0U ? 0U : sizeof(uint32_t);
    const size_t text_size = text_word == 0U ? 0U : sizeof(uint32_t);
    const size_t fini_size = fini_word == 0U ? 0U : sizeof(uint32_t);
    const size_t init_offset = LD_ELF64_EHDR_SIZE;
    const size_t text_offset = test_elf_align(init_offset + init_size, 4U);
    const size_t fini_offset = test_elf_align(text_offset + text_size, 4U);
    const size_t symbol_count = define_start ? 2U : 1U;
    const size_t symbols_offset =
            test_elf_align(fini_offset + fini_size, 8U);
    const size_t strings_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_names_offset =
            strings_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            section_names_offset + sizeof(section_names), 8U);
    const uint16_t section_count = 7U;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

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
    test_elf_write_u16(bytes + 62U, 6U);

    if (init_size != 0U) test_elf_write_u32(bytes + init_offset, init_word);
    if (text_size != 0U) test_elf_write_u32(bytes + text_offset, text_word);
    if (fini_size != 0U) test_elf_write_u32(bytes + fini_offset, fini_word);
    memcpy(bytes + strings_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           sizeof(section_names));

    if (define_start) {
        uint8_t *start = bytes + symbols_offset + LD_ELF64_SYM_SIZE;
        test_elf_write_u32(start, 1U);
        start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
        test_elf_write_u16(start + 6U, 2U);
        test_elf_write_u64(start + 16U, text_size);
    }

    uint8_t *sections = bytes + section_table_offset;
    const uint64_t executable_flags =
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, 1U,
                           LD_ELF_SHT_PROGBITS, executable_flags,
                           init_offset, init_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, 7U,
                           LD_ELF_SHT_PROGBITS, executable_flags,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, 13U,
                           LD_ELF_SHT_PROGBITS, executable_flags,
                           fini_offset, fini_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE, 19U,
                           LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
                           symbol_count * LD_ELF64_SYM_SIZE, 5U, 1U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE, 27U,
                           LD_ELF_SHT_STRTAB, 0U, strings_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 6U * LD_ELF64_SHDR_SIZE, 35U,
                           LD_ELF_SHT_STRTAB, 0U, section_names_offset,
                           sizeof(section_names), 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static const uint8_t *section_contents(const uint8_t *image,
                                       size_t image_size,
                                       const uint8_t *section,
                                       uint64_t expected_size) {
    assert(section != NULL);
    uint64_t offset = test_elf_read_u64(section + 24U);
    uint64_t size = test_elf_read_u64(section + 32U);
    assert(size == expected_size);
    assert(offset <= image_size && size <= image_size - offset);
    return image + (size_t) offset;
}

static void test_init_fini_sections_are_collected_separately(void) {
    const uint32_t init_head = 0xa1a2a3a4U;
    const uint32_t init_tail = 0xc1c2c3c4U;
    const uint32_t text_word = 0xd65f03c0U;
    const uint32_t fini_head = 0xb1b2b3b4U;
    const uint32_t fini_tail = 0xe1e2e3e4U;
    size_t first_size, main_size, last_size;
    uint8_t *first = make_init_fini_object(
            init_head, 0U, fini_head, false, &first_size);
    uint8_t *main = make_init_fini_object(
            0U, text_word, 0U, true, &main_size);
    uint8_t *last = make_init_fini_object(
            init_tail, 0U, fini_tail, false, &last_size);

    char first_path[] = "/tmp/nature-ld-init-head-XXXXXX";
    char main_path[] = "/tmp/nature-ld-init-main-XXXXXX";
    char last_path[] = "/tmp/nature-ld-init-tail-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(main_path, main, main_size);
    write_fixture(last_path, last, last_size);
    free(first);
    free(main);
    free(last);

    static const char *output_path = "/tmp/nature-ld-init-fini-output";
    const char *inputs[] = {first_path, main_path, last_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 3U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *init =
            test_elf_find_output_section(image, image_size, ".init");
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *fini =
            test_elf_find_output_section(image, image_size, ".fini");
    const uint8_t *init_bytes =
            section_contents(image, image_size, init, 2U * sizeof(uint32_t));
    const uint8_t *text_bytes =
            section_contents(image, image_size, text, sizeof(uint32_t));
    const uint8_t *fini_bytes =
            section_contents(image, image_size, fini, 2U * sizeof(uint32_t));

    assert(test_elf_read_u64(init + 16U) <
           test_elf_read_u64(text + 16U));
    assert(test_elf_read_u64(text + 16U) <
           test_elf_read_u64(fini + 16U));
    assert(test_elf_read_u32(init_bytes) == init_head);
    assert(test_elf_read_u32(init_bytes + sizeof(uint32_t)) == init_tail);
    assert(test_elf_read_u32(text_bytes) == text_word);
    assert(test_elf_read_u32(fini_bytes) == fini_head);
    assert(test_elf_read_u32(fini_bytes + sizeof(uint32_t)) == fini_tail);

    free(image);
    unlink(first_path);
    unlink(main_path);
    unlink(last_path);
    unlink(output_path);
}

static uint8_t *make_rodata_alignment_object(uint64_t rodata_alignment,
                                             uint8_t fill,
                                             bool define_start,
                                             size_t *result_size) {
    static const char symbol_names[] = "\0_start";
    static const char section_names[] =
            "\0.text\0.rodata\0.symtab\0.strtab\0.shstrtab";
    const size_t text_size = define_start ? sizeof(uint32_t) : 0U;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t rodata_offset =
            test_elf_align(text_offset + text_size, rodata_alignment);
    const size_t rodata_size = (size_t) rodata_alignment;
    const size_t symbol_count = define_start ? 2U : 1U;
    const size_t symbols_offset =
            test_elf_align(rodata_offset + rodata_size, 8U);
    const size_t strings_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_names_offset =
            strings_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            section_names_offset + sizeof(section_names), 8U);
    const uint16_t section_count = 6U;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

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
    test_elf_write_u16(bytes + 62U, 5U);

    if (define_start)
        test_elf_write_u32(bytes + text_offset, 0xd65f03c0U);
    memset(bytes + rodata_offset, fill, rodata_size);
    memcpy(bytes + strings_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           sizeof(section_names));

    if (define_start) {
        uint8_t *start = bytes + symbols_offset + LD_ELF64_SYM_SIZE;
        test_elf_write_u32(start, 1U);
        start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
        test_elf_write_u16(start + 6U, 1U);
        test_elf_write_u64(start + 16U, text_size);
    }

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + LD_ELF64_SHDR_SIZE, 1U, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + 2U * LD_ELF64_SHDR_SIZE, 7U,
            LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC, rodata_offset,
            rodata_size, 0U, 0U, rodata_alignment, 0U);
    test_elf_write_section(
            sections + 3U * LD_ELF64_SHDR_SIZE, 15U,
            LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, 4U, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + 4U * LD_ELF64_SHDR_SIZE, 23U,
            LD_ELF_SHT_STRTAB, 0U, strings_offset, sizeof(symbol_names),
            0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + 5U * LD_ELF64_SHDR_SIZE, 31U,
            LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            sizeof(section_names), 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void test_output_alignment_uses_maximum_input_alignment(void) {
    size_t low_size, high_size;
    uint8_t *low = make_rodata_alignment_object(8U, 0x18U, true,
                                                &low_size);
    uint8_t *high = make_rodata_alignment_object(16U, 0x30U, false,
                                                 &high_size);
    char low_path[] = "/tmp/nature-ld-align-low-XXXXXX";
    char high_path[] = "/tmp/nature-ld-align-high-XXXXXX";
    write_fixture(low_path, low, low_size);
    write_fixture(high_path, high, high_size);
    free(low);
    free(high);

    static const char *output_path = "/tmp/nature-ld-align-output";
    const char *inputs[] = {low_path, high_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 2U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *rodata =
            test_elf_find_output_section(image, image_size, ".rodata");
    assert(rodata != NULL);
    assert(test_elf_read_u64(rodata + 48U) == 16U);
    assert((test_elf_read_u64(rodata + 16U) & 15U) == 0U);
    const uint8_t *contents =
            section_contents(image, image_size, rodata, 32U);
    for (size_t i = 0U; i < 8U; i++) assert(contents[i] == 0x18U);
    for (size_t i = 8U; i < 16U; i++) assert(contents[i] == 0U);
    for (size_t i = 16U; i < 32U; i++) assert(contents[i] == 0x30U);

    free(image);
    unlink(low_path);
    unlink(high_path);
    unlink(output_path);
}

static uint8_t *make_merge_string_object(const char *unique,
                                         bool define_start,
                                         size_t *result_size) {
    static const char symbol_names[] = "\0shared\0_start";
    const size_t shared_size = sizeof("shared");
    const size_t unique_size = strlen(unique) + 1U;
    const size_t merge_size = shared_size + unique_size;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t data_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t merge_offset = data_offset + 16U;
    const size_t rela_offset = test_elf_align(merge_offset + merge_size, 8U);
    const size_t symbol_count = define_start ? 4U : 3U;
    const size_t symbols_offset =
            rela_offset + 2U * LD_ELF64_RELA_SIZE;
    const size_t strings_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".data");
    uint32_t merge_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rodata.str1.1");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.data");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t section_names_offset =
            strings_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            section_names_offset + section_names_size, 8U);
    const uint16_t section_count = 8U;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

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
    test_elf_write_u16(bytes + 62U, 7U);

    test_elf_write_u32(bytes + text_offset, 0xd65f03c0U);
    memcpy(bytes + merge_offset, "shared", shared_size);
    memcpy(bytes + merge_offset + shared_size, unique, unique_size);
    memcpy(bytes + strings_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           section_names_size);

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *section_symbol = symbols + LD_ELF64_SYM_SIZE;
    section_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(section_symbol + 6U, 3U);
    uint8_t *shared_symbol = section_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(shared_symbol, 1U);
    shared_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_OBJECT);
    test_elf_write_u16(shared_symbol + 6U, 3U);
    test_elf_write_u64(shared_symbol + 16U, shared_size);
    if (define_start) {
        uint8_t *start_symbol = shared_symbol + LD_ELF64_SYM_SIZE;
        test_elf_write_u32(start_symbol, 8U);
        start_symbol[4] =
                LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
        test_elf_write_u16(start_symbol + 6U, 1U);
        test_elf_write_u64(start_symbol + 16U, 4U);
    }

    uint8_t *relocations = bytes + rela_offset;
    test_elf_write_u64(relocations, 0U);
    test_elf_write_u64(
            relocations + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(relocations + LD_ELF64_RELA_SIZE, 8U);
    test_elf_write_u64(
            relocations + LD_ELF64_RELA_SIZE + 8U,
            LD_ELF_RELA_INFO(1U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(relocations + LD_ELF64_RELA_SIZE + 16U,
                       shared_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, 4U, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, data_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, data_offset,
                           16U, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + 3U * LD_ELF64_SHDR_SIZE, merge_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS,
            merge_offset, merge_size, 0U, 0U, 1U, 1U);
    test_elf_write_section(
            sections + 4U * LD_ELF64_SHDR_SIZE, rela_name, LD_ELF_SHT_RELA,
            0U, rela_offset, 2U * LD_ELF64_RELA_SIZE, 5U, 2U, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + 5U * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, 6U, 3U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 6U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, strings_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + 7U * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static const char *output_string_at(const uint8_t *image,
                                    size_t image_size,
                                    const uint8_t *section,
                                    uint64_t address) {
    uint64_t section_address = test_elf_read_u64(section + 16U);
    uint64_t section_offset = test_elf_read_u64(section + 24U);
    uint64_t section_size = test_elf_read_u64(section + 32U);
    assert(address >= section_address &&
           address - section_address < section_size);
    uint64_t offset = section_offset + address - section_address;
    assert(offset < image_size &&
           memchr(image + (size_t) offset, '\0', image_size - offset));
    return (const char *) image + (size_t) offset;
}

static void test_merge_strings_are_deduplicated(void) {
    size_t first_size, second_size;
    uint8_t *first =
            make_merge_string_object("first", true, &first_size);
    uint8_t *second =
            make_merge_string_object("second", false, &second_size);
    char first_path[] = "/tmp/nature-ld-merge-first-XXXXXX";
    char second_path[] = "/tmp/nature-ld-merge-second-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path = "/tmp/nature-ld-merge-output";
    const char *inputs[] = {first_path, second_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 2U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *merge = test_elf_find_output_section(
            image, image_size, ".rodata.str1.1");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    assert(merge != NULL && data != NULL);
    assert(test_elf_read_u64(merge + 32U) ==
           sizeof("shared") + sizeof("first") + sizeof("second"));
    assert(test_elf_read_u64(merge + 56U) == 1U);
    uint64_t flags = test_elf_read_u64(merge + 8U);
    assert((flags & (LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE |
                     LD_ELF_SHF_STRINGS)) ==
           (LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS));

    const uint8_t *data_bytes =
            section_contents(image, image_size, data, 4U * sizeof(uint64_t));
    uint64_t first_shared = test_elf_read_u64(data_bytes);
    uint64_t first_unique = test_elf_read_u64(data_bytes + 8U);
    uint64_t second_shared = test_elf_read_u64(data_bytes + 16U);
    uint64_t second_unique = test_elf_read_u64(data_bytes + 24U);
    assert(first_shared == second_shared);
    assert(first_unique != first_shared && second_unique != first_shared &&
           first_unique != second_unique);
    assert(strcmp(output_string_at(image, image_size, merge, first_shared),
                  "shared") == 0);
    assert(strcmp(output_string_at(image, image_size, merge, first_unique),
                  "first") == 0);
    assert(strcmp(output_string_at(image, image_size, merge, second_unique),
                  "second") == 0);

    free(image);
    unlink(first_path);
    unlink(second_path);
    unlink(output_path);
}

void test_ld_elf_layout(void) {
    test_init_fini_sections_are_collected_separately();
    test_output_alignment_uses_maximum_input_alignment();
    test_merge_strings_are_deduplicated();
}
