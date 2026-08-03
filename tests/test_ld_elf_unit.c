#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_cie.h"
#include "src/ld/ld_elf_eh_frame.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_merge.h"
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

static void test_elf_cie_deduplication(void) {
    static const uint8_t bytes_a[] = {1U, 2U, 3U, 4U};
    static const uint8_t bytes_a_copy[] = {1U, 2U, 3U, 4U};
    static const uint8_t bytes_b[] = {1U, 2U, 3U, 5U};
    static const int target_a = 1;
    static const int target_b = 2;
    const ld_elf_cie_relocation_t base_relocation = {
            .offset = 8U,
            .type = 2U,
            .addend = -4,
            .target_namespace = &target_a,
            .target_index = 7U,
    };
    ld_elf_cie_relocation_t relocation_offset = base_relocation;
    relocation_offset.offset++;
    ld_elf_cie_relocation_t relocation_type = base_relocation;
    relocation_type.type++;
    ld_elf_cie_relocation_t relocation_addend = base_relocation;
    relocation_addend.addend++;
    ld_elf_cie_relocation_t relocation_namespace = base_relocation;
    relocation_namespace.target_namespace = &target_b;
    ld_elf_cie_relocation_t relocation_index = base_relocation;
    relocation_index.target_index++;
    const ld_elf_cie_entry_t entries[] = {
            {bytes_a, sizeof(bytes_a), &base_relocation, 1U},
            {bytes_a_copy, sizeof(bytes_a_copy), &base_relocation, 1U},
            {bytes_b, sizeof(bytes_b), &base_relocation, 1U},
            {bytes_a, sizeof(bytes_a), &relocation_offset, 1U},
            {bytes_a, sizeof(bytes_a), &relocation_type, 1U},
            {bytes_a, sizeof(bytes_a), &relocation_addend, 1U},
            {bytes_a, sizeof(bytes_a), &relocation_namespace, 1U},
            {bytes_a, sizeof(bytes_a), &relocation_index, 1U},
            {bytes_a, sizeof(bytes_a), NULL, 0U},
            {bytes_a_copy, sizeof(bytes_a_copy), NULL, 0U},
    };
    const size_t expected[] = {0U, 0U, 2U, 3U, 4U,
                               5U, 6U, 7U, 8U, 8U};
    size_t canonical[sizeof(entries) / sizeof(entries[0])];
    memset(canonical, 0xa5, sizeof(canonical));
    size_t error_index = 0U;
    assert(ld_elf_cie_deduplicate(
                   entries, sizeof(entries) / sizeof(entries[0]), canonical,
                   &error_index) == LD_ELF_CIE_OK);
    assert(error_index == SIZE_MAX);
    assert(memcmp(canonical, expected, sizeof(expected)) == 0);

    size_t unchanged[] = {11U, 22U};
    const size_t unchanged_expected[] = {11U, 22U};
    const ld_elf_cie_entry_t invalid_bytes = {
            .bytes = NULL,
            .bytes_size = 1U,
    };
    assert(ld_elf_cie_deduplicate(&invalid_bytes, 1U, unchanged,
                                  &error_index) ==
           LD_ELF_CIE_INVALID_ENTRY);
    assert(error_index == 0U);
    assert(memcmp(unchanged, unchanged_expected, sizeof(unchanged)) == 0);

    const ld_elf_cie_entry_t invalid_relocations = {
            .bytes = bytes_a,
            .bytes_size = sizeof(bytes_a),
            .relocations = NULL,
            .relocation_count = 1U,
    };
    assert(ld_elf_cie_deduplicate(&invalid_relocations, 1U, unchanged,
                                  &error_index) ==
           LD_ELF_CIE_INVALID_ENTRY);
    assert(error_index == 0U);
    assert(memcmp(unchanged, unchanged_expected, sizeof(unchanged)) == 0);

    assert(ld_elf_cie_deduplicate(NULL, 1U, unchanged, &error_index) ==
           LD_ELF_CIE_INVALID_ARGUMENT);
    assert(error_index == SIZE_MAX);
    assert(memcmp(unchanged, unchanged_expected, sizeof(unchanged)) == 0);
    assert(ld_elf_cie_deduplicate(entries, 1U, NULL, &error_index) ==
           LD_ELF_CIE_INVALID_ARGUMENT);
    assert(ld_elf_cie_deduplicate(NULL, 0U, NULL, &error_index) ==
           LD_ELF_CIE_OK);
    assert(error_index == SIZE_MAX);
}

static void test_elf_symtab_encoder(void) {
    const ld_elf_symtab_entry_t locals[] = {
            {
                    .name = "",
                    .binding = LD_ELF_STB_LOCAL,
                    .type = LD_ELF_STT_SECTION,
                    .other = LD_ELF_STV_DEFAULT,
                    .section_index = 1U,
                    .value = UINT64_C(0x401000),
            },
            {
                    .name = "local",
                    .binding = LD_ELF_STB_LOCAL,
                    .type = LD_ELF_STT_OBJECT,
                    .other = LD_ELF_STV_HIDDEN,
                    .section_index = 2U,
                    .value = UINT64_C(0x402000),
                    .size = 4U,
            },
    };
    const ld_elf_symtab_entry_t globals[] = {
            {
                    .name = "global",
                    .binding = LD_ELF_STB_GLOBAL,
                    .type = LD_ELF_STT_FUNC,
                    .other = LD_ELF_STV_DEFAULT,
                    .section_index = 1U,
                    .value = UINT64_C(0x401004),
                    .size = 8U,
            },
            {
                    .name = "weak",
                    .binding = LD_ELF_STB_WEAK,
                    .type = LD_ELF_STT_NOTYPE,
                    .other = LD_ELF_STV_PROTECTED,
                    .section_index = LD_ELF_SHN_UNDEF,
            },
    };

    ld_elf_symtab_t table;
    ld_elf_symtab_init(&table);
    size_t error_index = 123U;
    assert(ld_elf_symtab_build(
                   locals, sizeof(locals) / sizeof(locals[0]), globals,
                   sizeof(globals) / sizeof(globals[0]), &table,
                   &error_index) == LD_ELF_SYMTAB_OK);
    assert(error_index == SIZE_MAX);
    assert(table.symbol_count == 5U);
    assert(table.symbols_size == 5U * LD_ELF64_SYM_SIZE);
    assert(table.first_global == 3U);
    static const char expected_strings[] = "\0local\0global\0weak";
    assert(table.strings_size == sizeof(expected_strings));
    assert(memcmp(table.strings, expected_strings,
                  sizeof(expected_strings)) == 0);

    for (size_t i = 0; i < LD_ELF64_SYM_SIZE; i++)
        assert(table.symbols[i] == 0U);
    const uint8_t *section = table.symbols + LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(section) == 0U);
    assert(section[4] == LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL,
                                         LD_ELF_STT_SECTION));
    assert(test_elf_read_u16(section + 6U) == 1U);
    assert(test_elf_read_u64(section + 8U) == UINT64_C(0x401000));

    const uint8_t *local = section + LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(local) == 1U);
    assert(local[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_OBJECT));
    assert(local[5] == LD_ELF_STV_HIDDEN);
    assert(test_elf_read_u16(local + 6U) == 2U);
    assert(test_elf_read_u64(local + 8U) == UINT64_C(0x402000));
    assert(test_elf_read_u64(local + 16U) == 4U);

    const uint8_t *global = local + LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(global) == 7U);
    assert(global[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC));
    assert(test_elf_read_u16(global + 6U) == 1U);
    assert(test_elf_read_u64(global + 8U) == UINT64_C(0x401004));
    assert(test_elf_read_u64(global + 16U) == 8U);

    const uint8_t *weak = global + LD_ELF64_SYM_SIZE;
    assert(test_elf_read_u32(weak) == 14U);
    assert(weak[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_WEAK, LD_ELF_STT_NOTYPE));
    assert(weak[5] == LD_ELF_STV_PROTECTED);
    assert(test_elf_read_u16(weak + 6U) == LD_ELF_SHN_UNDEF);

    ld_elf_symtab_deinit(&table);
    ld_elf_symtab_deinit(&table);
    assert(table.symbols == NULL && table.symbols_size == 0U);
    assert(table.strings == NULL && table.strings_size == 0U);
    assert(table.symbol_count == 0U && table.first_global == 0U);

    ld_elf_symtab_entry_t invalid = locals[0];
    invalid.name = NULL;
    error_index = SIZE_MAX;
    assert(ld_elf_symtab_build(&invalid, 1U, NULL, 0U, &table,
                               &error_index) ==
           LD_ELF_SYMTAB_INVALID_ARGUMENT);
    assert(error_index == 0U && table.symbols == NULL &&
           table.strings == NULL);

    ld_elf_symtab_init(&table);
    assert(ld_elf_symtab_build(NULL, 1U, NULL, 0U, &table,
                               &error_index) ==
           LD_ELF_SYMTAB_INVALID_ARGUMENT);
    assert(table.symbols == NULL && table.strings == NULL);
    assert(ld_elf_symtab_build(NULL, 0U, NULL, 0U, NULL,
                               &error_index) ==
           LD_ELF_SYMTAB_INVALID_ARGUMENT);

    invalid = globals[0];
    invalid.binding = 16U;
    error_index = SIZE_MAX;
    assert(ld_elf_symtab_build(NULL, 0U, &invalid, 1U, &table,
                               &error_index) == LD_ELF_SYMTAB_INVALID_INFO);
    assert(error_index == 0U && table.symbols == NULL &&
           table.strings == NULL);

#if SIZE_MAX > UINT32_MAX
    invalid = locals[0];
    assert(ld_elf_symtab_build(
                   &invalid, (size_t) UINT32_MAX + 1U, NULL, 0U, &table,
                   &error_index) == LD_ELF_SYMTAB_SYMBOL_COUNT_OVERFLOW);
    assert(table.symbols == NULL && table.strings == NULL);
#endif
    invalid = globals[0];
    assert(ld_elf_symtab_build(
                   NULL, 0U, &invalid, SIZE_MAX / LD_ELF64_SYM_SIZE + 1U,
                   &table, &error_index) ==
           LD_ELF_SYMTAB_SYMBOL_SIZE_OVERFLOW);
    assert(table.symbols == NULL && table.strings == NULL);

    ld_elf_symtab_init(&table);
    assert(ld_elf_symtab_build(NULL, 0U, NULL, 0U, &table,
                               &error_index) == LD_ELF_SYMTAB_OK);
    assert(table.symbol_count == 1U && table.first_global == 1U);
    assert(table.symbols_size == LD_ELF64_SYM_SIZE);
    assert(table.strings_size == 1U && table.strings[0] == '\0');
    ld_elf_symtab_deinit(&table);
}

static void test_elf_eh_frame_hdr_encoder(void) {
    size_t encoded_size = 999U;
    assert(ld_elf_eh_frame_hdr_size(3U, &encoded_size) ==
           LD_ELF_EH_FRAME_HDR_OK);
    assert(encoded_size == 36U);
    assert(ld_elf_eh_frame_hdr_size(0U, NULL) ==
           LD_ELF_EH_FRAME_HDR_INVALID_ARGUMENT);
#if SIZE_MAX > UINT32_MAX
    encoded_size = 999U;
    assert(ld_elf_eh_frame_hdr_size((size_t) UINT32_MAX + 1U,
                                    &encoded_size) ==
           LD_ELF_EH_FRAME_HDR_ENTRY_COUNT_OVERFLOW);
    assert(encoded_size == 999U);
#endif

    const uint64_t header_address = 0x100000U;
    const uint64_t eh_frame_address = 0x100080U;
    const ld_elf_eh_frame_hdr_entry_t entries[] = {
            {.first_pc = 0x100300U, .fde_address = 0x100900U},
            {.first_pc = 0x0fff00U, .fde_address = 0x100800U},
            {.first_pc = 0x100300U, .fde_address = 0x100700U},
    };
    uint8_t output[36];
    memset(output, 0xa5, sizeof(output));
    size_t error_index = 0U;
    assert(ld_elf_eh_frame_hdr_encode(
                   output, sizeof(output), header_address, eh_frame_address,
                   entries, sizeof(entries) / sizeof(entries[0]),
                   &error_index) == LD_ELF_EH_FRAME_HDR_OK);
    assert(error_index == SIZE_MAX);
    assert(output[0] == 1U);
    assert(output[1] == 0x1bU);
    assert(output[2] == 0x03U);
    assert(output[3] == 0x3bU);
    assert(test_elf_read_u32(output + 4U) == 0x7cU);
    assert(test_elf_read_u32(output + 8U) == 3U);
    /* Sorted by first PC, then by FDE address. */
    assert(test_elf_read_u32(output + 12U) == 0xffffff00U);
    assert(test_elf_read_u32(output + 16U) == 0x800U);
    assert(test_elf_read_u32(output + 20U) == 0x300U);
    assert(test_elf_read_u32(output + 24U) == 0x700U);
    assert(test_elf_read_u32(output + 28U) == 0x300U);
    assert(test_elf_read_u32(output + 32U) == 0x900U);

    uint8_t empty_header[12];
    memset(empty_header, 0xa5, sizeof(empty_header));
    assert(ld_elf_eh_frame_hdr_encode(
                   empty_header, sizeof(empty_header), header_address,
                   eh_frame_address, NULL, 0U, &error_index) ==
           LD_ELF_EH_FRAME_HDR_OK);
    assert(error_index == SIZE_MAX);
    assert(empty_header[0] == 1U && empty_header[1] == 0x1bU &&
           empty_header[2] == 0x03U && empty_header[3] == 0x3bU);
    assert(test_elf_read_u32(empty_header + 4U) == 0x7cU);
    assert(test_elf_read_u32(empty_header + 8U) == 0U);

    uint8_t unchanged[36];
    memset(unchanged, 0x5a, sizeof(unchanged));
    uint8_t expected[36];
    memcpy(expected, unchanged, sizeof(expected));
    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged) - 1U, header_address,
                   eh_frame_address, entries,
                   sizeof(entries) / sizeof(entries[0]), &error_index) ==
           LD_ELF_EH_FRAME_HDR_OUTPUT_TOO_SMALL);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);

    const ld_elf_eh_frame_hdr_entry_t first_pc_overflow = {
            .first_pc = header_address + (uint64_t) INT32_MAX + 1U,
            .fde_address = header_address,
    };
    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged), header_address,
                   eh_frame_address, &first_pc_overflow, 1U, &error_index) ==
           LD_ELF_EH_FRAME_HDR_FIRST_PC_RANGE);
    assert(error_index == 0U);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);

    const ld_elf_eh_frame_hdr_entry_t fde_overflow = {
            .first_pc = header_address,
            .fde_address = header_address + (uint64_t) INT32_MAX + 1U,
    };
    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged), header_address,
                   eh_frame_address, &fde_overflow, 1U, &error_index) ==
           LD_ELF_EH_FRAME_HDR_FDE_ADDRESS_RANGE);
    assert(error_index == 0U);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);

    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged), header_address,
                   header_address + 4U + (uint64_t) INT32_MAX + 1U,
                   entries, 1U, &error_index) ==
           LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE);
    assert(error_index == SIZE_MAX);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);

    const uint64_t pointer_boundary_field = UINT64_C(0x90000000);
    assert(ld_elf_eh_frame_hdr_encode(
                   empty_header, sizeof(empty_header),
                   pointer_boundary_field - 4U, UINT64_C(0x10000000), NULL,
                   0U, &error_index) == LD_ELF_EH_FRAME_HDR_OK);
    assert(test_elf_read_u32(empty_header + 4U) == 0x80000000U);
    assert(ld_elf_eh_frame_hdr_encode(
                   empty_header, sizeof(empty_header),
                   pointer_boundary_field - 4U,
                   pointer_boundary_field + (uint64_t) INT32_MAX, NULL, 0U,
                   &error_index) == LD_ELF_EH_FRAME_HDR_OK);
    assert(test_elf_read_u32(empty_header + 4U) == 0x7fffffffU);

    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged), UINT64_MAX - 3U,
                   eh_frame_address, entries, 1U, &error_index) ==
           LD_ELF_EH_FRAME_HDR_EH_FRAME_POINTER_RANGE);
    assert(error_index == SIZE_MAX);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);

    const uint64_t boundary_header = UINT64_C(0x90000000);
    const ld_elf_eh_frame_hdr_entry_t boundary_entries[] = {
            {.first_pc = UINT64_C(0x10000000),
             .fde_address = boundary_header + (uint64_t) INT32_MAX},
            {.first_pc = boundary_header + (uint64_t) INT32_MAX,
             .fde_address = UINT64_C(0x10000000)},
    };
    assert(ld_elf_eh_frame_hdr_encode(
                   output, sizeof(output), boundary_header,
                   boundary_header + 4U, boundary_entries, 2U,
                   &error_index) == LD_ELF_EH_FRAME_HDR_OK);
    assert(test_elf_read_u32(output + 12U) == 0x80000000U);
    assert(test_elf_read_u32(output + 16U) == 0x7fffffffU);
    assert(test_elf_read_u32(output + 20U) == 0x7fffffffU);
    assert(test_elf_read_u32(output + 24U) == 0x80000000U);

    const ld_elf_eh_frame_hdr_entry_t sorted_range_error[] = {
            {.first_pc = boundary_header,
             .fde_address = boundary_header},
            {.first_pc = UINT64_C(0x0fffffff),
             .fde_address = boundary_header},
    };
    assert(ld_elf_eh_frame_hdr_encode(
                   unchanged, sizeof(unchanged), boundary_header,
                   boundary_header + 4U, sorted_range_error, 2U,
                   &error_index) == LD_ELF_EH_FRAME_HDR_FIRST_PC_RANGE);
    assert(error_index == 1U);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);
}


static void test_elf_fixed_merge_entities(void) {
    static const uint8_t first_data[] = {
            0x10,
            0x11,
            0x12,
            0x13,
            0x14,
            0x15,
            0x16,
            0x17,
            0x20,
            0x21,
            0x22,
            0x23,
            0x24,
            0x25,
            0x26,
            0x27,
    };
    static const uint8_t second_data[] = {
            0x10,
            0x11,
            0x12,
            0x13,
            0x14,
            0x15,
            0x16,
            0x17,
            0x30,
            0x31,
            0x32,
            0x33,
            0x34,
            0x35,
            0x36,
            0x37,
    };
    ld_elf_section_t first = {
            .header =
                    {
                            .sh_type = LD_ELF_SHT_PROGBITS,
                            .sh_flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE,
                            .sh_size = sizeof(first_data),
                            .sh_addralign = 8U,
                            .sh_entsize = 8U,
                    },
            .name = ".rodata.cst8",
            .data = first_data,
            .data_size = sizeof(first_data),
    };
    ld_elf_section_t second = {
            .header =
                    {
                            .sh_type = LD_ELF_SHT_PROGBITS,
                            .sh_flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE,
                            .sh_size = sizeof(second_data),
                            .sh_addralign = 8U,
                            .sh_entsize = 8U,
                    },
            .name = ".rodata.cst8",
            .data = second_data,
            .data_size = sizeof(second_data),
    };
    ld_elf_output_section_t output = {
            .name = ".rodata.cst8",
            .type = LD_ELF_SHT_PROGBITS,
            .flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE,
            .align = 8U,
    };
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t context;
    ld_elf_context_init(&context, &options);
    ld_elf_merge_plan_t plan;
    ld_elf_merge_plan_init(&plan, &context);

    assert(ld_elf_merge_section_eligible(&first));
    assert(ld_elf_merge_add_section(&plan, &first, &output) == LD_OK);
    assert(ld_elf_merge_add_section(&plan, &second, &output) == LD_OK);
    assert(ld_elf_merge_finalize(&plan) == LD_OK);
    assert(output.size == 3U * 8U);
    assert(output.file_size == output.size);
    assert(output.entry_size == 8U);
    assert(output.align == 8U);
    output.data = calloc(1, (size_t) output.file_size);
    assert(output.data != NULL);
    assert(ld_elf_merge_emit(&plan) == LD_OK);

    uint64_t first_shared, first_unique, second_shared, second_unique;
    bool alive;
    assert(ld_elf_merge_map_input(&first, 0U, 8U, &first_shared, &alive,
                                  NULL) &&
           alive);
    assert(ld_elf_merge_map_input(&first, 8U, 8U, &first_unique, &alive,
                                  NULL) &&
           alive);
    assert(ld_elf_merge_map_input(&second, 0U, 8U, &second_shared, &alive,
                                  NULL) &&
           alive);
    assert(ld_elf_merge_map_input(&second, 8U, 8U, &second_unique, &alive,
                                  NULL) &&
           alive);
    assert(first_shared == second_shared);
    assert(first_unique != second_unique && first_unique != first_shared &&
           second_unique != first_shared);
    assert(memcmp(output.data + first_shared, first_data, 8U) == 0);
    assert(memcmp(output.data + first_unique, first_data + 8U, 8U) == 0);
    assert(memcmp(output.data + second_unique, second_data + 8U, 8U) == 0);
    assert(!ld_elf_merge_map_input(&first, 4U, 8U, NULL, NULL, NULL));

    free(output.data);
    ld_elf_merge_plan_deinit(&plan);
    ld_elf_context_deinit(&context);
    ld_options_deinit(&options);
    assert(capture.count == 0U);
}

static void test_elf_malformed_merge_entities(void) {
    static const uint8_t unterminated[] = {'b', 'a', 'd'};
    ld_elf_section_t section = {
            .header =
                    {
                            .sh_type = LD_ELF_SHT_PROGBITS,
                            .sh_flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE |
                                        LD_ELF_SHF_STRINGS,
                            .sh_size = sizeof(unterminated),
                            .sh_addralign = 1U,
                            .sh_entsize = 1U,
                    },
            .name = ".rodata.str1.1",
            .data = unterminated,
            .data_size = sizeof(unterminated),
    };
    ld_elf_output_section_t output = {
            .name = ".rodata.str1.1",
            .type = LD_ELF_SHT_PROGBITS,
            .flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE |
                     LD_ELF_SHF_STRINGS,
            .align = 1U,
    };
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t context;
    ld_elf_context_init(&context, &options);
    ld_elf_merge_plan_t plan;
    ld_elf_merge_plan_init(&plan, &context);
    assert(ld_elf_merge_add_section(&plan, &section, &output) ==
           LD_INVALID_INPUT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "not NUL terminated") != NULL);
    ld_elf_merge_plan_deinit(&plan);

    memset(&capture, 0, sizeof(capture));
    context.error = LD_OK;
    section.header.sh_flags = LD_ELF_SHF_ALLOC | LD_ELF_SHF_MERGE;
    section.header.sh_entsize = 2U;
    ld_elf_merge_plan_init(&plan, &context);
    assert(ld_elf_merge_add_section(&plan, &section, &output) ==
           LD_INVALID_INPUT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "not a multiple") != NULL);
    ld_elf_merge_plan_deinit(&plan);
    ld_elf_context_deinit(&context);
    ld_options_deinit(&options);
}

static void test_elf_rejects_invalid_debug_mode(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.debug_mode = (ld_debug_mode_t) 99;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_INVALID_ARGUMENT);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "invalid linker debug mode value 99") !=
           NULL);
    ld_options_deinit(&options);
}

void test_ld_elf_unit(void) {
    test_elf_cie_deduplication();
    test_elf_symtab_encoder();
    test_elf_eh_frame_hdr_encoder();
    test_elf_fixed_merge_entities();
    test_elf_malformed_merge_entities();
    test_elf_rejects_invalid_debug_mode();
}
