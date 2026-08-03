#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_rel.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint8_t *bytes;
    size_t size;
    size_t text_offset;
    size_t rel_offset;
    size_t section_table_offset;
} test_elf_rel_fixture_t;

static ld_arch_t test_elf_rel_arch(uint16_t machine) {
    if (machine == LD_ELF_EM_X86_64) return LD_ARCH_AMD64;
    if (machine == LD_ELF_EM_AARCH64) return LD_ARCH_ARM64;
    assert(machine == LD_ELF_EM_RISCV);
    return LD_ARCH_RISCV64;
}

static uint32_t test_elf_rel_riscv_jal(int32_t displacement) {
    uint32_t immediate = (uint32_t) displacement & 0x1fffffU;
    uint32_t instruction = 0x0000006fU;
    instruction |= (immediate & 0x100000U) << 11U;
    instruction |= (immediate & 0x0007feU) << 20U;
    instruction |= (immediate & 0x000800U) << 9U;
    instruction |= immediate & 0x0ff000U;
    return instruction;
}

static uint32_t test_elf_rel_aarch64_adr(uint32_t opcode,
                                         int64_t immediate, bool page) {
    uint64_t encoded = (uint64_t) (page ? immediate / 4096 : immediate) &
                       0x1fffffU;
    return opcode | (uint32_t) ((encoded & 3U) << 29U) |
           (uint32_t) (((encoded >> 2U) & 0x7ffffU) << 5U);
}

static int test_elf_rel_decode_direct(uint16_t machine, uint32_t type,
                                      const uint8_t *data, size_t size,
                                      int64_t *addend,
                                      diagnostic_capture_t *capture) {
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = test_elf_rel_arch(machine);
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = machine;
    object.display_name = (char *) "direct-rel-test.o";
    ld_elf_section_t section = {0};
    section.header.sh_type = LD_ELF_SHT_PROGBITS;
    section.name = ".text";
    section.data = data;
    section.data_size = size;
    ld_elf_relocation_t relocation = {
            .type = type,
    };
    int result = ld_elf_rel_decode_addend(
            &ctx, &object, &section, &relocation, addend);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
    return result;
}

static void test_elf_rel_direct_decoders(void) {
    uint8_t data[16] = {0};
    diagnostic_capture_t capture = {0};
    int64_t addend;

    test_elf_write_u64(data, UINT64_MAX - 7U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_X86_64, LD_ELF_R_X86_64_64, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -8 && capture.count == 0U);
    test_elf_write_u32(data, UINT32_MAX);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_X86_64, LD_ELF_R_X86_64_32, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == (int64_t) UINT32_MAX);
    test_elf_write_u16(data, UINT16_MAX - 1U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_X86_64, LD_ELF_R_X86_64_PC16, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -2);

    test_elf_write_u32(
            data, test_elf_rel_aarch64_adr(0x90000000U, -4096, true));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64,
                   LD_ELF_R_AARCH64_ADR_PREL_PG_HI21, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -4096);
    test_elf_write_u32(
            data, test_elf_rel_aarch64_adr(0x10000000U, -4, false));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_ADR_PREL_LO21,
                   data, sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -4);
    test_elf_write_u32(data, 0x91000000U | (0xabcU << 10U));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64,
                   LD_ELF_R_AARCH64_ADD_ABS_LO12_NC, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == 0xabc);
    test_elf_write_u32(data, 0x91400000U | (0x123U << 10U));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64,
                   LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == 0x123000);
    test_elf_write_u32(data, 0xf9400000U | (0x12U << 10U));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64,
                   LD_ELF_R_AARCH64_LD64_GOT_LO12_NC, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == 0x90);
    test_elf_write_u32(data,
                       0xd2800000U | (2U << 21U) | (0x1234U << 5U));
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_MOVW_UABS_G2,
                   data, sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == INT64_C(0x123400000000));

    test_elf_write_u64(data, UINT64_MAX - 7U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -8);
    test_elf_write_u32(data, 0x00001097U);
    test_elf_write_u32(data + 4U, 0x234080e7U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_RISCV, LD_ELF_R_RISCV_CALL, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == 0x1234);
    test_elf_write_u32(data, 0xfffff037U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_RISCV, LD_ELF_R_RISCV_HI20, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -4096);
    test_elf_write_u32(data, 0xffc00013U);
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_RISCV, LD_ELF_R_RISCV_LO12_I, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == -4);
    data[0] = 0xacU;
    data[1] = 0x02U;
    assert(test_elf_rel_decode_direct(
                   LD_ELF_EM_RISCV, LD_ELF_R_RISCV_SET_ULEB128, data,
                   sizeof(data), &addend, &capture) == LD_OK);
    assert(addend == 300);
}

static test_elf_rel_fixture_t test_elf_make_rel_fixture(
        uint16_t machine, uint32_t relocation_type,
        uint32_t initial_instruction) {
    static const char symbol_names[] = "\0_start\0target";
    static const char section_names[] =
            "\0.text\0.symtab\0.strtab\0.shstrtab\0.rel.text";
    const uint32_t text_name = 1U;
    const uint32_t symtab_name = 7U;
    const uint32_t strtab_name = 15U;
    const uint32_t shstrtab_name = 23U;
    const uint32_t rel_name = 33U;
    const uint32_t start_name = 1U;
    const uint32_t target_name = 8U;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t text_size = 8U;
    const size_t symtab_offset =
            test_elf_align(text_offset + text_size, 8U);
    const size_t symbol_count = 3U;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t rel_offset =
            test_elf_align(shstrtab_offset + sizeof(section_names), 8U);
    const size_t section_table_offset = rel_offset + LD_ELF64_REL_SIZE;
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
    test_elf_write_u16(bytes + 18U, machine);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 4U);

    test_elf_write_u32(bytes + text_offset, initial_instruction);
    if (machine == LD_ELF_EM_AARCH64) {
        test_elf_write_u32(bytes + text_offset + 4U, 0xd65f03c0U);
    } else if (machine == LD_ELF_EM_RISCV) {
        test_elf_write_u32(bytes + text_offset + 4U, 0x00008067U);
    } else {
        bytes[text_offset + 4U] = 0xc3U;
    }
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, sizeof(section_names));

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *start = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, 1U);
    test_elf_write_u64(start + 8U, 0U);
    test_elf_write_u64(start + 16U, text_size);
    uint8_t *target = start + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(target, target_name);
    target[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(target + 6U, 1U);
    test_elf_write_u64(target + 8U, 4U);
    test_elf_write_u64(target + 16U, 4U);

    test_elf_write_u64(bytes + rel_offset, 0U);
    test_elf_write_u64(
            bytes + rel_offset + 8U,
            LD_ELF_RELA_INFO(2U, relocation_type));

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, symtab_name,
                           LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
                           symbol_count * LD_ELF64_SYM_SIZE, 3U, 1U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, strtab_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE,
                           shstrtab_name, LD_ELF_SHT_STRTAB, 0U,
                           shstrtab_offset, sizeof(section_names), 0U, 0U,
                           1U, 0U);
    test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE, rel_name,
                           LD_ELF_SHT_REL, LD_ELF_SHF_INFO_LINK, rel_offset,
                           LD_ELF64_REL_SIZE, 2U, 1U, 8U,
                           LD_ELF64_REL_SIZE);

    test_elf_rel_fixture_t fixture = {
            .bytes = bytes,
            .size = size,
            .text_offset = text_offset,
            .rel_offset = rel_offset,
            .section_table_offset = section_table_offset,
    };
    return fixture;
}

static void test_elf_rel_parse_fixture(test_elf_rel_fixture_t *fixture,
                                       uint16_t machine,
                                       int64_t expected_addend) {
    char input_path[] = "/tmp/nature-ld-rel-parse-XXXXXX";
    write_fixture(input_path, fixture->bytes, fixture->size);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = test_elf_rel_arch(machine);
    diagnostic_capture_t capture = {0};
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, input_path) == LD_OK);
    assert(capture.count == 0U);
    assert(ctx.objects.count == 1U);
    assert(ctx.objects.items[0]->section_count == 6U);
    const ld_elf_section_t *text = &ctx.objects.items[0]->sections[1];
    assert(text->relocation_count == 1U);
    assert(text->relocations[0].addend == expected_addend);
    assert(text->relocations[0].relocation_section_index == 5U);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(input_path);
}

static void test_elf_rel_link_fixture(test_elf_rel_fixture_t *fixture,
                                      uint16_t machine,
                                      uint32_t expected_entry_word) {
    char input_path[] = "/tmp/nature-ld-rel-link-XXXXXX";
    char output_path[] = "/tmp/nature-ld-rel-output-XXXXXX";
    write_fixture(input_path, fixture->bytes, fixture->size);
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = test_elf_rel_arch(machine);
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    diagnostic_capture_t capture = {0};
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    assert(read_test_elf_entry_word(output_path) == expected_entry_word);
    ld_options_deinit(&options);
    unlink(input_path);
    unlink(output_path);
}

static void test_elf_expect_rel_failure(test_elf_rel_fixture_t *fixture,
                                        uint16_t machine, int expected,
                                        const char *fragment) {
    char input_path[] = "/tmp/nature-ld-rel-invalid-XXXXXX";
    char output_path[] = "/tmp/nature-ld-rel-invalid-output-XXXXXX";
    write_fixture(input_path, fixture->bytes, fixture->size);
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);

    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = test_elf_rel_arch(machine);
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    diagnostic_capture_t capture = {0};
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    assert(ld_link(&options) == expected);
    assert(capture.count > 0U);
    assert(strstr(capture.message, fragment) != NULL);
    assert(access(output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(input_path);
}

static void test_elf_rel_three_architectures(void) {
    test_elf_rel_fixture_t amd64 = test_elf_make_rel_fixture(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_PC32, UINT32_MAX - 3U);
    test_elf_rel_parse_fixture(&amd64, LD_ELF_EM_X86_64, -4);
    test_elf_rel_link_fixture(&amd64, LD_ELF_EM_X86_64, 0U);
    free(amd64.bytes);

    test_elf_rel_fixture_t arm64 = test_elf_make_rel_fixture(
            LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_CALL26, 0x97ffffffU);
    test_elf_rel_parse_fixture(&arm64, LD_ELF_EM_AARCH64, -4);
    test_elf_rel_link_fixture(&arm64, LD_ELF_EM_AARCH64, 0x94000000U);
    free(arm64.bytes);

    test_elf_rel_fixture_t riscv = test_elf_make_rel_fixture(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_JAL,
            test_elf_rel_riscv_jal(-4));
    test_elf_rel_parse_fixture(&riscv, LD_ELF_EM_RISCV, -4);
    test_elf_rel_link_fixture(&riscv, LD_ELF_EM_RISCV, 0x0000006fU);
    free(riscv.bytes);
}

static void test_elf_rel_malformed_inputs(void) {
    test_elf_rel_fixture_t fixture = test_elf_make_rel_fixture(
            LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_CALL26, 0x97ffffffU);
    uint8_t *rel_header = fixture.bytes + fixture.section_table_offset +
                          5U * LD_ELF64_SHDR_SIZE;

    test_elf_write_u64(rel_header + 56U, LD_ELF64_RELA_SIZE);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT, "expected 16-byte entries");
    test_elf_write_u64(rel_header + 56U, LD_ELF64_REL_SIZE);

    test_elf_write_u64(rel_header + 32U, LD_ELF64_REL_SIZE - 1U);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT, "expected 16-byte entries");
    test_elf_write_u64(rel_header + 32U, LD_ELF64_REL_SIZE);

    test_elf_write_u64(fixture.bytes + fixture.rel_offset, 6U);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT,
                                "4-byte relocation field extends");
    test_elf_write_u64(fixture.bytes + fixture.rel_offset, 0U);

    test_elf_write_u64(
            fixture.bytes + fixture.rel_offset + 8U,
            LD_ELF_RELA_INFO(3U, LD_ELF_R_AARCH64_CALL26));
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT,
                                "out-of-range symbol index 3");
    test_elf_write_u64(
            fixture.bytes + fixture.rel_offset + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_AARCH64_CALL26));

    uint8_t *text_header = fixture.bytes + fixture.section_table_offset +
                           LD_ELF64_SHDR_SIZE;
    test_elf_write_u32(text_header + 4U, LD_ELF_SHT_NOBITS);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT, "SHT_NOBITS");
    test_elf_write_u32(text_header + 4U, LD_ELF_SHT_PROGBITS);

    test_elf_write_u32(fixture.bytes + fixture.text_offset, 0xd503201fU);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_AARCH64,
                                LD_INVALID_INPUT, "expected BL instruction");
    free(fixture.bytes);

    fixture = test_elf_make_rel_fixture(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_ALIGN, 0x00000013U);
    test_elf_expect_rel_failure(&fixture, LD_ELF_EM_RISCV,
                                LD_UNSUPPORTED,
                                "does not contain a reversible implicit addend");
    free(fixture.bytes);
}

void test_ld_elf_rel_input(void) {
    test_elf_rel_direct_decoders();
    test_elf_rel_three_architectures();
    test_elf_rel_malformed_inputs();
}
