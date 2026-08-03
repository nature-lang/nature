#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_reloc.h"
#include "src/ld/ld_elf_riscv_relax.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Keep relaxation integration fixtures separate from the architecture-wide
 * relocation formula tests. This file exercises instruction rewriting and
 * the backend allocation consequence (the now-unused GOT must disappear).
 */

static uint8_t *make_amd64_rex_gotpcrelx_object(bool far_nobits_target,
                                                size_t *result_size) {
    static const char symbol_names[] = "\0target\0_start";
    static const uint8_t text_bytes[] = {
            0x48U,
            0x8bU,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0xc3U,
    };
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t data_offset = text_offset + sizeof(text_bytes);
    const size_t data_file_size = far_nobits_target ? 0U : 8U;
    const uint64_t target_offset =
            far_nobits_target ? UINT64_C(0x80000000) : 0U;
    const uint64_t data_size = target_offset + 8U;
    const size_t rela_offset =
            test_elf_align(data_offset + data_file_size, 8U);
    const size_t symbols_offset = rela_offset + LD_ELF64_RELA_SIZE;
    const size_t strings_offset = symbols_offset + 3U * LD_ELF64_SYM_SIZE;

    char section_names[96] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            far_nobits_target ? ".bss" : ".data");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.text");
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
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_X86_64);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 6U);

    memcpy(bytes + text_offset, text_bytes, sizeof(text_bytes));
    if (!far_nobits_target) {
        test_elf_write_u64(bytes + data_offset,
                           UINT64_C(0x1122334455667788));
    }
    memcpy(bytes + strings_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           section_names_size);

    uint8_t *relocation = bytes + rela_offset;
    test_elf_write_u64(relocation, 3U);
    test_elf_write_u64(
            relocation + 8U,
            LD_ELF_RELA_INFO(1U, LD_ELF_R_X86_64_REX_GOTPCRELX));
    test_elf_write_u64(relocation + 16U, UINT64_MAX - 3U);

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *target = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(target, 1U);
    target[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_OBJECT);
    test_elf_write_u16(target + 6U, 2U);
    test_elf_write_u64(target + 8U, target_offset);
    test_elf_write_u64(target + 16U, 8U);
    uint8_t *start = target + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, 8U);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, 1U);
    test_elf_write_u64(start + 16U, sizeof(text_bytes));

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, sizeof(text_bytes), 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, data_name,
                           far_nobits_target ? LD_ELF_SHT_NOBITS
                                             : LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, data_offset,
                           data_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + 3U * LD_ELF64_SHDR_SIZE, rela_name, LD_ELF_SHT_RELA,
            0U, rela_offset, LD_ELF64_RELA_SIZE, 4U, 1U, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + 4U * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            3U * LD_ELF64_SYM_SIZE, 5U, 1U, 8U, LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 5U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, strings_offset,
                           sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + 6U * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static const uint8_t *output_section_contents(const uint8_t *image,
                                              size_t image_size,
                                              const uint8_t *section,
                                              size_t minimum_size) {
    assert(section != NULL);
    uint64_t offset = test_elf_read_u64(section + 24U);
    uint64_t size = test_elf_read_u64(section + 32U);
    assert(size >= minimum_size && offset <= image_size &&
           size <= image_size - offset);
    return image + (size_t) offset;
}

static void test_riscv_align_plan(void) {
    static const uint8_t input[] = {
            0xaaU,
            0xbbU,
            0x01U,
            0x00U,
            0x13U,
            0x00U,
            0x00U,
            0x00U,
            0x11U,
            0x22U,
            0x33U,
            0x44U,
    };
    const ld_elf_riscv_align_input_t alignment = {
            .offset = 2U,
            .addend = 6,
            .source_index = 17U,
    };
    ld_elf_riscv_relax_plan_t plan;
    ld_elf_riscv_relax_plan_init(&plan);
    size_t error_index = 0U;
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, input, sizeof(input), &alignment, 1U, true,
                   &error_index) == LD_ELF_RISCV_RELAX_OK);
    assert(error_index == SIZE_MAX);
    assert(ld_elf_riscv_relax_plan_active(&plan));
    assert(!plan.laid_out && plan.input_size == sizeof(input));

    /* P=0x1004 needs four bytes before the next 8-byte boundary. */
    assert(ld_elf_riscv_relax_plan_layout(
                   &plan, 0x1002U, &error_index) ==
           LD_ELF_RISCV_RELAX_OK);
    assert(error_index == SIZE_MAX);
    assert(plan.laid_out && plan.output_size == 10U);
    assert(plan.regions[0].output_offset == 2U);
    assert(plan.regions[0].kept_size == 4U);

    uint64_t mapped = UINT64_MAX;
    uint64_t available = UINT64_MAX;
    bool alive = false;
    assert(ld_elf_riscv_relax_map(&plan, 2U, 0U, &mapped, &alive,
                                  &available));
    assert(alive && mapped == 2U && available == 8U);
    assert(ld_elf_riscv_relax_map(&plan, 6U, 1U, &mapped, &alive,
                                  &available));
    assert(!alive && mapped == 6U);
    assert(ld_elf_riscv_relax_map(&plan, 8U, 0U, &mapped, &alive,
                                  &available));
    assert(alive && mapped == 6U && available == 4U);
    assert(!ld_elf_riscv_relax_map(&plan, sizeof(input) + 1U, 0U,
                                   NULL, NULL, NULL));

    uint8_t output[10];
    memset(output, 0xa5, sizeof(output));
    assert(ld_elf_riscv_relax_emit(
                   &plan, output, sizeof(output), input, sizeof(input)) ==
           LD_ELF_RISCV_RELAX_OK);
    static const uint8_t expected[] = {
            0xaaU,
            0xbbU,
            0x13U,
            0x00U,
            0x00U,
            0x00U,
            0x11U,
            0x22U,
            0x33U,
            0x44U,
    };
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    assert(ld_elf_riscv_relax_emit(
                   &plan, output, sizeof(output) - 1U, input,
                   sizeof(input)) ==
           LD_ELF_RISCV_RELAX_OUTPUT_SIZE_MISMATCH);
    assert(ld_elf_riscv_relax_plan_layout(
                   &plan, UINT64_MAX, &error_index) ==
           LD_ELF_RISCV_RELAX_ADDRESS_OVERFLOW);
    assert(error_index == 17U);
    ld_elf_riscv_relax_plan_deinit(&plan);

    static const uint8_t compressed_nops[] = {
            0x01U,
            0x00U,
            0x01U,
            0x00U,
    };
    const ld_elf_riscv_align_input_t overlaps[] = {
            {.offset = 0U, .addend = 4, .source_index = 3U},
            {.offset = 2U, .addend = 2, .source_index = 9U},
    };
    ld_elf_riscv_relax_plan_init(&plan);
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, compressed_nops, sizeof(compressed_nops),
                   overlaps, 2U, true, &error_index) ==
           LD_ELF_RISCV_RELAX_OVERLAPPING_PADDING);
    assert(error_index == 9U);

    const ld_elf_riscv_align_input_t negative = {
            .offset = 0U,
            .addend = -1,
            .source_index = 11U,
    };
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, compressed_nops, sizeof(compressed_nops),
                   &negative, 1U, true, &error_index) ==
           LD_ELF_RISCV_RELAX_NEGATIVE_PADDING);
    assert(error_index == 11U);

    const ld_elf_riscv_align_input_t outside = {
            .offset = 3U,
            .addend = 2,
            .source_index = 12U,
    };
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, compressed_nops, sizeof(compressed_nops),
                   &outside, 1U, true, &error_index) ==
           LD_ELF_RISCV_RELAX_PADDING_OUT_OF_RANGE);
    assert(error_index == 12U);

    static const uint8_t bad_padding[] = {0xffU, 0xffU};
    const ld_elf_riscv_align_input_t bad = {
            .offset = 0U,
            .addend = 2,
            .source_index = 13U,
    };
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, bad_padding, sizeof(bad_padding), &bad, 1U, true,
                   &error_index) == LD_ELF_RISCV_RELAX_NON_NOP_PADDING);
    assert(error_index == 13U);

    static const uint8_t full_nop[] = {0x13U, 0x00U, 0x00U, 0x00U};
    const ld_elf_riscv_align_input_t word_alignment = {
            .offset = 0U,
            .addend = 4,
            .source_index = 14U,
    };
    assert(ld_elf_riscv_relax_plan_build(
                   &plan, full_nop, sizeof(full_nop), &word_alignment, 1U,
                   false, &error_index) == LD_ELF_RISCV_RELAX_OK);
    assert(ld_elf_riscv_relax_plan_layout(
                   &plan, 0x1002U, &error_index) ==
           LD_ELF_RISCV_RELAX_ALIGNMENT_IMPOSSIBLE);
    assert(error_index == 14U);
    assert(ld_elf_riscv_relax_plan_layout(
                   &plan, 0x1004U, &error_index) ==
           LD_ELF_RISCV_RELAX_OK);
    assert(plan.output_size == 4U);
    ld_elf_riscv_relax_plan_deinit(&plan);
}

static uint8_t *make_riscv_align_object(bool corrupt_padding,
                                        size_t *result_size) {
    static const char symbol_names[] = "\0_start\0target";
    static const uint8_t text_bytes[] = {
            0x01U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t rela_offset = test_elf_align(
            text_offset + sizeof(text_bytes), 8U);
    const size_t relocation_count = 2U;
    const size_t symbols_offset =
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE;
    const size_t symbol_count = 4U;
    const size_t strings_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;

    char section_names[96] = {0};
    size_t section_names_size = 1U;
    uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.text");
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
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_RISCV);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u32(bytes + 48U, 1U); /* EF_RISCV_RVC */
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, 5U);

    memcpy(bytes + text_offset, text_bytes, sizeof(text_bytes));
    if (corrupt_padding) bytes[text_offset] = 0xffU;
    memcpy(bytes + strings_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + section_names_offset, section_names,
           section_names_size);

    uint8_t *relocation = bytes + rela_offset;
    test_elf_write_u64(relocation, 0U);
    test_elf_write_u64(
            relocation + 8U,
            LD_ELF_RELA_INFO(0U, LD_ELF_R_RISCV_ALIGN));
    test_elf_write_u64(relocation + 16U, 2U);
    relocation += LD_ELF64_RELA_SIZE;
    test_elf_write_u64(relocation, 2U);
    test_elf_write_u64(
            relocation + 8U,
            LD_ELF_RELA_INFO(1U, LD_ELF_R_RISCV_32));
    test_elf_write_u64(relocation + 16U, 6U);

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *section_symbol = symbols + LD_ELF64_SYM_SIZE;
    section_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(section_symbol + 6U, 1U);
    uint8_t *start = section_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, 1U);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, 1U);
    test_elf_write_u64(start + 8U, 2U);
    test_elf_write_u64(start + 16U, 4U);
    uint8_t *target = start + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(target, 8U);
    target[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(target + 6U, 1U);
    test_elf_write_u64(target + 8U, 6U);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            sizeof(text_bytes), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + 2U * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset,
            relocation_count * LD_ELF64_RELA_SIZE, 3U, 1U, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + 3U * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, 4U, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + 4U * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strings_offset, sizeof(symbol_names),
            0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + 5U * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void test_riscv_align_link_relaxes_and_maps_offsets(void) {
    size_t object_size;
    uint8_t *object = make_riscv_align_object(false, &object_size);
    char object_path[] = "/tmp/nature-ld-riscv-align-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-riscv-align-output";
    unlink(output_path);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
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
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    assert(text != NULL);
    assert(test_elf_read_u64(text + 32U) == 4U);
    uint64_t text_address = test_elf_read_u64(text + 16U);
    assert(test_elf_read_u64(image + 24U) == text_address);
    const uint8_t *contents =
            output_section_contents(image, image_size, text, 4U);
    assert(test_elf_read_u32(contents) == (uint32_t) (text_address + 4U));
    free(image);
    unlink(object_path);
    unlink(output_path);

    object = make_riscv_align_object(true, &object_size);
    char bad_path[] = "/tmp/nature-ld-riscv-align-bad-XXXXXX";
    write_fixture(bad_path, object, object_size);
    free(object);
    memset(&capture, 0, sizeof(capture));
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, bad_path) == LD_OK);
    assert(ld_link(&options) == LD_INVALID_INPUT);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "non-NOP") != NULL);
    assert(access(output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(bad_path);
    unlink(output_path);
}

static void test_amd64_gotpcrelx_instruction_forms(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t context;
    ld_elf_context_init(&context, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_X86_64;
    object.display_name = (char *) "gotpcrelx-forms.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_reloc_values_t values = {
            .place_address = 0x400003U,
            .symbol_address = 0x401000U,
            .symbol_name = "target",
    };

    uint8_t rex_mov[] = {0x48U, 0x8bU, 0x05U, 0U, 0U, 0U, 0U};
    ld_elf_relocation_t relocation = {
            .offset = 3U,
            .type = LD_ELF_R_X86_64_REX_GOTPCRELX,
            .addend = -4,
    };
    section.data = rex_mov;
    section.data_size = sizeof(rex_mov);
    section.header.sh_size = sizeof(rex_mov);
    assert(ld_elf_relocation_can_relax_x86_gotpcrelx(&section,
                                                     &relocation));
    assert(ld_elf_relocation_apply_x86_gotpcrelx(
                   &context, &object, &section, &relocation, rex_mov,
                   sizeof(rex_mov), relocation.offset, &values) == LD_OK);
    assert(rex_mov[1] == 0x8dU);
    assert(test_elf_read_u32(rex_mov + 3U) == 0x00000ff9U);

    uint8_t call[] = {0xffU, 0x15U, 0U, 0U, 0U, 0U};
    relocation.offset = 2U;
    relocation.type = LD_ELF_R_X86_64_GOTPCRELX;
    values.place_address = 0x400002U;
    section.data = call;
    section.data_size = sizeof(call);
    section.header.sh_size = sizeof(call);
    assert(ld_elf_relocation_can_relax_x86_gotpcrelx(&section,
                                                     &relocation));
    assert(ld_elf_relocation_apply_x86_gotpcrelx(
                   &context, &object, &section, &relocation, call,
                   sizeof(call), relocation.offset, &values) == LD_OK);
    assert(call[0] == 0x90U && call[1] == 0xe8U);
    assert(test_elf_read_u32(call + 2U) == 0x00000ffaU);

    uint8_t jump[] = {0xffU, 0x25U, 0U, 0U, 0U, 0U};
    section.data = jump;
    section.data_size = sizeof(jump);
    section.header.sh_size = sizeof(jump);
    assert(ld_elf_relocation_can_relax_x86_gotpcrelx(&section,
                                                     &relocation));
    assert(ld_elf_relocation_apply_x86_gotpcrelx(
                   &context, &object, &section, &relocation, jump,
                   sizeof(jump), relocation.offset, &values) == LD_OK);
    assert(jump[0] == 0x90U && jump[1] == 0xe9U);

    uint8_t unsupported[] = {0x48U, 0x03U, 0x05U, 0U, 0U, 0U, 0U};
    relocation.offset = 3U;
    relocation.type = LD_ELF_R_X86_64_REX_GOTPCRELX;
    section.data = unsupported;
    section.data_size = sizeof(unsupported);
    section.header.sh_size = sizeof(unsupported);
    assert(!ld_elf_relocation_can_relax_x86_gotpcrelx(&section,
                                                      &relocation));

    ld_elf_context_deinit(&context);
    ld_options_deinit(&options);
    assert(capture.count == 0U);
}

static void test_amd64_rex_gotpcrelx_link_relaxes_without_got(void) {
    size_t object_size;
    uint8_t *object =
            make_amd64_rex_gotpcrelx_object(false, &object_size);
    char object_path[] = "/tmp/nature-ld-gotpcrelx-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-gotpcrelx-output";
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
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    assert(text != NULL && data != NULL);
    assert(test_elf_find_output_section(image, image_size, ".got") == NULL);
    const uint8_t *text_bytes =
            output_section_contents(image, image_size, text, 8U);
    assert(text_bytes[0] == 0x48U && text_bytes[1] == 0x8dU &&
           text_bytes[2] == 0x05U && text_bytes[7] == 0xc3U);
    uint64_t place = test_elf_read_u64(text + 16U) + 3U;
    uint64_t target = test_elf_read_u64(data + 16U);
    assert(test_elf_add_signed_u32(
                   place, test_elf_read_u32(text_bytes + 3U)) +
                   4U ==
           target);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_amd64_rex_gotpcrelx_far_target_falls_back_to_got(void) {
    size_t object_size;
    uint8_t *object =
            make_amd64_rex_gotpcrelx_object(true, &object_size);
    char object_path[] = "/tmp/nature-ld-gotpcrelx-far-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-gotpcrelx-far-output";
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
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(image_size < 1024U * 1024U);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *got =
            test_elf_find_output_section(image, image_size, ".got");
    const uint8_t *bss =
            test_elf_find_output_section(image, image_size, ".bss");
    assert(text != NULL && got != NULL && bss != NULL);
    assert(test_elf_read_u64(got + 32U) == 8U);
    assert(test_elf_read_u64(bss + 32U) == UINT64_C(0x80000008));

    const uint8_t *text_bytes =
            output_section_contents(image, image_size, text, 8U);
    const uint8_t *got_bytes =
            output_section_contents(image, image_size, got, 8U);
    assert(text_bytes[0] == 0x48U && text_bytes[1] == 0x8bU &&
           text_bytes[2] == 0x05U && text_bytes[7] == 0xc3U);
    uint64_t place = test_elf_read_u64(text + 16U) + 3U;
    uint64_t got_address = test_elf_read_u64(got + 16U);
    uint64_t target_address = test_elf_read_u64(bss + 16U) +
                              UINT64_C(0x80000000);
    assert(test_elf_add_signed_u32(
                   place, test_elf_read_u32(text_bytes + 3U)) +
                   4U ==
           got_address);
    assert(test_elf_read_u64(got_bytes) == target_address);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_relax(void) {
    test_riscv_align_plan();
    test_riscv_align_link_relaxes_and_maps_offsets();
    test_amd64_gotpcrelx_instruction_forms();
    test_amd64_rex_gotpcrelx_link_relaxes_without_got();
    test_amd64_rex_gotpcrelx_far_target_falls_back_to_got();
}
