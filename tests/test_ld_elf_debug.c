#include "src/ld/ld_elf_debug.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static uint64_t test_ld_elf_debug_read(const uint8_t *bytes, size_t width) {
    uint64_t value = 0U;
    for (size_t i = 0; i < width; i++)
        value |= (uint64_t) bytes[i] << (i * 8U);
    return value;
}

static void test_ld_elf_debug_write(uint8_t *bytes, size_t width,
                                    uint64_t value) {
    for (size_t i = 0; i < width; i++) {
        bytes[i] = (uint8_t) value;
        value >>= 8U;
    }
}

static void test_ld_elf_debug_expect_scalar(
        uint16_t machine, uint32_t type, int64_t addend,
        const ld_elf_debug_reloc_values_t *values, size_t width,
        uint64_t initial, uint64_t expected) {
    uint8_t place[16];
    memset(place, 0xa5, sizeof(place));
    test_ld_elf_debug_write(place, width, initial);
    uint8_t before[16];
    memcpy(before, place, sizeof(before));
    size_t written = SIZE_MAX;
    const ld_elf_debug_relocation_t relocation = {type, addend};
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   machine, &relocation, values, place, sizeof(place),
                   &written) == LD_ELF_DEBUG_RELOC_OK);
    assert(written == width);
    assert(test_ld_elf_debug_read(place, width) == expected);
    assert(memcmp(place + width, before + width, sizeof(place) - width) == 0);
}

static void test_ld_elf_debug_section_classification(void) {
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_PROGBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_DWARF);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_PROGBITS, 0U, false) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_line", LD_ELF_SHT_PROGBITS,
                   LD_ELF_SHF_COMPRESSED, true) ==
           LD_ELF_DEBUG_SECTION_DWARF);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".comment", LD_ELF_SHT_PROGBITS,
                   LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS, false) ==
           LD_ELF_DEBUG_SECTION_COMMENT);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".gcc_except_table.tooling", LD_ELF_SHT_PROGBITS, 0U,
                   false) == LD_ELF_DEBUG_SECTION_OTHER);

    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC,
                   true) == LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_PROGBITS, LD_ELF_SHF_EXCLUDE,
                   true) == LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".note.GNU-stack", LD_ELF_SHT_PROGBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".llvm_addrsig", LD_ELF_SHT_PROGBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".riscv.attributes", LD_ELF_SHT_PROGBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_RELA, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   ".debug_info", LD_ELF_SHT_NOBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
    assert(ld_elf_debug_classify_nonalloc_section(
                   NULL, LD_ELF_SHT_PROGBITS, 0U, true) ==
           LD_ELF_DEBUG_SECTION_SKIP);
}

static void test_ld_elf_debug_tombstones(void) {
    assert(ld_elf_debug_tombstone(".debug_loc", true) ==
           LD_ELF_DEBUG_TOMBSTONE_ONE);
    assert(ld_elf_debug_tombstone(".debug_ranges", true) ==
           LD_ELF_DEBUG_TOMBSTONE_ONE);
    assert(ld_elf_debug_tombstone(".debug_info", true) ==
           LD_ELF_DEBUG_TOMBSTONE_ZERO);
    assert(ld_elf_debug_tombstone(".debug_loclists", true) ==
           LD_ELF_DEBUG_TOMBSTONE_ZERO);
    assert(ld_elf_debug_tombstone(".debug_loc", false) ==
           LD_ELF_DEBUG_TOMBSTONE_NONE);
    assert(ld_elf_debug_tombstone(".eh_frame", true) ==
           LD_ELF_DEBUG_TOMBSTONE_NONE);
    assert(ld_elf_debug_tombstone(NULL, true) ==
           LD_ELF_DEBUG_TOMBSTONE_NONE);
}

static void test_ld_elf_debug_x86_64_relocations(void) {
    const ld_elf_debug_reloc_values_t values = {
            .symbol_value = 0x60U,
            .symbol_size = 0x50U,
            .got_address = 0x20U,
            .dynamic_thread_pointer = 0x10U,
    };
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_8, -0x20, &values, 1U,
                                    0U, 0x40U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_16, -0x20, &values, 2U,
                                    0U, 0x40U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_32, -0x20, &values, 4U,
                                    0U, 0x40U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_32S, -0x20, &values, 4U,
                                    0U, 0x40U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_64, -0x20, &values, 8U,
                                    0U, 0x40U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_DTPOFF32, -0x20, &values, 4U,
            0U, 0x30U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_DTPOFF64, -0x20, &values, 8U,
            0U, 0x30U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_GOTOFF64, -0x20, &values, 8U,
            0U, 0x20U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_GOTPC64, -0x20, &values, 8U,
            0U, 0U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_SIZE32, -0x20, &values, 4U,
            0U, 0x30U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_SIZE64, -0x20, &values, 8U,
            0U, 0x30U);

    ld_elf_debug_reloc_values_t negative = values;
    negative.symbol_value = 0U;
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_16, -1, &negative, 2U,
                                    0U, UINT16_MAX);

    ld_elf_debug_reloc_values_t tombstone = values;
    tombstone.has_tombstone = true;
    tombstone.tombstone_value = 1U;
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_X86_64,
                                    LD_ELF_R_X86_64_64, INT64_MAX,
                                    &tombstone, 8U, 0U, 1U);
    /* This unusual eight-byte DTPOFF32 tombstone matches Zig 738d2be9. */
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_DTPOFF32, INT64_MAX,
            &tombstone, 8U, 0U, 1U);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_DTPOFF64, INT64_MAX,
            &tombstone, 8U, 0U, 1U);
}

static void test_ld_elf_debug_aarch64_relocations(void) {
    const ld_elf_debug_reloc_values_t values = {
            .symbol_value = 0x1020U,
    };
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_AARCH64,
                                    LD_ELF_R_AARCH64_ABS32, -0x20, &values,
                                    4U, 0U, 0x1000U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_AARCH64,
                                    LD_ELF_R_AARCH64_ABS64, -0x20, &values,
                                    8U, 0U, 0x1000U);

    ld_elf_debug_reloc_values_t tombstone = values;
    tombstone.has_tombstone = true;
    tombstone.tombstone_value = 1U;
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_AARCH64,
                                    LD_ELF_R_AARCH64_ABS64, INT64_MAX,
                                    &tombstone, 8U, 0U, 1U);
}

static void test_ld_elf_debug_riscv_integer_relocations(void) {
    const ld_elf_debug_reloc_values_t values = {
            .symbol_value = 5U,
    };
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV, LD_ELF_R_RISCV_32, 1,
                                    &values, 4U, 0U, 6U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64, 1,
                                    &values, 8U, 0U, 6U);

    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_ADD8, 1, &values, 1U, 125U,
            INT8_MAX);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_ADD16, 1, &values, 2U,
            INT16_MAX - 2, INT16_MAX);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_ADD32, 1, &values, 4U,
            INT32_MAX - 2, INT32_MAX);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_ADD64, 1, &values, 8U,
            (uint64_t) INT64_MAX - 2U, (uint64_t) INT64_MAX);

    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_SUB8, 1, &values, 1U,
            (uint8_t) (INT8_MIN + 2), (uint8_t) INT8_MIN);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_SUB16, 1, &values, 2U,
            (uint16_t) (INT16_MIN + 2), (uint16_t) INT16_MIN);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_SUB32, 1, &values, 4U,
            (uint32_t) (INT32_MIN + 2), (uint32_t) INT32_MIN);
    test_ld_elf_debug_expect_scalar(
            LD_ELF_EM_RISCV, LD_ELF_R_RISCV_SUB64, 1, &values, 8U,
            (uint64_t) INT64_MIN + 2U, (uint64_t) INT64_MIN);

    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV,
                                    LD_ELF_R_RISCV_SET6, 1, &values, 1U,
                                    0xc0U, 0xc6U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV,
                                    LD_ELF_R_RISCV_SUB6, 1, &values, 1U,
                                    0xc8U, 0xc2U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV,
                                    LD_ELF_R_RISCV_SET8, 1, &values, 1U,
                                    0U, 6U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV,
                                    LD_ELF_R_RISCV_SET16, 1, &values, 2U,
                                    0U, 6U);
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV,
                                    LD_ELF_R_RISCV_SET32, 1, &values, 4U,
                                    0U, 6U);

    ld_elf_debug_reloc_values_t tombstone = values;
    tombstone.has_tombstone = true;
    tombstone.tombstone_value = 1U;
    test_ld_elf_debug_expect_scalar(LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64,
                                    INT64_MAX, &tombstone, 8U, 0U, 1U);
}

static void test_ld_elf_debug_riscv_uleb_pair(void) {
    const ld_elf_debug_relocation_t set = {
            LD_ELF_R_RISCV_SET_ULEB128,
            -0x20,
    };
    const ld_elf_debug_relocation_t sub = {
            LD_ELF_R_RISCV_SUB_ULEB128,
            -0x10,
    };
    const ld_elf_debug_reloc_values_t set_values = {
            .symbol_value = 0x120U,
    };
    const ld_elf_debug_reloc_values_t sub_values = {
            .symbol_value = 0x90U,
    };
    uint8_t field[3] = {0x80U, 0x00U, 0xa5U};
    size_t written = SIZE_MAX;
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &set, &set_values, &sub, &sub_values, field,
                   sizeof(field), &written) == LD_ELF_DEBUG_RELOC_OK);
    /* (0x120 - 0x20) - (0x90 - -0x10) == 0x60. */
    assert(written == 2U && field[0] == 0xe0U && field[1] == 0U &&
           field[2] == 0xa5U);

    const ld_elf_debug_relocation_t wrong = {LD_ELF_R_RISCV_64, 0};
    uint8_t unchanged[2] = {0x80U, 0x00U};
    uint8_t before[2];
    memcpy(before, unchanged, sizeof(before));
    written = 12U;
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &wrong, &set_values, &sub, &sub_values, unchanged,
                   sizeof(unchanged), &written) ==
           LD_ELF_DEBUG_RELOC_INVALID_PAIR);
    assert(written == 0U && memcmp(unchanged, before, sizeof(before)) == 0);

    uint8_t truncated = 0x80U;
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &set, &set_values, &sub, &sub_values, &truncated, 1U,
                   NULL) == LD_ELF_DEBUG_RELOC_TRUNCATED);
    assert(truncated == 0x80U);

    uint8_t malformed[10];
    memset(malformed, 0x80, sizeof(malformed));
    malformed[9] = 0x02U;
    uint8_t malformed_before[10];
    memcpy(malformed_before, malformed, sizeof(malformed));
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &set, &set_values, &sub, &sub_values, malformed,
                   sizeof(malformed), NULL) ==
           LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128);
    assert(memcmp(malformed, malformed_before, sizeof(malformed)) == 0);

    const ld_elf_debug_relocation_t large_set = {
            LD_ELF_R_RISCV_SET_ULEB128,
            0,
    };
    const ld_elf_debug_relocation_t zero_sub = {
            LD_ELF_R_RISCV_SUB_ULEB128,
            0,
    };
    const ld_elf_debug_reloc_values_t large_values = {
            .symbol_value = 128U,
    };
    const ld_elf_debug_reloc_values_t zero_values = {0};
    uint8_t narrow = 0x55U;
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &large_set, &large_values, &zero_sub, &zero_values,
                   &narrow, 1U, NULL) == LD_ELF_DEBUG_RELOC_OK);
    assert(narrow == 0U);

    const ld_elf_debug_reloc_values_t one_value = {.symbol_value = 1U};
    const ld_elf_debug_reloc_values_t two_value = {.symbol_value = 2U};
    narrow = 0x33U;
    assert(ld_elf_debug_apply_riscv_uleb_pair(
                   &large_set, &one_value, &zero_sub, &two_value, &narrow,
                   1U, NULL) == LD_ELF_DEBUG_RELOC_OK);
    assert(narrow == 0x7fU);
}

static void test_ld_elf_debug_relocation_failures(void) {
    ld_elf_debug_reloc_values_t values = {.symbol_value = 128U};
    const ld_elf_debug_relocation_t overflow = {
            LD_ELF_R_X86_64_8,
            0,
    };
    uint8_t place[8];
    memset(place, 0xa5, sizeof(place));
    uint8_t before[8];
    memcpy(before, place, sizeof(before));
    size_t written = SIZE_MAX;
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &overflow, &values, place,
                   sizeof(place), &written) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(written == 0U && memcmp(place, before, sizeof(place)) == 0);

    values.symbol_value = (uint64_t) INT16_MAX + 1U;
    const ld_elf_debug_relocation_t x86_16 = {LD_ELF_R_X86_64_16, 0};
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &x86_16, &values, place,
                   sizeof(place), NULL) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(memcmp(place, before, sizeof(place)) == 0);

    values.symbol_value = (uint64_t) INT32_MAX + 1U;
    const ld_elf_debug_relocation_t aarch64_32 = {
            LD_ELF_R_AARCH64_ABS32,
            0,
    };
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_AARCH64, &aarch64_32, &values, place,
                   sizeof(place), NULL) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(memcmp(place, before, sizeof(place)) == 0);

    const ld_elf_debug_relocation_t riscv_32 = {LD_ELF_R_RISCV_32, 0};
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_RISCV, &riscv_32, &values, place,
                   sizeof(place), NULL) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(memcmp(place, before, sizeof(place)) == 0);

    values.symbol_value = 0U;
    values.got_address = 1U;
    const ld_elf_debug_relocation_t gotoff_overflow = {
            LD_ELF_R_X86_64_GOTOFF64,
            INT64_MIN,
    };
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &gotoff_overflow, &values, place,
                   sizeof(place), NULL) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(memcmp(place, before, sizeof(place)) == 0);

    values.symbol_value = (uint64_t) INT64_MAX + 1U;
    const ld_elf_debug_relocation_t riscv_add = {LD_ELF_R_RISCV_ADD8, 0};
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_RISCV, &riscv_add, &values, place,
                   sizeof(place), NULL) == LD_ELF_DEBUG_RELOC_OVERFLOW);
    assert(memcmp(place, before, sizeof(place)) == 0);

    const ld_elf_debug_relocation_t wide = {LD_ELF_R_X86_64_64, 0};
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &wide, &values, place, 7U,
                   &written) == LD_ELF_DEBUG_RELOC_TRUNCATED);
    assert(written == 0U && memcmp(place, before, sizeof(place)) == 0);

    const ld_elf_debug_relocation_t unsupported = {
            LD_ELF_R_X86_64_PC32,
            0,
    };
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &unsupported, &values, place,
                   sizeof(place), NULL) ==
           LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION);
    assert(memcmp(place, before, sizeof(place)) == 0);

    const ld_elf_debug_relocation_t standalone = {
            LD_ELF_R_RISCV_SET_ULEB128,
            0,
    };
    uint8_t uleb = 0U;
    written = 0U;
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_RISCV, &standalone, &values, &uleb,
                   sizeof(uleb), &written) == LD_ELF_DEBUG_RELOC_OK);
    assert(uleb == 0U && written == 1U);

    assert(ld_elf_debug_apply_nonalloc_relocation(
                   0xffffU, &wide, &values, place, sizeof(place), NULL) ==
           LD_ELF_DEBUG_RELOC_UNSUPPORTED_MACHINE);
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, NULL, &values, place, sizeof(place),
                   NULL) == LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT);
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &wide, NULL, place, sizeof(place),
                   NULL) == LD_ELF_DEBUG_RELOC_INVALID_ARGUMENT);

    const ld_elf_debug_relocation_t none = {LD_ELF_R_X86_64_NONE, 0};
    written = SIZE_MAX;
    assert(ld_elf_debug_apply_nonalloc_relocation(
                   LD_ELF_EM_X86_64, &none, NULL, NULL, 0U, &written) ==
           LD_ELF_DEBUG_RELOC_OK);
    assert(written == 0U);

    for (int result = LD_ELF_DEBUG_RELOC_OK;
         result <= LD_ELF_DEBUG_RELOC_MALFORMED_ULEB128; result++) {
        const char *message = ld_elf_debug_reloc_result_string(
                (ld_elf_debug_reloc_result_t) result);
        assert(message && message[0]);
    }
    assert(strstr(ld_elf_debug_reloc_result_string(
                          (ld_elf_debug_reloc_result_t) 999),
                  "unknown"));
}

void test_ld_elf_debug(void) {
    test_ld_elf_debug_section_classification();
    test_ld_elf_debug_tombstones();
    test_ld_elf_debug_x86_64_relocations();
    test_ld_elf_debug_aarch64_relocations();
    test_ld_elf_debug_riscv_integer_relocations();
    test_ld_elf_debug_riscv_uleb_pair();
    test_ld_elf_debug_relocation_failures();
}

#ifdef LD_ELF_DEBUG_TEST_MAIN
int main(void) {
    test_ld_elf_debug();
    return 0;
}
#endif
