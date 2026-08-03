#include "src/ld/ld_elf_dynamic.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t tag;
    uint64_t value;
} test_ld_elf_dynamic_entry_t;

static uint64_t test_ld_elf_dynamic_read_u64(const uint8_t *bytes) {
    uint64_t value = 0U;
    for (size_t i = 0U; i < 8U; i++)
        value |= (uint64_t) bytes[i] << (i * 8U);
    return value;
}

static void test_ld_elf_dynamic_expect_entries(
        const uint8_t *encoded, size_t encoded_size,
        const test_ld_elf_dynamic_entry_t *expected,
        size_t expected_count) {
    assert(encoded != NULL);
    assert(expected != NULL || expected_count == 0U);
    assert(expected_count <= SIZE_MAX / LD_ELF64_DYN_SIZE);
    assert(encoded_size == expected_count * LD_ELF64_DYN_SIZE);
    for (size_t i = 0U; i < expected_count; i++) {
        const uint8_t *entry = encoded + i * LD_ELF64_DYN_SIZE;
        assert(test_ld_elf_dynamic_read_u64(entry) == expected[i].tag);
        assert(test_ld_elf_dynamic_read_u64(entry + 8U) ==
               expected[i].value);
    }
}

static void test_ld_elf_dynamic_architecture_types(void) {
    assert(ld_elf_dynamic_relative_type(LD_ARCH_AMD64) ==
           LD_ELF_R_X86_64_RELATIVE);
    assert(ld_elf_dynamic_irelative_type(LD_ARCH_AMD64) ==
           LD_ELF_R_X86_64_IRELATIVE);

    assert(ld_elf_dynamic_relative_type(LD_ARCH_ARM64) ==
           LD_ELF_R_AARCH64_RELATIVE);
    assert(ld_elf_dynamic_irelative_type(LD_ARCH_ARM64) ==
           LD_ELF_R_AARCH64_IRELATIVE);

    assert(ld_elf_dynamic_relative_type(LD_ARCH_RISCV64) ==
           LD_ELF_R_RISCV_RELATIVE);
    assert(ld_elf_dynamic_irelative_type(LD_ARCH_RISCV64) ==
           LD_ELF_R_RISCV_IRELATIVE);

    assert(ld_elf_dynamic_relative_type((ld_arch_t) 0) == 0U);
    assert(ld_elf_dynamic_irelative_type((ld_arch_t) 0) == 0U);
    assert(ld_elf_dynamic_relative_type((ld_arch_t) UINT16_MAX) == 0U);
    assert(ld_elf_dynamic_irelative_type((ld_arch_t) UINT16_MAX) == 0U);
}

static void test_ld_elf_dynamic_relocation_order(void) {
    ld_elf_dynamic_relocation_t relocations[] = {
            {
                    .offset = 0x90U,
                    .symbol = 0U,
                    .type = LD_ELF_R_RISCV_IRELATIVE,
                    .addend = 9,
                    .kind = LD_ELF_DYNAMIC_RELOC_IRELATIVE,
            },
            {
                    .offset = 0x40U,
                    .symbol = 2U,
                    .type = 5U,
                    .addend = 4,
                    .kind = LD_ELF_DYNAMIC_RELOC_ORDINARY,
            },
            {
                    .offset = 0x30U,
                    .symbol = 0U,
                    .type = LD_ELF_R_X86_64_RELATIVE,
                    .addend = 3,
                    .kind = LD_ELF_DYNAMIC_RELOC_RELATIVE,
            },
            {
                    .offset = 0x50U,
                    .symbol = 1U,
                    .type = 7U,
                    .addend = 5,
                    .kind = LD_ELF_DYNAMIC_RELOC_ORDINARY,
            },
            {
                    .offset = 0x10U,
                    .symbol = 0U,
                    .type = LD_ELF_R_AARCH64_RELATIVE,
                    .addend = 1,
                    .kind = LD_ELF_DYNAMIC_RELOC_RELATIVE,
            },
            {
                    .offset = 0x80U,
                    .symbol = 0U,
                    .type = LD_ELF_R_X86_64_IRELATIVE,
                    .addend = 8,
                    .kind = LD_ELF_DYNAMIC_RELOC_IRELATIVE,
            },
            {
                    .offset = 0x20U,
                    .symbol = 1U,
                    .type = 7U,
                    .addend = 2,
                    .kind = LD_ELF_DYNAMIC_RELOC_ORDINARY,
            },
    };

    ld_elf_dynamic_sort_relocations(
            relocations, sizeof(relocations) / sizeof(relocations[0]));

    static const ld_elf_dynamic_reloc_kind_t expected_kinds[] = {
            LD_ELF_DYNAMIC_RELOC_RELATIVE,
            LD_ELF_DYNAMIC_RELOC_RELATIVE,
            LD_ELF_DYNAMIC_RELOC_ORDINARY,
            LD_ELF_DYNAMIC_RELOC_ORDINARY,
            LD_ELF_DYNAMIC_RELOC_ORDINARY,
            LD_ELF_DYNAMIC_RELOC_IRELATIVE,
            LD_ELF_DYNAMIC_RELOC_IRELATIVE,
    };
    static const uint64_t expected_offsets[] = {
            0x10U,
            0x30U,
            0x20U,
            0x50U,
            0x40U,
            0x80U,
            0x90U,
    };
    for (size_t i = 0U; i < sizeof(relocations) / sizeof(relocations[0]);
         i++) {
        assert(relocations[i].kind == expected_kinds[i]);
        assert(relocations[i].offset == expected_offsets[i]);
    }

    /* Sorting an already sorted sequence is deterministic. */
    ld_elf_dynamic_relocation_t sorted_copy[sizeof(relocations) /
                                            sizeof(relocations[0])];
    memcpy(sorted_copy, relocations, sizeof(relocations));
    ld_elf_dynamic_sort_relocations(
            relocations, sizeof(relocations) / sizeof(relocations[0]));
    assert(memcmp(sorted_copy, relocations, sizeof(relocations)) == 0);

    ld_elf_dynamic_sort_relocations(NULL, 2U);
    ld_elf_dynamic_sort_relocations(relocations, 0U);
    ld_elf_dynamic_sort_relocations(relocations, 1U);
}

static void test_ld_elf_dynamic_relocation_encoding(void) {
    static const ld_elf_dynamic_relocation_t relocations[] = {
            {
                    .offset = UINT64_C(0x1122334455667788),
                    .symbol = UINT32_C(0x99aabbcc),
                    .type = UINT32_C(0xddeeff00),
                    .addend = -2,
                    .kind = LD_ELF_DYNAMIC_RELOC_ORDINARY,
            },
            {
                    .offset = UINT64_C(0x0102030405060708),
                    .symbol = 0U,
                    .type = UINT32_C(0x89abcdef),
                    .addend = INT64_MIN,
                    .kind = LD_ELF_DYNAMIC_RELOC_RELATIVE,
            },
    };
    static const uint8_t expected[] = {
            0x88U,
            0x77U,
            0x66U,
            0x55U,
            0x44U,
            0x33U,
            0x22U,
            0x11U,
            0x00U,
            0xffU,
            0xeeU,
            0xddU,
            0xccU,
            0xbbU,
            0xaaU,
            0x99U,
            0xfeU,
            0xffU,
            0xffU,
            0xffU,
            0xffU,
            0xffU,
            0xffU,
            0xffU,
            0x08U,
            0x07U,
            0x06U,
            0x05U,
            0x04U,
            0x03U,
            0x02U,
            0x01U,
            0xefU,
            0xcdU,
            0xabU,
            0x89U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x80U,
    };
    uint8_t encoded[sizeof(expected)];
    memset(encoded, 0xa5, sizeof(encoded));
    assert(ld_elf_dynamic_encode_relocations(
                   relocations,
                   sizeof(relocations) / sizeof(relocations[0]), encoded,
                   sizeof(encoded)) == LD_ELF_DYNAMIC_OK);
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);

    assert(ld_elf_dynamic_encode_relocations(NULL, 0U, NULL, 0U) ==
           LD_ELF_DYNAMIC_OK);
    assert(ld_elf_dynamic_encode_relocations(NULL, 1U, encoded,
                                             LD_ELF64_RELA_SIZE) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(ld_elf_dynamic_encode_relocations(relocations, 1U, NULL,
                                             LD_ELF64_RELA_SIZE) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);

    uint8_t before[sizeof(encoded)];
    memset(encoded, 0x5a, sizeof(encoded));
    memcpy(before, encoded, sizeof(encoded));
    assert(ld_elf_dynamic_encode_relocations(
                   relocations,
                   sizeof(relocations) / sizeof(relocations[0]), encoded,
                   sizeof(encoded) - 1U) ==
           LD_ELF_DYNAMIC_BUFFER_TOO_SMALL);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
    assert(ld_elf_dynamic_encode_relocations(relocations, 1U, encoded,
                                             LD_ELF64_RELA_SIZE + 1U) ==
           LD_ELF_DYNAMIC_BUFFER_TOO_SMALL);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);

    const size_t overflowing_count = SIZE_MAX / LD_ELF64_RELA_SIZE + 1U;
    assert(ld_elf_dynamic_encode_relocations(
                   relocations, overflowing_count, NULL, 0U) ==
           LD_ELF_DYNAMIC_OVERFLOW);
}

static void test_ld_elf_dynamic_minimal_tables(void) {
    uint8_t dynsym[LD_ELF_DYNAMIC_MIN_DYNSYM_SIZE];
    uint8_t dynstr[LD_ELF_DYNAMIC_MIN_DYNSTR_SIZE];
    uint8_t hash[LD_ELF_DYNAMIC_MIN_HASH_SIZE];
    uint8_t gnu_hash[LD_ELF_DYNAMIC_MIN_GNU_HASH_SIZE];
    memset(dynsym, 0xa5, sizeof(dynsym));
    memset(dynstr, 0xa5, sizeof(dynstr));
    memset(hash, 0xa5, sizeof(hash));
    memset(gnu_hash, 0xa5, sizeof(gnu_hash));

    assert(ld_elf_dynamic_encode_minimal_tables(
                   dynsym, sizeof(dynsym), dynstr, sizeof(dynstr), hash,
                   sizeof(hash), gnu_hash, sizeof(gnu_hash)) ==
           LD_ELF_DYNAMIC_OK);

    static const uint8_t expected_dynsym[LD_ELF_DYNAMIC_MIN_DYNSYM_SIZE] = {
            0U,
    };
    static const uint8_t expected_dynstr[LD_ELF_DYNAMIC_MIN_DYNSTR_SIZE] = {
            0U,
    };
    static const uint8_t expected_hash[LD_ELF_DYNAMIC_MIN_HASH_SIZE] = {
            1U,
            0U,
            0U,
            0U,
            1U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
    };
    static const uint8_t expected_gnu_hash
            [LD_ELF_DYNAMIC_MIN_GNU_HASH_SIZE] = {
                    1U,
                    0U,
                    0U,
                    0U,
                    1U,
                    0U,
                    0U,
                    0U,
                    1U,
                    0U,
                    0U,
                    0U,
                    26U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
                    0U,
            };
    assert(memcmp(dynsym, expected_dynsym, sizeof(dynsym)) == 0);
    assert(memcmp(dynstr, expected_dynstr, sizeof(dynstr)) == 0);
    assert(memcmp(hash, expected_hash, sizeof(hash)) == 0);
    assert(memcmp(gnu_hash, expected_gnu_hash, sizeof(gnu_hash)) == 0);

    assert(ld_elf_dynamic_encode_minimal_tables(
                   NULL, sizeof(dynsym), dynstr, sizeof(dynstr), hash,
                   sizeof(hash), gnu_hash, sizeof(gnu_hash)) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(ld_elf_dynamic_encode_minimal_tables(
                   dynsym, sizeof(dynsym), NULL, sizeof(dynstr), hash,
                   sizeof(hash), gnu_hash, sizeof(gnu_hash)) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(ld_elf_dynamic_encode_minimal_tables(
                   dynsym, sizeof(dynsym), dynstr, sizeof(dynstr), NULL,
                   sizeof(hash), gnu_hash, sizeof(gnu_hash)) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(ld_elf_dynamic_encode_minimal_tables(
                   dynsym, sizeof(dynsym), dynstr, sizeof(dynstr), hash,
                   sizeof(hash), NULL, sizeof(gnu_hash)) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);

    uint8_t dynsym_before[sizeof(dynsym)];
    uint8_t dynstr_before[sizeof(dynstr)];
    uint8_t hash_before[sizeof(hash)];
    uint8_t gnu_hash_before[sizeof(gnu_hash)];
    memset(dynsym, 0x3c, sizeof(dynsym));
    memset(dynstr, 0x3c, sizeof(dynstr));
    memset(hash, 0x3c, sizeof(hash));
    memset(gnu_hash, 0x3c, sizeof(gnu_hash));
    memcpy(dynsym_before, dynsym, sizeof(dynsym));
    memcpy(dynstr_before, dynstr, sizeof(dynstr));
    memcpy(hash_before, hash, sizeof(hash));
    memcpy(gnu_hash_before, gnu_hash, sizeof(gnu_hash));

#define TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE(dynsym_length, dynstr_length,  \
                                             hash_length, gnu_hash_length)  \
    do {                                                                    \
        assert(ld_elf_dynamic_encode_minimal_tables(                        \
                       dynsym, (dynsym_length), dynstr, (dynstr_length),    \
                       hash, (hash_length), gnu_hash, (gnu_hash_length)) == \
               LD_ELF_DYNAMIC_BUFFER_TOO_SMALL);                            \
        assert(memcmp(dynsym, dynsym_before, sizeof(dynsym)) == 0);         \
        assert(memcmp(dynstr, dynstr_before, sizeof(dynstr)) == 0);         \
        assert(memcmp(hash, hash_before, sizeof(hash)) == 0);               \
        assert(memcmp(gnu_hash, gnu_hash_before, sizeof(gnu_hash)) == 0);   \
    } while (0)

    TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE(sizeof(dynsym) - 1U,
                                         sizeof(dynstr), sizeof(hash),
                                         sizeof(gnu_hash));
    TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE(sizeof(dynsym),
                                         sizeof(dynstr) + 1U, sizeof(hash),
                                         sizeof(gnu_hash));
    TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE(sizeof(dynsym), sizeof(dynstr),
                                         sizeof(hash) - 1U,
                                         sizeof(gnu_hash));
    TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE(sizeof(dynsym), sizeof(dynstr),
                                         sizeof(hash),
                                         sizeof(gnu_hash) + 1U);
#undef TEST_LD_ELF_DYNAMIC_WRONG_TABLE_SIZE
}

static ld_elf_dynamic_metadata_t test_ld_elf_dynamic_base_metadata(void) {
    const ld_elf_dynamic_metadata_t metadata = {
            /* Non-zero optional values must be ignored while flags are off. */
            .init_address = UINT64_C(0xaaaaaaaaaaaaaaaa),
            .fini_address = UINT64_C(0xbbbbbbbbbbbbbbbb),
            .init_array_address = UINT64_C(0xcccccccccccccccc),
            .init_array_size = UINT64_C(0xdddddddddddddddd),
            .fini_array_address = UINT64_C(0xeeeeeeeeeeeeeeee),
            .fini_array_size = UINT64_C(0xffffffffffffffff),
            .rela_address = UINT64_C(0x1111111111111111),
            .rela_size = UINT64_C(0x2222222222222222),
            .hash_address = UINT64_C(0x1020304050607080),
            .gnu_hash_address = UINT64_C(0x1121314151617181),
            .dynsym_address = UINT64_C(0x1222324252627282),
            .dynstr_address = UINT64_C(0x1323334353637383),
            .dynstr_size = UINT64_C(0x1424344454647484),
    };
    return metadata;
}

static void test_ld_elf_dynamic_minimal_metadata(void) {
    const ld_elf_dynamic_metadata_t metadata =
            test_ld_elf_dynamic_base_metadata();
    size_t encoded_size = SIZE_MAX;
    assert(ld_elf_dynamic_metadata_size(&metadata, &encoded_size) ==
           LD_ELF_DYNAMIC_OK);
    assert(encoded_size == 9U * LD_ELF64_DYN_SIZE);

    uint8_t encoded[9U * LD_ELF64_DYN_SIZE];
    memset(encoded, 0xa5, sizeof(encoded));
    assert(ld_elf_dynamic_encode_metadata(&metadata, encoded,
                                          sizeof(encoded)) ==
           LD_ELF_DYNAMIC_OK);
    static const test_ld_elf_dynamic_entry_t expected[] = {
            {LD_ELF_DT_HASH, UINT64_C(0x1020304050607080)},
            {LD_ELF_DT_GNU_HASH, UINT64_C(0x1121314151617181)},
            {LD_ELF_DT_SYMTAB, UINT64_C(0x1222324252627282)},
            {LD_ELF_DT_SYMENT, LD_ELF64_SYM_SIZE},
            {LD_ELF_DT_STRTAB, UINT64_C(0x1323334353637383)},
            {LD_ELF_DT_STRSZ, UINT64_C(0x1424344454647484)},
            {LD_ELF_DT_FLAGS_1, LD_ELF_DF_1_PIE},
            {LD_ELF_DT_DEBUG, 0U},
            {LD_ELF_DT_NULL, 0U},
    };
    test_ld_elf_dynamic_expect_entries(
            encoded, sizeof(encoded), expected,
            sizeof(expected) / sizeof(expected[0]));
}

static void test_ld_elf_dynamic_full_metadata(void) {
    ld_elf_dynamic_metadata_t metadata =
            test_ld_elf_dynamic_base_metadata();
    metadata.has_init = true;
    metadata.has_fini = true;
    metadata.has_init_array = true;
    metadata.has_fini_array = true;
    metadata.has_rela = true;
    metadata.has_static_tls = true;
    metadata.init_address = UINT64_C(0x1000);
    metadata.fini_address = UINT64_C(0x2000);
    metadata.init_array_address = UINT64_C(0x3000);
    metadata.init_array_size = UINT64_C(0x18);
    metadata.fini_array_address = UINT64_C(0x4000);
    metadata.fini_array_size = UINT64_C(0x20);
    metadata.rela_address = UINT64_C(0x5000);
    metadata.rela_size = UINT64_C(0x78);

    size_t encoded_size = 0U;
    assert(ld_elf_dynamic_metadata_size(&metadata, &encoded_size) ==
           LD_ELF_DYNAMIC_OK);
    assert(encoded_size == 19U * LD_ELF64_DYN_SIZE);

    uint8_t encoded[19U * LD_ELF64_DYN_SIZE];
    memset(encoded, 0xa5, sizeof(encoded));
    assert(ld_elf_dynamic_encode_metadata(&metadata, encoded,
                                          sizeof(encoded)) ==
           LD_ELF_DYNAMIC_OK);
    static const test_ld_elf_dynamic_entry_t expected[] = {
            {LD_ELF_DT_INIT, UINT64_C(0x1000)},
            {LD_ELF_DT_FINI, UINT64_C(0x2000)},
            {LD_ELF_DT_INIT_ARRAY, UINT64_C(0x3000)},
            {LD_ELF_DT_INIT_ARRAYSZ, UINT64_C(0x18)},
            {LD_ELF_DT_FINI_ARRAY, UINT64_C(0x4000)},
            {LD_ELF_DT_FINI_ARRAYSZ, UINT64_C(0x20)},
            {LD_ELF_DT_RELA, UINT64_C(0x5000)},
            {LD_ELF_DT_RELASZ, UINT64_C(0x78)},
            {LD_ELF_DT_RELAENT, LD_ELF64_RELA_SIZE},
            {LD_ELF_DT_HASH, UINT64_C(0x1020304050607080)},
            {LD_ELF_DT_GNU_HASH, UINT64_C(0x1121314151617181)},
            {LD_ELF_DT_SYMTAB, UINT64_C(0x1222324252627282)},
            {LD_ELF_DT_SYMENT, LD_ELF64_SYM_SIZE},
            {LD_ELF_DT_STRTAB, UINT64_C(0x1323334353637383)},
            {LD_ELF_DT_STRSZ, UINT64_C(0x1424344454647484)},
            {LD_ELF_DT_FLAGS, LD_ELF_DF_STATIC_TLS},
            {LD_ELF_DT_FLAGS_1, LD_ELF_DF_1_PIE},
            {LD_ELF_DT_DEBUG, 0U},
            {LD_ELF_DT_NULL, 0U},
    };
    test_ld_elf_dynamic_expect_entries(
            encoded, sizeof(encoded), expected,
            sizeof(expected) / sizeof(expected[0]));
}

static void test_ld_elf_dynamic_metadata_errors(void) {
    ld_elf_dynamic_metadata_t metadata =
            test_ld_elf_dynamic_base_metadata();
    size_t encoded_size = 0x1234U;
    assert(ld_elf_dynamic_metadata_size(NULL, &encoded_size) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(encoded_size == 0x1234U);
    assert(ld_elf_dynamic_metadata_size(&metadata, NULL) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);

    assert(ld_elf_dynamic_metadata_size(&metadata, &encoded_size) ==
           LD_ELF_DYNAMIC_OK);
    assert(encoded_size == 9U * LD_ELF64_DYN_SIZE);

    uint8_t encoded[10U * LD_ELF64_DYN_SIZE];
    uint8_t before[sizeof(encoded)];
    memset(encoded, 0x6d, sizeof(encoded));
    memcpy(before, encoded, sizeof(encoded));
    assert(ld_elf_dynamic_encode_metadata(NULL, encoded, encoded_size) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
    assert(ld_elf_dynamic_encode_metadata(&metadata, NULL, encoded_size) ==
           LD_ELF_DYNAMIC_INVALID_ARGUMENT);
    assert(ld_elf_dynamic_encode_metadata(&metadata, encoded,
                                          encoded_size - 1U) ==
           LD_ELF_DYNAMIC_BUFFER_TOO_SMALL);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
    assert(ld_elf_dynamic_encode_metadata(&metadata, encoded,
                                          encoded_size + 1U) ==
           LD_ELF_DYNAMIC_BUFFER_TOO_SMALL);
    assert(memcmp(encoded, before, sizeof(encoded)) == 0);
}

static void test_ld_elf_dynamic_result_strings(void) {
    assert(strcmp(ld_elf_dynamic_result_string(LD_ELF_DYNAMIC_OK),
                  "success") == 0);
    assert(strcmp(ld_elf_dynamic_result_string(
                          LD_ELF_DYNAMIC_INVALID_ARGUMENT),
                  "invalid argument") == 0);
    assert(strcmp(ld_elf_dynamic_result_string(LD_ELF_DYNAMIC_OVERFLOW),
                  "integer overflow") == 0);
    assert(strcmp(ld_elf_dynamic_result_string(
                          LD_ELF_DYNAMIC_BUFFER_TOO_SMALL),
                  "output buffer has the wrong size") == 0);
    assert(strcmp(ld_elf_dynamic_result_string(
                          LD_ELF_DYNAMIC_UNSUPPORTED_ARCH),
                  "unsupported architecture") == 0);
    assert(strstr(ld_elf_dynamic_result_string(
                          (ld_elf_dynamic_result_t) 999),
                  "unknown") != NULL);
}

void test_ld_elf_dynamic(void) {
    test_ld_elf_dynamic_architecture_types();
    test_ld_elf_dynamic_relocation_order();
    test_ld_elf_dynamic_relocation_encoding();
    test_ld_elf_dynamic_minimal_tables();
    test_ld_elf_dynamic_minimal_metadata();
    test_ld_elf_dynamic_full_metadata();
    test_ld_elf_dynamic_metadata_errors();
    test_ld_elf_dynamic_result_strings();
}

#ifdef LD_ELF_DYNAMIC_STANDALONE_TEST
int main(void) {
    test_ld_elf_dynamic();
    return 0;
}
#endif
