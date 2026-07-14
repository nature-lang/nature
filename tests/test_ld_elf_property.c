#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_property.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t type;
    uint32_t data_size;
    uint64_t value;
} test_elf_property_entry_t;

static size_t make_test_property_note(
        uint8_t *bytes, size_t capacity,
        const test_elf_property_entry_t *entries, size_t entry_count) {
    size_t descriptor_size = 0U;
    for (size_t i = 0; i < entry_count; i++) {
        descriptor_size +=
                test_elf_align(8U + entries[i].data_size, 8U);
    }
    size_t note_size = 16U + descriptor_size;
    assert(note_size <= capacity && descriptor_size <= UINT32_MAX);
    memset(bytes, 0, note_size);
    test_elf_write_u32(bytes, 4U);
    test_elf_write_u32(bytes + 4U, (uint32_t) descriptor_size);
    test_elf_write_u32(bytes + 8U,
                       LD_ELF_NT_GNU_PROPERTY_TYPE_0);
    memcpy(bytes + 12U, "GNU\0", 4U);
    size_t cursor = 16U;
    for (size_t i = 0; i < entry_count; i++) {
        test_elf_write_u32(bytes + cursor, entries[i].type);
        test_elf_write_u32(bytes + cursor + 4U,
                           entries[i].data_size);
        if (entries[i].data_size == 4U) {
            test_elf_write_u32(bytes + cursor + 8U,
                               (uint32_t) entries[i].value);
        } else if (entries[i].data_size == 8U) {
            test_elf_write_u64(bytes + cursor + 8U,
                               entries[i].value);
        }
        cursor = test_elf_align(cursor + 8U + entries[i].data_size, 8U);
    }
    assert(cursor == note_size);
    return note_size;
}

static ld_elf_property_result_t add_test_property_object(
        ld_elf_property_plan_t *plan, ld_arch_t arch,
        const uint8_t *note, size_t note_size) {
    ld_elf_property_input_t input = {
            .data = note,
            .size = note_size,
            .section_index = 3U,
    };
    return ld_elf_property_add_object(plan, arch, &input, 1U);
}

static void expect_property_parse_result(
        ld_arch_t arch, const uint8_t *note, size_t note_size,
        ld_elf_property_result_t expected) {
    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, arch, note, note_size) ==
           expected);
    ld_elf_property_plan_deinit(&plan);
}

static void test_property_merge_unit(void) {
    uint8_t first_note[64], second_note[64];
    test_elf_property_entry_t first = {
            .type = LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND,
            .data_size = 4U,
            .value = LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_BTI |
                     LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_PAC,
    };
    test_elf_property_entry_t second = first;
    second.value = LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_BTI;
    size_t first_size = make_test_property_note(
            first_note, sizeof(first_note), &first, 1U);
    size_t second_size = make_test_property_note(
            second_note, sizeof(second_note), &second, 1U);

    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    second_note, second_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output_size == 32U);
    assert(test_elf_read_u32(plan.output) == 4U);
    assert(test_elf_read_u32(plan.output + 4U) == 16U);
    assert(test_elf_read_u32(plan.output + 8U) ==
           LD_ELF_NT_GNU_PROPERTY_TYPE_0);
    assert(memcmp(plan.output + 12U, "GNU\0", 4U) == 0);
    assert(test_elf_read_u32(plan.output + 16U) ==
           LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND);
    assert(test_elf_read_u32(plan.output + 20U) == 4U);
    assert(test_elf_read_u32(plan.output + 24U) ==
           LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_BTI);
    ld_elf_property_plan_deinit(&plan);

    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_add_object(&plan, LD_ARCH_ARM64, NULL, 0U) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output == NULL && plan.output_size == 0U);
    assert(ld_elf_property_finalize(&plan) ==
           LD_ELF_PROPERTY_INVALID_ARGUMENT);
    assert(ld_elf_property_add_object(&plan, LD_ARCH_ARM64, NULL, 0U) ==
           LD_ELF_PROPERTY_INVALID_ARGUMENT);
    ld_elf_property_plan_deinit(&plan);

    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(&plan, LD_ARCH_ARM64, NULL, 0U) ==
           LD_ELF_PROPERTY_OK);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output == NULL && plan.output_size == 0U);
    ld_elf_property_plan_deinit(&plan);

    test_elf_property_entry_t or_first = {
            .type = LD_ELF_GNU_PROPERTY_1_NEEDED,
            .data_size = 4U,
            .value = 1U,
    };
    test_elf_property_entry_t or_second = or_first;
    or_second.value = 2U;
    first_size = make_test_property_note(
            first_note, sizeof(first_note), &or_first, 1U);
    second_size = make_test_property_note(
            second_note, sizeof(second_note), &or_second, 1U);
    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, LD_ARCH_RISCV64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(add_test_property_object(&plan, LD_ARCH_RISCV64,
                                    second_note, second_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_add_object(&plan, LD_ARCH_RISCV64, NULL, 0U) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output_size == 32U);
    assert(test_elf_read_u32(plan.output + 16U) ==
           LD_ELF_GNU_PROPERTY_1_NEEDED);
    assert(test_elf_read_u32(plan.output + 24U) == 3U);
    ld_elf_property_plan_deinit(&plan);

    test_elf_property_entry_t x86_first[3] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
                    .data_size = 4U,
                    .value = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_IBT |
                             LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED,
                    .data_size = 4U,
                    .value = 1U,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_ISA_1_USED,
                    .data_size = 4U,
                    .value = 1U,
            },
    };
    test_elf_property_entry_t x86_second[3];
    memcpy(x86_second, x86_first, sizeof(x86_second));
    x86_second[0].value = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK;
    x86_second[1].value = 2U;
    x86_second[2].value = 2U;
    first_size = make_test_property_note(
            first_note, sizeof(first_note), x86_first, 3U);
    second_size = make_test_property_note(
            second_note, sizeof(second_note), x86_second, 3U);
    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, LD_ARCH_AMD64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(add_test_property_object(&plan, LD_ARCH_AMD64,
                                    second_note, second_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output_size == 64U);
    assert(test_elf_read_u32(plan.output + 16U) ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND);
    assert(test_elf_read_u32(plan.output + 24U) ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK);
    assert(test_elf_read_u32(plan.output + 32U) ==
           LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED);
    assert(test_elf_read_u32(plan.output + 40U) == 3U);
    assert(test_elf_read_u32(plan.output + 48U) ==
           LD_ELF_GNU_PROPERTY_X86_ISA_1_USED);
    assert(test_elf_read_u32(plan.output + 56U) == 3U);
    ld_elf_property_plan_deinit(&plan);

    test_elf_property_entry_t stack_entries[2] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_STACK_SIZE,
                    .data_size = 8U,
                    .value = 4096U,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_NO_COPY_ON_PROTECTED,
                    .data_size = 0U,
                    .value = 1U,
            },
    };
    first_size = make_test_property_note(
            first_note, sizeof(first_note), stack_entries, 2U);
    stack_entries[0].value = 16384U;
    second_size = make_test_property_note(
            second_note, sizeof(second_note), stack_entries, 2U);
    ld_elf_property_plan_init(&plan);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    first_note, first_size) ==
           LD_ELF_PROPERTY_OK);
    assert(add_test_property_object(&plan, LD_ARCH_ARM64,
                                    second_note, second_size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&plan) == LD_ELF_PROPERTY_OK);
    assert(plan.output_size == 40U);
    assert(test_elf_read_u32(plan.output + 16U) ==
           LD_ELF_GNU_PROPERTY_STACK_SIZE);
    assert(test_elf_read_u64(plan.output + 24U) == 16384U);
    assert(test_elf_read_u32(plan.output + 32U) ==
           LD_ELF_GNU_PROPERTY_NO_COPY_ON_PROTECTED);
    assert(test_elf_read_u32(plan.output + 36U) == 0U);
    ld_elf_property_plan_deinit(&plan);
}

static void test_property_parse_failures(void) {
    uint8_t note[96];
    test_elf_property_entry_t entry = {
            .type = LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND,
            .data_size = 4U,
            .value = 3U,
    };
    size_t size = make_test_property_note(
            note, sizeof(note), &entry, 1U);
    expect_property_parse_result(LD_ARCH_ARM64, note, 11U,
                                 LD_ELF_PROPERTY_TRUNCATED_NOTE);

    uint8_t malformed[96];
    memcpy(malformed, note, size);
    test_elf_write_u32(malformed, 3U);
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_NOTE);
    memcpy(malformed, note, size);
    malformed[12U] = 'X';
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_OWNER);
    memcpy(malformed, note, size);
    test_elf_write_u32(malformed + 8U, 4U);
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_NOTE);
    memcpy(malformed, note, size);
    test_elf_write_u32(malformed + 4U, 15U);
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_PROPERTY);
    memcpy(malformed, note, size);
    test_elf_write_u32(malformed + 20U, 8U);
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_PROPERTY);
    memcpy(malformed, note, size);
    malformed[28U] = 1U;
    expect_property_parse_result(LD_ARCH_ARM64, malformed, size,
                                 LD_ELF_PROPERTY_INVALID_PROPERTY);
    expect_property_parse_result(LD_ARCH_AMD64, note, size,
                                 LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY);

    test_elf_property_entry_t unsorted[2] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED,
                    .data_size = 4U,
                    .value = 1U,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND,
                    .data_size = 4U,
                    .value = 1U,
            },
    };
    size = make_test_property_note(
            malformed, sizeof(malformed), unsorted, 2U);
    ld_elf_property_plan_t unsorted_plan;
    ld_elf_property_plan_init(&unsorted_plan);
    assert(add_test_property_object(&unsorted_plan, LD_ARCH_AMD64,
                                    malformed, size) ==
           LD_ELF_PROPERTY_OK);
    assert(ld_elf_property_finalize(&unsorted_plan) ==
           LD_ELF_PROPERTY_OK);
    assert(unsorted_plan.output_size == 48U);
    assert(test_elf_read_u32(unsorted_plan.output + 16U) ==
           LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND);
    assert(test_elf_read_u32(unsorted_plan.output + 32U) ==
           LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED);
    ld_elf_property_plan_deinit(&unsorted_plan);

    test_elf_property_entry_t duplicate[3] = {
            unsorted[0],
            unsorted[1],
            unsorted[0],
    };
    size = make_test_property_note(
            malformed, sizeof(malformed), duplicate, 3U);
    expect_property_parse_result(LD_ARCH_AMD64, malformed, size,
                                 LD_ELF_PROPERTY_DUPLICATE_PROPERTY);

    test_elf_property_entry_t gcc13_used[2] = {
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_ISA_1_USED,
                    .data_size = 4U,
                    .value = 1U,
            },
            {
                    .type = LD_ELF_GNU_PROPERTY_X86_FEATURE_2_USED,
                    .data_size = 4U,
                    .value = 9U,
            },
    };
    size = make_test_property_note(
            malformed, sizeof(malformed), gcc13_used, 2U);
    expect_property_parse_result(LD_ARCH_AMD64, malformed, size,
                                 LD_ELF_PROPERTY_OK);

    unsorted[0].type = 0xc0001234U;
    size = make_test_property_note(
            malformed, sizeof(malformed), unsorted, 1U);
    expect_property_parse_result(LD_ARCH_AMD64, malformed, size,
                                 LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY);

    ld_elf_property_plan_t plan;
    ld_elf_property_plan_init(NULL);
    ld_elf_property_plan_deinit(NULL);
    ld_elf_property_plan_init(&plan);
    assert(ld_elf_property_add_object(NULL, LD_ARCH_ARM64, NULL, 0U) ==
           LD_ELF_PROPERTY_INVALID_ARGUMENT);
    assert(ld_elf_property_add_object(&plan, LD_ARCH_ARM64, NULL, 1U) ==
           LD_ELF_PROPERTY_INVALID_ARGUMENT);
    assert(ld_elf_property_finalize(NULL) ==
           LD_ELF_PROPERTY_INVALID_ARGUMENT);
    ld_elf_property_plan_deinit(&plan);
    assert(strcmp(ld_elf_property_result_string(
                          LD_ELF_PROPERTY_DUPLICATE_PROPERTY),
                  "duplicate GNU property type") == 0);
}

static uint8_t *make_test_property_object(
        const char *symbol_name, const uint8_t *note, size_t note_size,
        size_t *result_size) {
    const bool has_note = note != NULL;
    static const char section_names[] =
            "\0.text\0.note.gnu.property\0.symtab\0.strtab\0.shstrtab";
    const size_t symbol_name_size = strlen(symbol_name) + 2U;
    char *symbol_names = calloc(1, symbol_name_size);
    assert(symbol_names != NULL);
    memcpy(symbol_names + 1U, symbol_name, symbol_name_size - 1U);

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t note_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t symbol_names_offset =
            has_note ? note_offset + note_size : text_offset + 4U;
    const size_t section_names_offset =
            symbol_names_offset + symbol_name_size;
    const size_t symbols_offset = test_elf_align(
            section_names_offset + sizeof(section_names), 8U);
    const uint16_t section_count = has_note ? 6U : 5U;
    const uint16_t symtab_index = has_note ? 3U : 2U;
    const uint16_t strtab_index = has_note ? 4U : 3U;
    const uint16_t shstrtab_index = has_note ? 5U : 4U;
    const size_t section_table_offset =
            symbols_offset + 2U * LD_ELF64_SYM_SIZE;
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
    test_elf_write_u16(bytes + 62U, shstrtab_index);

    test_elf_write_u32(bytes + text_offset, 0xd65f03c0U);
    if (has_note) memcpy(bytes + note_offset, note, note_size);
    memcpy(bytes + symbol_names_offset, symbol_names, symbol_name_size);
    memcpy(bytes + section_names_offset, section_names,
           sizeof(section_names));
    free(symbol_names);

    uint8_t *symbol = bytes + symbols_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(symbol, 1U);
    symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(symbol + 6U, 1U);
    test_elf_write_u64(symbol + 16U, 4U);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, 1U,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, 4U, 0U, 0U, 4U, 0U);
    if (has_note) {
        test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, 7U,
                               LD_ELF_SHT_NOTE, LD_ELF_SHF_ALLOC,
                               note_offset, note_size, 0U, 0U, 8U, 0U);
    }
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE, 26U,
            LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            2U * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE, 34U,
            LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
            symbol_name_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE, 42U,
            LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            sizeof(section_names), 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static size_t count_test_program_header(const uint8_t *image,
                                        size_t image_size, uint32_t type,
                                        const uint8_t **result) {
    uint64_t offset = test_elf_read_u64(image + 32U);
    uint16_t entry_size = test_elf_read_u16(image + 54U);
    uint16_t count = test_elf_read_u16(image + 56U);
    assert(entry_size == LD_ELF64_PHDR_SIZE && offset <= image_size &&
           (uint64_t) count * entry_size <= image_size - offset);
    size_t matches = 0U;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *program =
                image + (size_t) offset + (size_t) i * entry_size;
        if (test_elf_read_u32(program) != type) continue;
        if (result) *result = program;
        matches++;
    }
    return matches;
}

static void test_property_link_integration(void) {
    uint8_t first_note[64], second_note[64];
    test_elf_property_entry_t first = {
            .type = LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND,
            .data_size = 4U,
            .value = 3U,
    };
    test_elf_property_entry_t second = first;
    second.value = 1U;
    size_t first_note_size = make_test_property_note(
            first_note, sizeof(first_note), &first, 1U);
    size_t second_note_size = make_test_property_note(
            second_note, sizeof(second_note), &second, 1U);
    size_t first_size, second_size;
    uint8_t *first_object = make_test_property_object(
            "_start", first_note, first_note_size, &first_size);
    uint8_t *second_object = make_test_property_object(
            "helper", second_note, second_note_size, &second_size);
    char first_path[] = "/tmp/nature-ld-property-first-XXXXXX";
    char second_path[] = "/tmp/nature-ld-property-second-XXXXXX";
    write_fixture(first_path, first_object, first_size);
    write_fixture(second_path, second_object, second_size);
    free(first_object);
    free(second_object);
    const char *inputs[] = {first_path, second_path};
    const char *output = "/tmp/nature-ld-property-output";
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output, inputs, 2U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output, &image_size, NULL);
    const uint8_t *section = test_elf_find_output_section(
            image, image_size, ".note.gnu.property");
    assert(section != NULL);
    assert(test_elf_read_u32(section + 4U) == LD_ELF_SHT_NOTE);
    assert(test_elf_read_u64(section + 8U) == LD_ELF_SHF_ALLOC);
    uint64_t section_address = test_elf_read_u64(section + 16U);
    uint64_t section_offset = test_elf_read_u64(section + 24U);
    uint64_t section_size = test_elf_read_u64(section + 32U);
    assert(section_size == 32U && section_offset <= image_size &&
           section_size <= image_size - section_offset);
    const uint8_t *note = image + (size_t) section_offset;
    assert(test_elf_read_u32(note + 4U) == 16U);
    assert(test_elf_read_u32(note + 16U) ==
           LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND);
    assert(test_elf_read_u32(note + 24U) == 1U);
    const uint8_t *note_program = NULL;
    const uint8_t *property_program = NULL;
    assert(count_test_program_header(image, image_size, LD_ELF_PT_NOTE,
                                     &note_program) == 1U);
    assert(count_test_program_header(
                   image, image_size, LD_ELF_PT_GNU_PROPERTY,
                   &property_program) == 1U);
    assert(test_elf_read_u64(note_program + 8U) == section_offset);
    assert(test_elf_read_u64(note_program + 16U) == section_address);
    assert(test_elf_read_u64(note_program + 32U) == section_size);
    assert(test_elf_read_u32(note_program + 4U) == LD_ELF_PF_R);
    assert(test_elf_read_u64(note_program + 48U) == 8U);
    assert(test_elf_read_u64(property_program + 8U) == section_offset);
    assert(test_elf_read_u64(property_program + 16U) == section_address);
    assert(test_elf_read_u64(property_program + 32U) == section_size);
    assert(test_elf_read_u32(property_program + 4U) == LD_ELF_PF_R);
    assert(test_elf_read_u64(property_program + 48U) == 8U);
    free(image);
    const char *preserved =
            getenv("NATURE_TEST_ELF_PROPERTY_OUTPUT");
    if (preserved && preserved[0]) {
        unlink(preserved);
        assert(rename(output, preserved) == 0);
    } else {
        unlink(output);
    }

    second_object = make_test_property_object(
            "helper", NULL, 0U, &second_size);
    char missing_path[] = "/tmp/nature-ld-property-missing-XXXXXX";
    write_fixture(missing_path, second_object, second_size);
    free(second_object);
    const char *missing_inputs[] = {first_path, missing_path};
    memset(&capture, 0, sizeof(capture));
    assert(link_test_elf_inputs(output, missing_inputs, 2U, &capture) ==
           LD_OK);
    image = read_test_fixture(output, &image_size, NULL);
    assert(test_elf_find_output_section(
                   image, image_size, ".note.gnu.property") == NULL);
    assert(count_test_program_header(image, image_size, LD_ELF_PT_NOTE,
                                     NULL) == 0U);
    assert(count_test_program_header(
                   image, image_size, LD_ELF_PT_GNU_PROPERTY, NULL) == 0U);
    free(image);
    unlink(output);

    second_object = make_test_property_object(
            "unused", NULL, 0U, &second_size);
    size_t archive_capacity = 8U + LD_ELF_AR_HEADER_SIZE +
                              second_size + (second_size & 1U);
    uint8_t *archive = calloc(1, archive_capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t archive_size = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, archive_capacity, &archive_size,
                        "unused.o/", second_object, second_size);
    assert(archive_size == archive_capacity);
    free(second_object);
    char archive_path[] = "/tmp/nature-ld-property-archive-XXXXXX";
    write_fixture(archive_path, archive, archive_size);
    free(archive);
    const char *archive_inputs[] = {first_path, archive_path};
    memset(&capture, 0, sizeof(capture));
    assert(link_test_elf_inputs(output, archive_inputs, 2U, &capture) ==
           LD_OK);
    image = read_test_fixture(output, &image_size, NULL);
    section = test_elf_find_output_section(
            image, image_size, ".note.gnu.property");
    assert(section != NULL);
    section_offset = test_elf_read_u64(section + 24U);
    assert(test_elf_read_u32(image + (size_t) section_offset + 24U) == 3U);
    free(image);
    unlink(output);

    uint8_t malformed_note[64];
    memcpy(malformed_note, first_note, first_note_size);
    test_elf_write_u32(malformed_note + 20U, 8U);
    first_object = make_test_property_object(
            "_start", malformed_note, first_note_size, &first_size);
    char malformed_path[] = "/tmp/nature-ld-property-malformed-XXXXXX";
    write_fixture(malformed_path, first_object, first_size);
    free(first_object);
    const char *malformed_inputs[] = {malformed_path};
    memset(&capture, 0, sizeof(capture));
    assert(link_test_elf_inputs(output, malformed_inputs, 1U, &capture) ==
           LD_INVALID_INPUT);
    assert(capture.count > 0U);
    assert(strstr(capture.message, ".note.gnu.property") != NULL);
    assert(strstr(capture.message, malformed_path) != NULL);
    assert(access(output, F_OK) != 0);

    first_object = make_test_property_object(
            "_start", first_note, first_note_size, &first_size);
    uint64_t section_table_offset = test_elf_read_u64(first_object + 40U);
    assert(section_table_offset <= first_size &&
           3U * LD_ELF64_SHDR_SIZE <=
                   first_size - section_table_offset);
    test_elf_write_u64(
            first_object + (size_t) section_table_offset +
                    2U * LD_ELF64_SHDR_SIZE + 48U,
            4U);
    char alignment_path[] = "/tmp/nature-ld-property-alignment-XXXXXX";
    write_fixture(alignment_path, first_object, first_size);
    free(first_object);
    const char *alignment_inputs[] = {alignment_path};
    memset(&capture, 0, sizeof(capture));
    assert(link_test_elf_inputs(output, alignment_inputs, 1U, &capture) ==
           LD_INVALID_INPUT);
    assert(strstr(capture.message, "alignment 8") != NULL);
    assert(strstr(capture.message, alignment_path) != NULL);
    assert(access(output, F_OK) != 0);

    unlink(alignment_path);
    unlink(malformed_path);
    unlink(archive_path);
    unlink(missing_path);
    unlink(second_path);
    unlink(first_path);
}

void test_ld_elf_property(void) {
    test_property_merge_unit();
    test_property_parse_failures();
    test_property_link_integration();
}
