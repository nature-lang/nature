#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_cie.h"
#include "src/ld/ld_elf_eh_frame.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_reloc.h"
#include "src/ld/ld_elf_symtab.h"
#include "src/ld/ld_elf_thunk.h"
#include "src/ld/ld_internal.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    TEST_ELF_GROUP_VALID = 0,
    TEST_ELF_GROUP_ORDINARY,
    TEST_ELF_GROUP_BAD_LINK,
    TEST_ELF_GROUP_BAD_INFO,
    TEST_ELF_GROUP_BAD_FLAGS,
    TEST_ELF_GROUP_BAD_SIZE,
    TEST_ELF_GROUP_BAD_ENTSIZE,
    TEST_ELF_GROUP_EMPTY,
    TEST_ELF_GROUP_BAD_MEMBER,
    TEST_ELF_GROUP_MEMBER_WITHOUT_FLAG,
    TEST_ELF_GROUP_DUPLICATE_OWNER,
    TEST_ELF_GROUP_ORPHAN_MEMBER,
} test_elf_group_variant_t;

static uint8_t *make_test_elf_group_object_with_reference(
        const char *signature, const char *member_name, uint32_t text_word,
        bool section_symbol_signature, test_elf_group_variant_t variant,
        const char *undefined_symbol,
        size_t *result_size) {
    assert(signature && *signature && member_name && *member_name);
    const bool has_relocation =
            undefined_symbol != NULL && undefined_symbol[0] != '\0';
    const uint32_t group_count = variant == TEST_ELF_GROUP_ORPHAN_MEMBER
                                         ? 0U
                                         : (variant ==
                                                            TEST_ELF_GROUP_DUPLICATE_OWNER
                                                    ? 2U
                                                    : 1U);
    const uint32_t text_index = 1U + group_count;
    const uint32_t rela_index = has_relocation ? text_index + 1U : UINT32_MAX;
    const uint32_t symtab_index = text_index + 1U +
                                  (has_relocation ? 1U : 0U);
    const uint32_t strtab_index = symtab_index + 1U;
    const uint32_t shstrtab_index = strtab_index + 1U;
    const uint16_t section_count = (uint16_t) (shstrtab_index + 1U);

    const size_t signature_length = strlen(signature);
    const size_t member_name_length = strlen(member_name);
    const size_t undefined_symbol_length =
            has_relocation ? strlen(undefined_symbol) : 0U;
    static const char start_name[] = "_start";
    static const char group_name[] = ".group";
    static const char symtab_name[] = ".symtab";
    static const char strtab_name[] = ".strtab";
    static const char shstrtab_name[] = ".shstrtab";
    static const char rela_name[] = ".rela.text";

    const size_t symbol_strings_size =
            1U + signature_length + 1U + sizeof(start_name) +
            (has_relocation ? undefined_symbol_length + 1U : 0U);
    char *symbol_strings = calloc(1, symbol_strings_size);
    assert(symbol_strings != NULL);
    const uint32_t signature_name_offset = 1U;
    memcpy(symbol_strings + signature_name_offset, signature,
           signature_length + 1U);
    const uint32_t start_name_offset =
            (uint32_t) (signature_name_offset + signature_length + 1U);
    memcpy(symbol_strings + start_name_offset, start_name,
           sizeof(start_name));
    const uint32_t undefined_symbol_name_offset =
            start_name_offset + (uint32_t) sizeof(start_name);
    if (has_relocation) {
        memcpy(symbol_strings + undefined_symbol_name_offset,
               undefined_symbol, undefined_symbol_length + 1U);
    }

    const size_t section_strings_size =
            1U + sizeof(group_name) + member_name_length + 1U +
            sizeof(symtab_name) + sizeof(strtab_name) +
            sizeof(shstrtab_name) +
            (has_relocation ? sizeof(rela_name) : 0U);
    char *section_strings = calloc(1, section_strings_size);
    assert(section_strings != NULL);
    size_t section_string_cursor = 1U;
    const uint32_t group_name_offset = (uint32_t) section_string_cursor;
    memcpy(section_strings + section_string_cursor, group_name,
           sizeof(group_name));
    section_string_cursor += sizeof(group_name);
    const uint32_t member_name_offset = (uint32_t) section_string_cursor;
    memcpy(section_strings + section_string_cursor, member_name,
           member_name_length + 1U);
    section_string_cursor += member_name_length + 1U;
    const uint32_t symtab_name_offset = (uint32_t) section_string_cursor;
    memcpy(section_strings + section_string_cursor, symtab_name,
           sizeof(symtab_name));
    section_string_cursor += sizeof(symtab_name);
    const uint32_t strtab_name_offset = (uint32_t) section_string_cursor;
    memcpy(section_strings + section_string_cursor, strtab_name,
           sizeof(strtab_name));
    section_string_cursor += sizeof(strtab_name);
    const uint32_t shstrtab_name_offset = (uint32_t) section_string_cursor;
    memcpy(section_strings + section_string_cursor, shstrtab_name,
           sizeof(shstrtab_name));
    section_string_cursor += sizeof(shstrtab_name);
    uint32_t rela_name_offset = 0U;
    if (has_relocation) {
        rela_name_offset = (uint32_t) section_string_cursor;
        memcpy(section_strings + section_string_cursor, rela_name,
               sizeof(rela_name));
        section_string_cursor += sizeof(rela_name);
    }
    assert(section_string_cursor == section_strings_size);

    const size_t groups_offset = LD_ELF64_EHDR_SIZE;
    const size_t group_stride =
            (2U + (has_relocation ? 1U : 0U)) * sizeof(uint32_t);
    const size_t text_offset = test_elf_align(
            groups_offset + (size_t) group_count * group_stride, 4U);
    const size_t relocation_offset =
            has_relocation ? test_elf_align(text_offset + 4U, 8U) : 0U;
    const size_t symbols_offset = test_elf_align(
            has_relocation ? relocation_offset + LD_ELF64_RELA_SIZE
                           : text_offset + 4U,
            8U);
    const size_t symbol_count = 3U + (has_relocation ? 1U : 0U);
    const size_t symbol_strings_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_strings_offset =
            symbol_strings_offset + symbol_strings_size;
    const size_t section_table_offset = test_elf_align(
            section_strings_offset + section_strings_size, 8U);
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
    test_elf_write_u16(bytes + 62U, (uint16_t) shstrtab_index);

    for (uint32_t i = 0; i < group_count; i++) {
        uint8_t *group_data = bytes + groups_offset + i * group_stride;
        test_elf_write_u32(
                group_data,
                variant == TEST_ELF_GROUP_BAD_FLAGS
                        ? 2U
                        : (variant == TEST_ELF_GROUP_ORDINARY ? 0U : 1U));
        test_elf_write_u32(
                group_data + sizeof(uint32_t),
                variant == TEST_ELF_GROUP_BAD_MEMBER ? section_count
                                                     : text_index);
        if (has_relocation) {
            test_elf_write_u32(group_data + 2U * sizeof(uint32_t),
                               rela_index);
        }
    }
    test_elf_write_u32(bytes + text_offset, text_word);
    if (has_relocation) {
        uint8_t *relocation = bytes + relocation_offset;
        test_elf_write_u64(relocation, 0U);
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(3U, LD_ELF_R_AARCH64_ABS32));
        test_elf_write_u64(relocation + 16U, 0U);
    }
    memcpy(bytes + symbol_strings_offset, symbol_strings,
           symbol_strings_size);
    memcpy(bytes + section_strings_offset, section_strings,
           section_strings_size);
    free(symbol_strings);
    free(section_strings);

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *signature_symbol = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(signature_symbol,
                       section_symbol_signature ? 0U
                                                : signature_name_offset);
    signature_symbol[4] = LD_ELF_SYM_INFO(
            LD_ELF_STB_LOCAL, section_symbol_signature
                                      ? LD_ELF_STT_SECTION
                                      : LD_ELF_STT_NOTYPE);
    test_elf_write_u16(signature_symbol + 6U, (uint16_t) text_index);
    uint8_t *start_symbol = signature_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start_symbol, start_name_offset);
    start_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start_symbol + 6U, (uint16_t) text_index);
    test_elf_write_u64(start_symbol + 16U, 4U);
    if (has_relocation) {
        uint8_t *undefined = start_symbol + LD_ELF64_SYM_SIZE;
        test_elf_write_u32(undefined, undefined_symbol_name_offset);
        undefined[4] =
                LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
        test_elf_write_u16(undefined + 6U, LD_ELF_SHN_UNDEF);
    }

    uint8_t *sections = bytes + section_table_offset;
    for (uint32_t i = 0; i < group_count; i++) {
        uint32_t link = variant == TEST_ELF_GROUP_BAD_LINK
                                ? strtab_index
                                : symtab_index;
        uint32_t info = variant == TEST_ELF_GROUP_BAD_INFO
                                ? (uint32_t) symbol_count
                                : 1U;
        uint64_t group_size = variant == TEST_ELF_GROUP_BAD_SIZE
                                      ? 6U
                                      : (variant == TEST_ELF_GROUP_EMPTY
                                                 ? sizeof(uint32_t)
                                                 : group_stride);
        uint64_t entry_size = variant == TEST_ELF_GROUP_BAD_ENTSIZE
                                      ? 8U
                                      : sizeof(uint32_t);
        test_elf_write_section(
                sections + (size_t) (1U + i) * LD_ELF64_SHDR_SIZE,
                group_name_offset, LD_ELF_SHT_GROUP, 0U,
                groups_offset + (size_t) i * group_stride, group_size, link,
                info, 4U, entry_size);
    }
    uint64_t text_flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR;
    if (variant != TEST_ELF_GROUP_MEMBER_WITHOUT_FLAG)
        text_flags |= LD_ELF_SHF_GROUP;
    test_elf_write_section(
            sections + (size_t) text_index * LD_ELF64_SHDR_SIZE,
            member_name_offset, LD_ELF_SHT_PROGBITS, text_flags, text_offset,
            4U, 0U, 0U, 4U, 0U);
    if (has_relocation) {
        test_elf_write_section(
                sections + (size_t) rela_index * LD_ELF64_SHDR_SIZE,
                rela_name_offset, LD_ELF_SHT_RELA,
                LD_ELF_SHF_INFO_LINK | LD_ELF_SHF_GROUP, relocation_offset,
                LD_ELF64_RELA_SIZE, symtab_index, text_index, 8U,
                LD_ELF64_RELA_SIZE);
    }
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name_offset, LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name_offset, LD_ELF_SHT_STRTAB, 0U,
            symbol_strings_offset, symbol_strings_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name_offset, LD_ELF_SHT_STRTAB, 0U,
            section_strings_offset, section_strings_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *make_test_elf_group_object(
        const char *signature, const char *member_name, uint32_t text_word,
        bool section_symbol_signature, test_elf_group_variant_t variant,
        size_t *result_size) {
    return make_test_elf_group_object_with_reference(
            signature, member_name, text_word, section_symbol_signature,
            variant, NULL, result_size);
}

static void test_elf_minimal_link(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_AARCH64, false, false,
                                           &object_size);
    char object_path[] = "/tmp/nature-ld-elf-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    static const char *output_path = "/tmp/nature-ld-elf-executable";
    unlink(output_path);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    mode_t image_mode;
    uint8_t *image = read_test_fixture(output_path, &image_size, &image_mode);
    assert(image_size >= LD_ELF64_EHDR_SIZE + 4U * LD_ELF64_PHDR_SIZE);
    test_elf_assert_executable_mode(image_mode);
    assert(image[0] == LD_ELF_MAGIC_0 && image[1] == LD_ELF_MAGIC_1 &&
           image[2] == LD_ELF_MAGIC_2 && image[3] == LD_ELF_MAGIC_3);
    assert(image[LD_ELF_EI_CLASS] == LD_ELF_CLASS_64);
    assert(image[LD_ELF_EI_DATA] == LD_ELF_DATA_LSB);
    assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_EXEC);
    assert(test_elf_read_u16(image + 18U) == LD_ELF_EM_AARCH64);
    assert(test_elf_read_u32(image + 20U) == LD_ELF_VERSION_CURRENT);
    assert(test_elf_read_u64(image + 32U) == LD_ELF64_EHDR_SIZE);
    const uint64_t section_offset = test_elf_read_u64(image + 40U);
    assert(test_elf_read_u16(image + 52U) == LD_ELF64_EHDR_SIZE);
    assert(test_elf_read_u16(image + 54U) == LD_ELF64_PHDR_SIZE);
    assert(test_elf_read_u16(image + 56U) == 4U);
    assert(test_elf_read_u16(image + 58U) == LD_ELF64_SHDR_SIZE);
    const uint16_t section_count = test_elf_read_u16(image + 60U);
    const uint16_t section_names_index = test_elf_read_u16(image + 62U);
    assert(section_offset != 0U && section_offset % 8U == 0U);
    assert(section_count == 5U && section_names_index == 4U);
    assert(section_offset <= image_size &&
           (uint64_t) section_count * LD_ELF64_SHDR_SIZE <=
                   image_size - section_offset);

    uint64_t entry = test_elf_read_u64(image + 24U);
    bool saw_phdr = false, saw_rx = false, saw_rw = false, saw_stack = false;
    uint64_t loaded_file_end = 0U;
    const size_t program_offset = (size_t) test_elf_read_u64(image + 32U);
    const uint16_t program_count = test_elf_read_u16(image + 56U);
    for (uint16_t i = 0; i < program_count; i++) {
        const uint8_t *program =
                image + program_offset + (size_t) i * LD_ELF64_PHDR_SIZE;
        uint32_t type = test_elf_read_u32(program);
        uint32_t flags = test_elf_read_u32(program + 4U);
        uint64_t offset = test_elf_read_u64(program + 8U);
        uint64_t address = test_elf_read_u64(program + 16U);
        uint64_t file_size = test_elf_read_u64(program + 32U);
        uint64_t memory_size = test_elf_read_u64(program + 40U);
        uint64_t alignment = test_elf_read_u64(program + 48U);
        assert(offset <= image_size && file_size <= image_size - offset);
        if (type == LD_ELF_PT_LOAD && offset + file_size > loaded_file_end)
            loaded_file_end = offset + file_size;
        if (type == LD_ELF_PT_PHDR) {
            saw_phdr = true;
            assert(flags == LD_ELF_PF_R);
            assert(offset == LD_ELF64_EHDR_SIZE);
            assert(file_size == (uint64_t) program_count *
                                        LD_ELF64_PHDR_SIZE);
            assert(memory_size == file_size && alignment == 8U);
        } else if (type == LD_ELF_PT_LOAD &&
                   flags == (LD_ELF_PF_R | LD_ELF_PF_X)) {
            saw_rx = true;
            assert(offset == 0U && address == 0x400000U);
            assert(alignment == 0x10000U && memory_size == file_size);
            assert(entry >= address && entry < address + memory_size);
            size_t entry_offset = (size_t) (entry - address);
            assert(entry_offset + sizeof(uint32_t) <= image_size);
            assert(test_elf_read_u32(image + entry_offset) == 0xd65f03c0U);
        } else if (type == LD_ELF_PT_LOAD &&
                   flags == (LD_ELF_PF_R | LD_ELF_PF_W)) {
            saw_rw = true;
            assert(alignment == 0x10000U && memory_size >= file_size);
            assert(offset % alignment == address % alignment);
        } else if (type == LD_ELF_PT_GNU_STACK) {
            saw_stack = true;
            assert(flags == (LD_ELF_PF_R | LD_ELF_PF_W));
            assert(file_size == 0U && memory_size == 0U && alignment == 16U);
        }
    }
    assert(saw_phdr && saw_rx && saw_rw && saw_stack);

    const uint8_t *section_headers = image + (size_t) section_offset;
    const uint8_t *text_section = section_headers + LD_ELF64_SHDR_SIZE;
    const uint8_t *symtab_section =
            section_headers + 2U * LD_ELF64_SHDR_SIZE;
    const uint8_t *strtab_section =
            section_headers + 3U * LD_ELF64_SHDR_SIZE;
    const uint8_t *names_section =
            section_headers + 4U * LD_ELF64_SHDR_SIZE;
    for (size_t i = 0; i < LD_ELF64_SHDR_SIZE; i++)
        assert(section_headers[i] == 0U);

    assert(test_elf_read_u32(text_section + 4U) == LD_ELF_SHT_PROGBITS);
    assert(test_elf_read_u64(text_section + 8U) ==
           (LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR));
    assert(test_elf_read_u64(text_section + 16U) == entry);
    assert(test_elf_read_u64(text_section + 24U) == entry - 0x400000U);
    assert(test_elf_read_u64(text_section + 32U) == sizeof(uint32_t));
    assert(test_elf_read_u64(text_section + 48U) == 4U);

    assert(test_elf_read_u32(symtab_section + 4U) == LD_ELF_SHT_SYMTAB);
    assert(test_elf_read_u64(symtab_section + 8U) == 0U);
    assert(test_elf_read_u64(symtab_section + 16U) == 0U);
    const uint64_t symtab_offset =
            test_elf_read_u64(symtab_section + 24U);
    const uint64_t symtab_size = test_elf_read_u64(symtab_section + 32U);
    assert(symtab_offset >= loaded_file_end && symtab_offset % 8U == 0U);
    assert(symtab_offset <= image_size &&
           symtab_size <= image_size - symtab_offset);
    assert(symtab_size == 6U * LD_ELF64_SYM_SIZE);
    assert(test_elf_read_u32(symtab_section + 40U) == 3U);
    assert(test_elf_read_u32(symtab_section + 44U) == 5U);
    assert(test_elf_read_u64(symtab_section + 48U) == 8U);
    assert(test_elf_read_u64(symtab_section + 56U) == LD_ELF64_SYM_SIZE);

    assert(test_elf_read_u32(strtab_section + 4U) == LD_ELF_SHT_STRTAB);
    assert(test_elf_read_u64(strtab_section + 8U) == 0U);
    assert(test_elf_read_u64(strtab_section + 16U) == 0U);
    const uint64_t strtab_offset =
            test_elf_read_u64(strtab_section + 24U);
    const uint64_t strtab_size = test_elf_read_u64(strtab_section + 32U);
    assert(strtab_offset == symtab_offset + symtab_size);
    assert(strtab_offset <= image_size &&
           strtab_size <= image_size - strtab_offset);
    assert(strtab_size == sizeof("\0_start"));
    assert(test_elf_read_u32(strtab_section + 40U) == 0U);
    assert(test_elf_read_u32(strtab_section + 44U) == 0U);
    assert(test_elf_read_u64(strtab_section + 48U) == 1U);
    assert(test_elf_read_u64(strtab_section + 56U) == 0U);

    assert(test_elf_read_u32(names_section + 4U) == LD_ELF_SHT_STRTAB);
    assert(test_elf_read_u64(names_section + 8U) == 0U);
    assert(test_elf_read_u64(names_section + 16U) == 0U);
    const uint64_t names_offset = test_elf_read_u64(names_section + 24U);
    const uint64_t names_size = test_elf_read_u64(names_section + 32U);
    assert(names_offset == strtab_offset + strtab_size);
    assert(names_offset >= loaded_file_end && names_offset <= section_offset);
    assert(names_size <= image_size - names_offset);
    assert(test_elf_read_u64(names_section + 48U) == 1U);
    assert(test_elf_read_u64(names_section + 56U) == 0U);

    const uint8_t *section_names = image + (size_t) names_offset;
    uint32_t text_name = test_elf_read_u32(text_section);
    uint32_t symtab_name = test_elf_read_u32(symtab_section);
    uint32_t strtab_name = test_elf_read_u32(strtab_section);
    uint32_t names_name = test_elf_read_u32(names_section);
    assert(text_name < names_size && symtab_name < names_size &&
           strtab_name < names_size && names_name < names_size);
    assert(strcmp((const char *) section_names + text_name, ".text") == 0);
    assert(strcmp((const char *) section_names + symtab_name, ".symtab") ==
           0);
    assert(strcmp((const char *) section_names + strtab_name, ".strtab") ==
           0);
    assert(strcmp((const char *) section_names + names_name, ".shstrtab") ==
           0);

    const uint8_t *symbols = image + (size_t) symtab_offset;
    for (size_t i = 0; i < LD_ELF64_SYM_SIZE; i++)
        assert(symbols[i] == 0U);
    const uint8_t *text_symbol = symbols + LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(text_symbol) == 0U);
    assert(text_symbol[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION));
    assert(test_elf_read_u16(text_symbol + 6U) == 1U);
    assert(test_elf_read_u64(text_symbol + 8U) == entry);
    for (uint16_t index = 2U; index <= 4U; index++) {
        const uint8_t *metadata_symbol =
                symbols + (size_t) index * LD_ELF64_SYM_SIZE;
        assert(test_elf_read_u32(metadata_symbol) == 0U);
        assert(metadata_symbol[4] ==
               LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION));
        assert(test_elf_read_u16(metadata_symbol + 6U) == index);
        assert(test_elf_read_u64(metadata_symbol + 8U) == 0U);
    }
    const uint8_t *start_symbol = symbols + 5U * LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(start_symbol) == 1U);
    assert(start_symbol[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC));
    assert(start_symbol[5] == LD_ELF_STV_DEFAULT);
    assert(test_elf_read_u16(start_symbol + 6U) == 1U);
    assert(test_elf_read_u64(start_symbol + 8U) == entry);
    assert(test_elf_read_u64(start_symbol + 16U) == sizeof(uint32_t));
    const char *symbol_strings = (const char *) image + strtab_offset;
    assert(symbol_strings[0] == '\0');
    assert(strcmp(symbol_strings + 1U, "_start") == 0);

    /* Global collection is sorted before encoding, so an unchanged link is
       byte-for-byte deterministic even though the resolver uses a hash
       table internally. */
    memset(&capture, 0, sizeof(capture));
    const char *repeat_inputs[] = {object_path};
    assert(link_test_elf_inputs(output_path, repeat_inputs, 1U, &capture) ==
           LD_OK);
    assert(capture.count == 0U);
    size_t repeated_size;
    uint8_t *repeated =
            read_test_fixture(output_path, &repeated_size, NULL);
    assert(repeated_size == image_size);
    assert(memcmp(repeated, image, image_size) == 0);
    free(repeated);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_executable_stack_note(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_AARCH64, false, true,
                                           &object_size);
    char object_path[] = "/tmp/nature-ld-execstack-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-execstack-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    assert(program_offset <= image_size &&
           (uint64_t) program_count * LD_ELF64_PHDR_SIZE <=
                   image_size - program_offset);
    bool found_stack = false;
    for (uint16_t i = 0; i < program_count; i++) {
        const uint8_t *program =
                image + (size_t) program_offset +
                (size_t) i * LD_ELF64_PHDR_SIZE;
        if (test_elf_read_u32(program) != LD_ELF_PT_GNU_STACK) continue;
        found_stack = true;
        assert(test_elf_read_u32(program + 4U) ==
               (LD_ELF_PF_R | LD_ELF_PF_W | LD_ELF_PF_X));
    }
    assert(found_stack);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_riscv_minimal_link(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_object(LD_ELF_EM_RISCV, false, false,
                                           &object_size);
    test_elf_write_u32(object + 48U, 0x5U);
    test_elf_write_u32(object + LD_ELF64_EHDR_SIZE, 0x00008067U);
    char object_path[] = "/tmp/nature-ld-riscv-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-riscv-executable";
    unlink(output_path);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    mode_t image_mode;
    uint8_t *image = read_test_fixture(output_path, &image_size, &image_mode);
    assert(image_size >= LD_ELF64_EHDR_SIZE + 4U * LD_ELF64_PHDR_SIZE);
    test_elf_assert_executable_mode(image_mode);
    assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_EXEC);
    assert(test_elf_read_u16(image + 18U) == LD_ELF_EM_RISCV);
    assert(test_elf_read_u32(image + 48U) == 0x5U);
    assert(test_elf_read_u16(image + 56U) == 4U);

    uint64_t entry = test_elf_read_u64(image + 24U);
    assert(entry >= 0x400000U && entry - 0x400000U <= image_size - 4U);
    assert(test_elf_read_u32(image + (size_t) (entry - 0x400000U)) ==
           0x00008067U);
    const uint8_t *rx = image + LD_ELF64_EHDR_SIZE + LD_ELF64_PHDR_SIZE;
    assert(test_elf_read_u32(rx) == LD_ELF_PT_LOAD);
    assert(test_elf_read_u64(rx + 48U) == 0x1000U);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void expect_invalid_elf_input(const uint8_t *bytes, size_t size,
                                     int expected,
                                     const char *message_fragment) {
    char input_path[] = "/tmp/nature-ld-invalid-elf-XXXXXX";
    write_fixture(input_path, bytes, size);
    static const char *output_path = "/tmp/nature-ld-invalid-elf-output";
    unlink(output_path);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    assert(ld_link(&options) == expected);
    assert(capture.count > 0U);
    assert(strstr(capture.message, message_fragment) != NULL);
    assert(access(output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(input_path);
}

static void test_elf_invalid_inputs(void) {
    static const uint8_t truncated[] = {0x7f, 'E', 'L', 'F'};
    expect_invalid_elf_input(truncated, sizeof(truncated), LD_INVALID_INPUT,
                             "truncated ELF header");

    size_t size;
    /* Raw e_machine values deliberately remain test-only: accepting their
       ELF32 forms must not add i386, Arm32, or RV32 production definitions. */
    static const uint16_t excluded_elf32_machines[] = {
            3U, /* EM_386 */
            40U, /* EM_ARM */
            LD_ELF_EM_RISCV, /* RV32 is distinguished by EI_CLASS. */
    };
    for (size_t i = 0;
         i < sizeof(excluded_elf32_machines) /
                     sizeof(excluded_elf32_machines[0]);
         i++) {
        uint8_t *elf32 = make_test_elf_object(
                LD_ELF_EM_AARCH64, false, false, &size);
        elf32[LD_ELF_EI_CLASS] = 1U;
        test_elf_write_u16(elf32 + 18U, excluded_elf32_machines[i]);
        expect_invalid_elf_input(elf32, size, LD_UNSUPPORTED,
                                 "expected ELF64 little-endian");
        free(elf32);
    }

    ld_options_t unsupported_options;
    ld_options_init(&unsupported_options);
    diagnostic_capture_t unsupported_capture = {0};
    unsupported_options.os = LD_OS_LINUX;
    unsupported_options.arch = (ld_arch_t) 99;
    unsupported_options.diagnostic = capture_diagnostic;
    unsupported_options.diagnostic_context = &unsupported_capture;
    assert(ld_link_elf(&unsupported_options) == LD_UNSUPPORTED);
    assert(unsupported_capture.count == 1U);
    assert(strstr(unsupported_capture.message,
                  "x86_64, aarch64, and riscv64") != NULL);
    ld_options_deinit(&unsupported_options);

    uint8_t *wrong_arch =
            make_test_elf_object(LD_ELF_EM_X86_64, false, false, &size);
    expect_invalid_elf_input(wrong_arch, size, LD_UNSUPPORTED,
                             "architecture mismatch");
    free(wrong_arch);
}

static void test_elf_unreferenced_ifunc_definition(void) {
    size_t object_size;
    uint8_t *object =
            make_test_elf_object(LD_ELF_EM_X86_64, false, false,
                                 &object_size);
    size_t section_table_offset = (size_t) test_elf_read_u64(object + 40U);
    assert(section_table_offset <= object_size &&
           3U * LD_ELF64_SHDR_SIZE <= object_size - section_table_offset);
    const uint8_t *symbol_section =
            object + section_table_offset + 2U * LD_ELF64_SHDR_SIZE;
    size_t symbol_table_offset =
            (size_t) test_elf_read_u64(symbol_section + 24U);
    assert(symbol_table_offset <= object_size - 2U * LD_ELF64_SYM_SIZE);
    uint8_t *start_symbol =
            object + symbol_table_offset + LD_ELF64_SYM_SIZE;
    start_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_GNU_IFUNC);

    char object_path[] = "/tmp/nature-ld-ifunc-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    static const char *output_path = "/tmp/nature-ld-ifunc-output";
    unlink(output_path);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link_elf(&options) == LD_OK);
    assert(capture.count == 0U);
    assert(access(output_path, X_OK) == 0);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(test_elf_find_output_section(image, image_size, ".plt.got") ==
           NULL);
    assert(test_elf_find_output_section(image, image_size, ".rela.dyn") ==
           NULL);
    free(image);

    ld_options_deinit(&options);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_aarch64_thunk_helpers(void) {
    const uint64_t place = UINT64_C(0x10000000);
    int64_t displacement = INT64_MIN;
    assert(ld_elf_aarch64_branch26_fits(place, place, &displacement));
    assert(displacement == 0);
    assert(ld_elf_aarch64_branch26_fits(
            place + UINT64_C(0x07fffffc), place, &displacement));
    assert(displacement == INT64_C(0x07fffffc));
    assert(!ld_elf_aarch64_branch26_fits(
            place + UINT64_C(0x08000000), place, NULL));
    assert(ld_elf_aarch64_branch26_fits(
            place - UINT64_C(0x08000000), place, &displacement));
    assert(displacement == -INT64_C(0x08000000));
    assert(!ld_elf_aarch64_branch26_fits(
            place - UINT64_C(0x08000004), place, NULL));
    assert(!ld_elf_aarch64_branch26_fits(place + 2U, place, NULL));

    uint8_t encoded[LD_ELF_AARCH64_THUNK_SIZE];
    memset(encoded, 0xa5, sizeof(encoded));
    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x401000), UINT64_C(0x401234)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_OK);
    assert(test_elf_read_u32(encoded) == 0x90000010U);
    assert(test_elf_read_u32(encoded + 4U) == 0x9108d210U);
    assert(test_elf_read_u32(encoded + 8U) == 0xd61f0200U);

    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x402000), UINT64_C(0x401234)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_OK);
    assert(test_elf_read_u32(encoded) == 0xf0fffff0U);
    assert(test_elf_read_u32(encoded + 4U) == 0x9108d210U);
    assert(test_elf_read_u32(encoded + 8U) == 0xd61f0200U);

    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x100000000),
                   UINT64_C(0x100000000) +
                           (UINT64_C(0xfffff) << 12U)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_OK);
    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x200000000),
                   UINT64_C(0x200000000) -
                           (UINT64_C(0x100000) << 12U)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_OK);

    memset(encoded, 0xa5, sizeof(encoded));
    uint8_t before[sizeof(encoded)];
    memcpy(before, encoded, sizeof(before));
    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x100000000),
                   UINT64_C(0x100000000) +
                           (UINT64_C(0x100000) << 12U)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_RANGE);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x200000000),
                   UINT64_C(0x200000000) -
                           (UINT64_C(0x100001) << 12U)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_RANGE);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
    assert(ld_elf_aarch64_thunk_encode(
                   encoded, UINT64_C(0x401000), UINT64_C(0x401002)) ==
           LD_ELF_AARCH64_THUNK_ENCODE_UNALIGNED);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);

    ld_elf_aarch64_thunk_plan_t plan;
    ld_elf_aarch64_thunk_plan_init(&plan);
    uint32_t group_index;
    assert(ld_elf_aarch64_thunk_group_append(
            &plan, 0U, 7U, &group_index));
    assert(group_index == 0U);
    assert(plan.group_count == 1U);
    assert(plan.groups[0].first_placement_index == 7U);
    assert(plan.groups[0].last_placement_index == 7U);

    ld_elf_aarch64_thunk_key_t global_key = {
            .global = (ld_elf_global_t *) (uintptr_t) 1U,
            .addend = 0,
    };
    uint32_t entry_index;
    bool added;
    assert(ld_elf_aarch64_thunk_entry_find_or_add(
            &plan.groups[0], &global_key, &entry_index, &added));
    assert(added && entry_index == 0U);
    assert(ld_elf_aarch64_thunk_entry_find_or_add(
            &plan.groups[0], &global_key, &entry_index, &added));
    assert(!added && entry_index == 0U);

    global_key.addend = 4;
    assert(ld_elf_aarch64_thunk_entry_find_or_add(
            &plan.groups[0], &global_key, &entry_index, &added));
    assert(added && entry_index == 1U);
    ld_elf_aarch64_thunk_key_t local_key = {
            .object = (ld_elf_object_t *) (uintptr_t) 1U,
            .symbol_index = 3U,
            .addend = 0,
    };
    assert(ld_elf_aarch64_thunk_entry_find_or_add(
            &plan.groups[0], &local_key, &entry_index, &added));
    assert(added && entry_index == 2U);
    local_key.object = (ld_elf_object_t *) (uintptr_t) 2U;
    assert(ld_elf_aarch64_thunk_entry_find_or_add(
            &plan.groups[0], &local_key, &entry_index, &added));
    assert(added && entry_index == 3U);
    ld_elf_aarch64_thunk_plan_deinit(&plan);
    assert(plan.groups == NULL && plan.group_count == 0U);
}

static void test_elf_comdat_winner_order(void) {
    const uint32_t first_word = 0xd65f03c0U;
    const uint32_t second_word = 0xd503201fU;
    size_t first_size, second_size;
    uint8_t *first = make_test_elf_group_object(
            "comdat.entry", ".text.comdat_entry", first_word, false,
            TEST_ELF_GROUP_VALID, &first_size);
    uint8_t *second = make_test_elf_group_object(
            "comdat.entry", ".text.comdat_entry", second_word, false,
            TEST_ELF_GROUP_VALID, &second_size);
    char first_path[] = "/tmp/nature-ld-comdat-first-XXXXXX";
    char second_path[] = "/tmp/nature-ld-comdat-second-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path = "/tmp/nature-ld-comdat-output";
    diagnostic_capture_t capture = {0};
    const char *forward[] = {first_path, second_path};
    assert(link_test_elf_inputs(output_path, forward, 2U, &capture) == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == first_word);

    memset(&capture, 0, sizeof(capture));
    const char *reverse[] = {second_path, first_path};
    assert(link_test_elf_inputs(output_path, reverse, 2U, &capture) == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == second_word);

    unlink(first_path);
    unlink(second_path);
    unlink(output_path);
}

static void test_elf_comdat_distinct_signatures(void) {
    size_t first_size, second_size;
    uint8_t *first = make_test_elf_group_object(
            "comdat.first", ".text.comdat_entry", 0xd65f03c0U, false,
            TEST_ELF_GROUP_VALID, &first_size);
    uint8_t *second = make_test_elf_group_object(
            "comdat.second", ".text.comdat_entry", 0xd503201fU, false,
            TEST_ELF_GROUP_VALID, &second_size);
    char first_path[] = "/tmp/nature-ld-comdat-distinct-a-XXXXXX";
    char second_path[] = "/tmp/nature-ld-comdat-distinct-b-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path =
            "/tmp/nature-ld-comdat-distinct-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {first_path, second_path};
    assert(link_test_elf_inputs(output_path, inputs, 2U, &capture) ==
           LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "duplicate strong ELF symbol") != NULL);
    assert(strstr(capture.message, "_start") != NULL);
    assert(access(output_path, F_OK) != 0);

    unlink(first_path);
    unlink(second_path);
}

static void test_elf_ordinary_group_is_not_comdat(void) {
    size_t first_size, second_size;
    uint8_t *first = make_test_elf_group_object(
            "ordinary.group", ".text.ordinary_group", 0xd65f03c0U, false,
            TEST_ELF_GROUP_ORDINARY, &first_size);
    uint8_t *second = make_test_elf_group_object(
            "ordinary.group", ".text.ordinary_group", 0xd503201fU, false,
            TEST_ELF_GROUP_ORDINARY, &second_size);
    char first_path[] = "/tmp/nature-ld-ordinary-group-a-XXXXXX";
    char second_path[] = "/tmp/nature-ld-ordinary-group-b-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path =
            "/tmp/nature-ld-ordinary-group-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {first_path, second_path};
    assert(link_test_elf_inputs(output_path, inputs, 2U, &capture) ==
           LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "duplicate strong ELF symbol") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(first_path);
    unlink(second_path);
}

static void test_elf_comdat_live_winner_undefined(void) {
    size_t winner_size, loser_size;
    uint8_t *winner = make_test_elf_group_object_with_reference(
            "comdat.undefined", ".text.comdat_undefined", 0U, false,
            TEST_ELF_GROUP_VALID, "foobar", &winner_size);
    uint8_t *loser = make_test_elf_group_object(
            "comdat.undefined", ".text.comdat_undefined", 0xd503201fU,
            false, TEST_ELF_GROUP_VALID, &loser_size);
    char winner_path[] = "/tmp/nature-ld-comdat-live-undef-a-XXXXXX";
    char loser_path[] = "/tmp/nature-ld-comdat-live-undef-b-XXXXXX";
    write_fixture(winner_path, winner, winner_size);
    write_fixture(loser_path, loser, loser_size);
    free(winner);
    free(loser);

    static const char *output_path =
            "/tmp/nature-ld-comdat-live-undef-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {winner_path, loser_path};
    assert(link_test_elf_inputs(output_path, inputs, 2U, &capture) ==
           LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "undefined ELF symbol 'foobar'") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(winner_path);
    unlink(loser_path);
}

static void test_elf_comdat_section_symbol_signature(void) {
    const uint32_t first_word = 0xd65f03c0U;
    const uint32_t second_word = 0xd503201fU;
    size_t first_size, second_size;
    uint8_t *first = make_test_elf_group_object(
            "unused.first", ".text.fallback", first_word, true,
            TEST_ELF_GROUP_VALID, &first_size);
    uint8_t *second = make_test_elf_group_object(
            "unused.second", ".text.fallback", second_word, true,
            TEST_ELF_GROUP_VALID, &second_size);
    char first_path[] = "/tmp/nature-ld-group-fallback-a-XXXXXX";
    char second_path[] = "/tmp/nature-ld-group-fallback-b-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(second_path, second, second_size);
    free(first);
    free(second);

    static const char *output_path =
            "/tmp/nature-ld-group-fallback-output";
    diagnostic_capture_t capture = {0};
    const char *same_section[] = {first_path, second_path};
    assert(link_test_elf_inputs(output_path, same_section, 2U, &capture) ==
           LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == first_word);
    unlink(first_path);
    unlink(second_path);
    unlink(output_path);

    first = make_test_elf_group_object(
            "unused.first", ".text.fallback_first", first_word, true,
            TEST_ELF_GROUP_VALID, &first_size);
    second = make_test_elf_group_object(
            "unused.second", ".text.fallback_second", second_word, true,
            TEST_ELF_GROUP_VALID, &second_size);
    char distinct_first_path[] =
            "/tmp/nature-ld-group-fallback-distinct-a-XXXXXX";
    char distinct_second_path[] =
            "/tmp/nature-ld-group-fallback-distinct-b-XXXXXX";
    write_fixture(distinct_first_path, first, first_size);
    write_fixture(distinct_second_path, second, second_size);
    free(first);
    free(second);

    memset(&capture, 0, sizeof(capture));
    const char *distinct_sections[] = {distinct_first_path,
                                       distinct_second_path};
    assert(link_test_elf_inputs(output_path, distinct_sections, 2U,
                                &capture) == LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "duplicate strong ELF symbol") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(distinct_first_path);
    unlink(distinct_second_path);
}

static void test_elf_malformed_groups(void) {
    static const struct {
        test_elf_group_variant_t variant;
        const char *diagnostic;
    } cases[] = {
            {TEST_ELF_GROUP_BAD_LINK, "SHT_SYMTAB"},
            {TEST_ELF_GROUP_BAD_INFO, "signature symbol"},
            {TEST_ELF_GROUP_BAD_FLAGS, "unsupported flag"},
            {TEST_ELF_GROUP_BAD_SIZE, "invalid size"},
            {TEST_ELF_GROUP_BAD_ENTSIZE, "invalid sh_entsize"},
            {TEST_ELF_GROUP_EMPTY, "invalid size"},
            {TEST_ELF_GROUP_BAD_MEMBER, "invalid member"},
            {TEST_ELF_GROUP_MEMBER_WITHOUT_FLAG, "SHF_GROUP"},
            {TEST_ELF_GROUP_DUPLICATE_OWNER, "already owned by group"},
            {TEST_ELF_GROUP_ORPHAN_MEMBER, "no SHT_GROUP owner"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = make_test_elf_group_object(
                "malformed.group", ".text.malformed_group", 0xd65f03c0U,
                false, cases[i].variant, &object_size);
        expect_invalid_elf_input(object, object_size, LD_INVALID_INPUT,
                                 cases[i].diagnostic);
        free(object);
    }
}

static void test_elf_comdat_loser_only_undefined(void) {
    const uint32_t winner_word = 0xd65f03c0U;
    const uint32_t archive_word = 0x6badc0deU;
    size_t winner_size, loser_size, provider_size;
    uint8_t *winner = make_test_elf_group_object(
            "comdat.loser_undefined", ".text.comdat_loser_undefined",
            winner_word, false, TEST_ELF_GROUP_VALID, &winner_size);
    uint8_t *loser = make_test_elf_group_object_with_reference(
            "comdat.loser_undefined", ".text.comdat_loser_undefined", 0U,
            false, TEST_ELF_GROUP_VALID, "foobar", &loser_size);
    uint8_t *provider = make_test_elf_object(LD_ELF_EM_AARCH64, false,
                                             false, &provider_size);
    test_elf_write_u32(provider + LD_ELF64_EHDR_SIZE, archive_word);
    memcpy(provider + LD_ELF64_EHDR_SIZE + sizeof(uint32_t) + 1U,
           "foobar", sizeof("foobar") - 1U);

    char winner_path[] = "/tmp/nature-ld-comdat-loser-undef-a-XXXXXX";
    char loser_path[] = "/tmp/nature-ld-comdat-loser-undef-b-XXXXXX";
    write_fixture(winner_path, winner, winner_size);
    write_fixture(loser_path, loser, loser_size);
    free(winner);
    free(loser);

    size_t archive_capacity = LD_ELF_AR_MAGIC_SIZE +
                              LD_ELF_AR_HEADER_SIZE + provider_size + 1U;
    uint8_t *archive = calloc(1, archive_capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t archive_size = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, archive_capacity, &archive_size,
                        "provider.o/", provider, provider_size);
    free(provider);
    char archive_path[] = "/tmp/nature-ld-comdat-loser-archive-XXXXXX";
    write_fixture(archive_path, archive, archive_size);
    free(archive);

    static const char *output_path =
            "/tmp/nature-ld-comdat-loser-undef-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {winner_path, loser_path, archive_path};
    assert(link_test_elf_inputs(output_path, inputs, 3U, &capture) == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == winner_word);
    assert(!test_elf_file_contains_u32(output_path, archive_word));

    unlink(winner_path);
    unlink(loser_path);
    unlink(archive_path);
    unlink(output_path);
}

void test_ld_elf_basic(void) {
    test_elf_minimal_link();
    test_elf_executable_stack_note();
    test_elf_riscv_minimal_link();
    test_elf_invalid_inputs();
    test_elf_unreferenced_ifunc_definition();
    test_elf_aarch64_thunk_helpers();
    test_elf_comdat_winner_order();
    test_elf_comdat_distinct_signatures();
    test_elf_ordinary_group_is_not_comdat();
    test_elf_comdat_section_symbol_signature();
    test_elf_comdat_live_winner_undefined();
    test_elf_comdat_loser_only_undefined();
    test_elf_malformed_groups();
}
