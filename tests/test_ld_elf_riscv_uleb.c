#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_reloc.h"
#include "src/ld/ld_elf_riscv_uleb.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_elf_riscv_uleb_encoder(void) {
    size_t width = SIZE_MAX;
    uint8_t one_byte[] = {0x19U};
    assert(ld_elf_riscv_uleb_apply_pair(
                   one_byte, sizeof(one_byte), 0x1001cU, 0,
                   0x10003U, 0, &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 1U && one_byte[0] == 0x19U);

    uint8_t two_bytes[] = {0x9eU, 0x02U};
    assert(ld_elf_riscv_uleb_apply_pair(
                   two_bytes, sizeof(two_bytes), 0x1011eU, 0,
                   0x10000U, 0, &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 2U && two_bytes[0] == 0x9eU &&
           two_bytes[1] == 0x02U);

    uint8_t with_addends[] = {0x80U, 0x00U};
    assert(ld_elf_riscv_uleb_apply_pair(
                   with_addends, sizeof(with_addends), 0x100U, 5,
                   0x80U, 1, &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 2U && with_addends[0] == 0x86U &&
           with_addends[1] == 0x01U);

    uint8_t standalone = 0x00U;
    assert(ld_elf_riscv_uleb_apply(
                   &standalone, 1U, false, 0x105U, 0,
                   &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 1U && standalone == 0x05U);
    assert(ld_elf_riscv_uleb_apply(
                   &standalone, 1U, true, 2U, 0,
                   &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 1U && standalone == 0x03U);

    uint8_t maximum[10];
    memset(maximum, 0x80, sizeof(maximum));
    maximum[9] = 0U;
    assert(ld_elf_riscv_uleb_apply_pair(
                   maximum, sizeof(maximum), UINT64_MAX, 0, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 10U);
    for (size_t i = 0; i < 9U; i++) assert(maximum[i] == 0xffU);
    assert(maximum[9] == 0x01U);
}

static void test_elf_riscv_uleb_encoder_failures(void) {
    size_t width = SIZE_MAX;
    uint8_t truncated[] = {0x80U};
    uint8_t truncated_copy[sizeof(truncated)];
    memcpy(truncated_copy, truncated, sizeof(truncated));
    assert(ld_elf_riscv_uleb_apply_pair(
                   truncated, sizeof(truncated), 1U, 0, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_TRUNCATED);
    assert(width == 0U &&
           memcmp(truncated, truncated_copy, sizeof(truncated)) == 0);

    uint8_t field_overflow[10];
    memset(field_overflow, 0x80, sizeof(field_overflow));
    field_overflow[9] = 0x02U;
    uint8_t field_overflow_copy[sizeof(field_overflow)];
    memcpy(field_overflow_copy, field_overflow, sizeof(field_overflow));
    assert(ld_elf_riscv_uleb_apply_pair(
                   field_overflow, sizeof(field_overflow), 1U, 0, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_FIELD_OVERFLOW);
    assert(width == 0U &&
           memcmp(field_overflow, field_overflow_copy,
                  sizeof(field_overflow)) == 0);

    uint8_t byte = 0x55U;
    assert(ld_elf_riscv_uleb_apply_pair(
                   &byte, 1U, UINT64_MAX, 1, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_SET_OVERFLOW);
    assert(width == 1U && byte == 0x55U);
    assert(ld_elf_riscv_uleb_apply_pair(
                   &byte, 1U, 0U, -1, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_SET_OVERFLOW);
    assert(byte == 0x55U);
    assert(ld_elf_riscv_uleb_apply_pair(
                   &byte, 1U, 1U, 0, 0U, 1,
                   &width) == LD_ELF_RISCV_ULEB_SUB_OVERFLOW);
    assert(byte == 0x55U);
    assert(ld_elf_riscv_uleb_apply_pair(
                   &byte, 1U, 1U, 0, 2U, 0,
                   &width) == LD_ELF_RISCV_ULEB_OK);
    assert(byte == 0x7fU);
    byte = 0x55U;
    assert(ld_elf_riscv_uleb_apply_pair(
                   &byte, 1U, 128U, 0, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_OK);
    assert(width == 1U && byte == 0U);
    assert(ld_elf_riscv_uleb_apply_pair(
                   NULL, 0U, 0U, 0, 0U, 0,
                   &width) == LD_ELF_RISCV_ULEB_INVALID_ARGUMENT);
}

static ld_elf_relocation_t test_elf_riscv_uleb_relocation(
        uint32_t type, uint64_t offset, uint32_t symbol_index,
        uint32_t relocation_section_index) {
    ld_elf_relocation_t relocation = {
            .offset = offset,
            .symbol_index = symbol_index,
            .type = type,
            .relocation_section_index = relocation_section_index,
            .x86_tls_pair_index = SIZE_MAX,
    };
    return relocation;
}

static void test_elf_riscv_uleb_pair_diagnostics(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_RISCV;
    object.display_name = (char *) "uleb-apply.o";
    ld_elf_section_t section = {0};
    section.name = ".gcc_except_table";
    ld_elf_relocation_t set = test_elf_riscv_uleb_relocation(
            LD_ELF_R_RISCV_SET_ULEB128, 2U, 1U, 7U);
    ld_elf_relocation_t sub = test_elf_riscv_uleb_relocation(
            LD_ELF_R_RISCV_SUB_ULEB128, 2U, 2U, 7U);
    ld_elf_reloc_values_t set_values = {
            .symbol_address = 0x1001cU,
            .symbol_name = "set_target",
    };
    ld_elf_reloc_values_t sub_values = {
            .symbol_address = 0x10003U,
            .symbol_name = "sub_target",
    };
    uint8_t field = 0U;
    assert(ld_elf_relocation_apply_riscv_uleb_pair(
                   &ctx, &object, &section, &set, &sub, &field, 1U,
                   &set_values, &sub_values) == LD_OK);
    assert(field == 0x19U && capture.count == 0U);

    field = 0x55U;
    set_values.symbol_address = 1U;
    sub_values.symbol_address = 2U;
    assert(ld_elf_relocation_apply_riscv_uleb_pair(
                   &ctx, &object, &section, &set, &sub, &field, 1U,
                   &set_values, &sub_values) == LD_OK);
    assert(field == 0x7fU && capture.count == 0U);

    capture = (diagnostic_capture_t) {0};
    field = 0x55U;
    sub.offset = 3U;
    assert(ld_elf_relocation_apply_riscv_uleb_pair(
                   &ctx, &object, &section, &set, &sub, &field, 1U,
                   &set_values, &sub_values) == LD_RELOCATION_ERROR);
    assert(field == 0x55U && strstr(capture.message, "invalid"));

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

typedef enum {
    TEST_ELF_RISCV_ULEB_VALID = 0,
    TEST_ELF_RISCV_ULEB_MISSING_SUB,
    TEST_ELF_RISCV_ULEB_REVERSE,
    TEST_ELF_RISCV_ULEB_TRUNCATED,
} test_elf_riscv_uleb_fixture_mode_t;

static uint8_t *test_elf_make_riscv_uleb_fixture(
        test_elf_riscv_uleb_fixture_mode_t mode, size_t *result_size) {
    char symbol_names[64] = {0};
    size_t symbol_name_cursor = 1U;
    uint32_t set_symbol_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_name_cursor,
            ".Lset");
    uint32_t sub_symbol_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_name_cursor,
            ".Lsub");
    uint32_t start_symbol_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_name_cursor,
            "_start");

    char section_names[128] = {0};
    size_t section_name_cursor = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".text");
    uint32_t except_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".gcc_except_table");
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".shstrtab");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_name_cursor,
            ".rela.gcc_except_table");

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t text_size = 256U;
    const size_t except_offset = text_offset + text_size;
    const size_t symtab_offset = test_elf_align(except_offset + 1U, 8U);
    const size_t symbol_count = 4U;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t rela_offset =
            test_elf_align(shstrtab_offset + sizeof(section_names), 8U);
    size_t relocation_count =
            mode == TEST_ELF_RISCV_ULEB_MISSING_SUB ? 1U : 2U;
    const size_t section_table_offset =
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE;
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
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_RISCV);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 5U);

    test_elf_write_u32(bytes + text_offset, 0x00000513U);
    test_elf_write_u32(bytes + text_offset + 4U, 0x00008067U);
    bytes[except_offset] =
            mode == TEST_ELF_RISCV_ULEB_TRUNCATED ? 0x80U : 0x19U;
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, sizeof(section_names));

    uint64_t set_value = 0x1cU;
    uint64_t sub_value = 0x03U;
    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *set_symbol = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(set_symbol, set_symbol_name);
    set_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(set_symbol + 6U, 1U);
    test_elf_write_u64(set_symbol + 8U, set_value);
    uint8_t *sub_symbol = set_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(sub_symbol, sub_symbol_name);
    sub_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(sub_symbol + 6U, 1U);
    test_elf_write_u64(sub_symbol + 8U, sub_value);
    uint8_t *start_symbol = sub_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start_symbol, start_symbol_name);
    start_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start_symbol + 6U, 1U);
    test_elf_write_u64(start_symbol + 16U, 8U);

    uint8_t *rela = bytes + rela_offset;
    uint32_t first_type = mode == TEST_ELF_RISCV_ULEB_REVERSE
                                  ? LD_ELF_R_RISCV_SUB_ULEB128
                                  : LD_ELF_R_RISCV_SET_ULEB128;
    uint32_t first_symbol = mode == TEST_ELF_RISCV_ULEB_REVERSE ? 2U : 1U;
    test_elf_write_u64(rela, 0U);
    test_elf_write_u64(rela + 8U,
                       LD_ELF_RELA_INFO(first_symbol, first_type));
    if (relocation_count == 2U) {
        uint32_t second_type = mode == TEST_ELF_RISCV_ULEB_REVERSE
                                       ? LD_ELF_R_RISCV_SET_ULEB128
                                       : LD_ELF_R_RISCV_SUB_ULEB128;
        uint32_t second_symbol =
                mode == TEST_ELF_RISCV_ULEB_REVERSE ? 1U : 2U;
        test_elf_write_u64(rela + LD_ELF64_RELA_SIZE, 0U);
        test_elf_write_u64(rela + LD_ELF64_RELA_SIZE + 8U,
                           LD_ELF_RELA_INFO(second_symbol, second_type));
    }

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, except_name,
                           LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC,
                           except_offset, 1U, 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, symtab_name,
                           LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
                           symbol_count * LD_ELF64_SYM_SIZE, 4U, 3U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, strtab_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE,
                           shstrtab_name, LD_ELF_SHT_STRTAB, 0U,
                           shstrtab_offset, sizeof(section_names), 0U, 0U,
                           1U, 0U);
    test_elf_write_section(sections + 6U * LD_ELF64_SHDR_SIZE, rela_name,
                           LD_ELF_SHT_RELA, LD_ELF_SHF_INFO_LINK,
                           rela_offset,
                           relocation_count * LD_ELF64_RELA_SIZE, 3U, 2U,
                           8U, LD_ELF64_RELA_SIZE);

    *result_size = size;
    return bytes;
}

static int test_elf_link_riscv_uleb_fixture(
        test_elf_riscv_uleb_fixture_mode_t mode, const char *output_path,
        diagnostic_capture_t *capture) {
    size_t fixture_size;
    uint8_t *fixture = test_elf_make_riscv_uleb_fixture(
            mode, &fixture_size);
    char input_path[] = "/tmp/nature-ld-riscv-uleb-input-XXXXXX";
    write_fixture(input_path, fixture, fixture_size);
    free(fixture);
    unlink(output_path);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    int status = ld_link(&options);
    ld_options_deinit(&options);
    unlink(input_path);
    return status;
}

static void test_elf_riscv_uleb_full_link(void) {
    char output_path[] = "/tmp/nature-ld-riscv-uleb-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0 && close(output_fd) == 0);
    unlink(output_path);

    diagnostic_capture_t capture = {0};
    assert(test_elf_link_riscv_uleb_fixture(
                   TEST_ELF_RISCV_ULEB_VALID, output_path, &capture) == LD_OK);
    assert(capture.count == 0U);
    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *header = test_elf_find_output_section(
            image, image_size, ".gcc_except_table");
    assert(header != NULL);
    uint64_t file_offset = test_elf_read_u64(header + 24U);
    uint64_t section_size = test_elf_read_u64(header + 32U);
    assert(section_size == 1U && file_offset < image_size);
    assert(image[file_offset] == 0x19U);
    free(image);
    unlink(output_path);

    capture = (diagnostic_capture_t) {0};
    assert(test_elf_link_riscv_uleb_fixture(
                   TEST_ELF_RISCV_ULEB_MISSING_SUB, output_path,
                   &capture) == LD_OK);
    image = read_test_fixture(output_path, &image_size, NULL);
    header = test_elf_find_output_section(
            image, image_size, ".gcc_except_table");
    assert(header != NULL);
    file_offset = test_elf_read_u64(header + 24U);
    assert(file_offset < image_size && image[file_offset] == 0x3cU);
    free(image);
    unlink(output_path);

    capture = (diagnostic_capture_t) {0};
    assert(test_elf_link_riscv_uleb_fixture(
                   TEST_ELF_RISCV_ULEB_REVERSE, output_path,
                   &capture) == LD_OK);
    image = read_test_fixture(output_path, &image_size, NULL);
    header = test_elf_find_output_section(
            image, image_size, ".gcc_except_table");
    assert(header != NULL);
    file_offset = test_elf_read_u64(header + 24U);
    assert(file_offset < image_size && image[file_offset] == 0x3cU);
    free(image);
    unlink(output_path);

    capture = (diagnostic_capture_t) {0};
    assert(test_elf_link_riscv_uleb_fixture(
                   TEST_ELF_RISCV_ULEB_TRUNCATED, output_path,
                   &capture) == LD_RELOCATION_ERROR);
    assert(strstr(capture.message, "truncated") &&
           access(output_path, F_OK) != 0);
}

void test_ld_elf_riscv_uleb(void) {
    test_elf_riscv_uleb_encoder();
    test_elf_riscv_uleb_encoder_failures();
    test_elf_riscv_uleb_pair_diagnostics();
    test_elf_riscv_uleb_full_link();
}
