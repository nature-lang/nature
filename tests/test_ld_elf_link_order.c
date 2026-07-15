#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    TEST_ELF_LINK_ORDER_VALID = 0,
    TEST_ELF_LINK_ORDER_BAD_LINK,
    TEST_ELF_LINK_ORDER_SELF_LINK,
    TEST_ELF_LINK_ORDER_CYCLE,
} test_elf_link_order_variant_t;

enum {
    TEST_LINK_TEXT_A = 1,
    TEST_LINK_TEXT_B,
    TEST_LINK_META_B,
    TEST_LINK_META_A_FIRST,
    TEST_LINK_META_A_SECOND,
    TEST_LINK_CHAIN_B,
    TEST_LINK_CHAIN_A_SECOND,
    TEST_LINK_CHAIN_A_FIRST,
    TEST_LINK_DISCARDED_TARGET,
    TEST_LINK_DISCARDED_META,
    TEST_LINK_DISCARDED_CHAIN,
    TEST_LINK_ZERO,
    TEST_LINK_SYMTAB,
    TEST_LINK_STRTAB,
    TEST_LINK_SHSTRTAB,
    TEST_LINK_SECTION_COUNT,
};

/*
 * The direct link-order contributions deliberately occur as B, A1, A2 even
 * though their linked text sections occur as A, B.  The second layer repeats
 * that inversion while linking to the first link-order layer.  A1/A2 share a
 * target and therefore also exercise the stable input-order tie breaker.
 */
static uint8_t *make_test_elf_link_order_object(
        test_elf_link_order_variant_t variant, size_t *result_size) {
    static const char symbol_names[] = "\0_start";
    char section_names[192] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_a_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.a");
    const uint32_t text_b_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.b");
    const uint32_t meta_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".meta");
    const uint32_t chain_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".meta.chain");
    const uint32_t discarded_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".discarded");
    const uint32_t discarded_meta_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".meta.discarded");
    const uint32_t discarded_chain_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".meta.discarded.chain");
    const uint32_t zero_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".meta.zero");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_a_offset = LD_ELF64_EHDR_SIZE;
    const size_t text_b_offset = text_a_offset + sizeof(uint32_t);
    const size_t meta_b_offset = text_b_offset + sizeof(uint32_t);
    const size_t meta_a_first_offset = meta_b_offset + 1U;
    const size_t meta_a_second_offset = meta_a_first_offset + 1U;
    const size_t chain_b_offset = meta_a_second_offset + 1U;
    const size_t chain_a_second_offset = chain_b_offset + 1U;
    const size_t chain_a_first_offset = chain_a_second_offset + 1U;
    const size_t discarded_offset = chain_a_first_offset + 1U;
    const size_t discarded_meta_offset = discarded_offset + 1U;
    const size_t discarded_chain_offset = discarded_meta_offset + 1U;
    const size_t zero_offset = discarded_chain_offset + 1U;
    const size_t symtab_offset = test_elf_align(zero_offset + 1U, 8U);
    const size_t symbol_count = 2U;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        TEST_LINK_SECTION_COUNT * LD_ELF64_SHDR_SIZE;
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
    test_elf_write_u16(bytes + 60U, TEST_LINK_SECTION_COUNT);
    test_elf_write_u16(bytes + 62U, TEST_LINK_SHSTRTAB);

    test_elf_write_u32(bytes + text_a_offset, UINT32_C(0xd65f03c0));
    test_elf_write_u32(bytes + text_b_offset, UINT32_C(0xd503201f));
    bytes[meta_b_offset] = 0xb2U;
    bytes[meta_a_first_offset] = 0xa1U;
    bytes[meta_a_second_offset] = 0xa2U;
    bytes[chain_b_offset] = 0xd2U;
    bytes[chain_a_second_offset] = 0xd3U;
    bytes[chain_a_first_offset] = 0xd1U;
    bytes[discarded_offset] = 0xe8U;
    bytes[discarded_meta_offset] = 0xe9U;
    bytes[discarded_chain_offset] = 0xe7U;
    bytes[zero_offset] = 0xf0U;

    uint8_t *start = bytes + symtab_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, 1U);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, TEST_LINK_TEXT_A);
    test_elf_write_u64(start + 16U, sizeof(uint32_t));
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint32_t meta_b_link = TEST_LINK_TEXT_B;
    uint32_t meta_a_link = TEST_LINK_TEXT_A;
    if (variant == TEST_ELF_LINK_ORDER_BAD_LINK) {
        meta_b_link = TEST_LINK_SECTION_COUNT;
    } else if (variant == TEST_ELF_LINK_ORDER_SELF_LINK) {
        meta_b_link = TEST_LINK_META_B;
    } else if (variant == TEST_ELF_LINK_ORDER_CYCLE) {
        meta_b_link = TEST_LINK_META_A_FIRST;
        meta_a_link = TEST_LINK_META_B;
    }

    const uint64_t text_flags =
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR;
    const uint64_t link_flags =
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_LINK_ORDER;
    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + TEST_LINK_TEXT_A * LD_ELF64_SHDR_SIZE, text_a_name,
            LD_ELF_SHT_PROGBITS, text_flags, text_a_offset,
            sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_TEXT_B * LD_ELF64_SHDR_SIZE, text_b_name,
            LD_ELF_SHT_PROGBITS, text_flags, text_b_offset,
            sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_META_B * LD_ELF64_SHDR_SIZE, meta_name,
            LD_ELF_SHT_PROGBITS, link_flags, meta_b_offset, 1U,
            meta_b_link, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_META_A_FIRST * LD_ELF64_SHDR_SIZE,
            meta_name, LD_ELF_SHT_PROGBITS, link_flags,
            meta_a_first_offset, 1U, meta_a_link, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_META_A_SECOND * LD_ELF64_SHDR_SIZE,
            meta_name, LD_ELF_SHT_PROGBITS, link_flags,
            meta_a_second_offset, 1U, TEST_LINK_TEXT_A, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_CHAIN_B * LD_ELF64_SHDR_SIZE, chain_name,
            LD_ELF_SHT_PROGBITS, link_flags, chain_b_offset, 1U,
            TEST_LINK_META_B, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_CHAIN_A_SECOND * LD_ELF64_SHDR_SIZE,
            chain_name, LD_ELF_SHT_PROGBITS, link_flags,
            chain_a_second_offset, 1U, TEST_LINK_META_A_SECOND, 0U, 1U,
            0U);
    test_elf_write_section(
            sections + TEST_LINK_CHAIN_A_FIRST * LD_ELF64_SHDR_SIZE,
            chain_name, LD_ELF_SHT_PROGBITS, link_flags,
            chain_a_first_offset, 1U, TEST_LINK_META_A_FIRST, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_DISCARDED_TARGET * LD_ELF64_SHDR_SIZE,
            discarded_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXCLUDE, discarded_offset, 1U,
            0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_DISCARDED_META * LD_ELF64_SHDR_SIZE,
            discarded_meta_name, LD_ELF_SHT_PROGBITS, link_flags,
            discarded_meta_offset, 1U, TEST_LINK_DISCARDED_TARGET, 0U, 1U,
            0U);
    test_elf_write_section(
            sections + TEST_LINK_DISCARDED_CHAIN * LD_ELF64_SHDR_SIZE,
            discarded_chain_name, LD_ELF_SHT_PROGBITS, link_flags,
            discarded_chain_offset, 1U, TEST_LINK_DISCARDED_META, 0U, 1U,
            0U);
    test_elf_write_section(
            sections + TEST_LINK_ZERO * LD_ELF64_SHDR_SIZE, zero_name,
            LD_ELF_SHT_PROGBITS, link_flags, zero_offset, 1U, 0U, 0U, 1U,
            0U);
    test_elf_write_section(
            sections + TEST_LINK_SYMTAB * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, TEST_LINK_STRTAB, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + TEST_LINK_STRTAB * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_LINK_SHSTRTAB * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static const uint8_t *test_link_order_section_contents(
        const uint8_t *image, size_t image_size, const uint8_t *section,
        uint64_t expected_size) {
    assert(section != NULL);
    uint64_t offset = test_elf_read_u64(section + 24U);
    uint64_t size = test_elf_read_u64(section + 32U);
    assert(size == expected_size);
    assert(offset <= image_size && size <= image_size - offset);
    return image + (size_t) offset;
}

static uint32_t test_link_order_section_index(const uint8_t *image,
                                              size_t image_size,
                                              const uint8_t *section) {
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t entry_size = test_elf_read_u16(image + 58U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    assert(entry_size == LD_ELF64_SHDR_SIZE);
    assert(table_offset <= image_size);
    assert((uint64_t) section_count * entry_size <=
           image_size - table_offset);
    const uint8_t *table = image + (size_t) table_offset;
    assert(section >= table && section < table + section_count * entry_size);
    size_t distance = (size_t) (section - table);
    assert(distance % entry_size == 0U);
    return (uint32_t) (distance / entry_size);
}

static void test_elf_link_order_layout(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_link_order_object(
            TEST_ELF_LINK_ORDER_VALID, &object_size);
    char object_path[] = "/tmp/nature-ld-link-order-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-link-order-output";
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *meta =
            test_elf_find_output_section(image, image_size, ".meta");
    const uint8_t *chain =
            test_elf_find_output_section(image, image_size, ".meta.chain");
    const uint8_t *zero =
            test_elf_find_output_section(image, image_size, ".meta.zero");
    assert(text != NULL && meta != NULL && chain != NULL && zero != NULL);
    assert(test_elf_find_output_section(image, image_size, ".discarded") ==
           NULL);
    assert(test_elf_find_output_section(image, image_size,
                                        ".meta.discarded") == NULL);
    assert(test_elf_find_output_section(image, image_size,
                                        ".meta.discarded.chain") == NULL);

    const uint8_t *text_bytes = test_link_order_section_contents(
            image, image_size, text, 2U * sizeof(uint32_t));
    const uint8_t *meta_bytes = test_link_order_section_contents(
            image, image_size, meta, 3U);
    const uint8_t *chain_bytes = test_link_order_section_contents(
            image, image_size, chain, 3U);
    const uint8_t *zero_bytes = test_link_order_section_contents(
            image, image_size, zero, 1U);
    assert(test_elf_read_u32(text_bytes) == UINT32_C(0xd65f03c0));
    assert(test_elf_read_u32(text_bytes + sizeof(uint32_t)) ==
           UINT32_C(0xd503201f));
    assert(meta_bytes[0] == 0xa1U && meta_bytes[1] == 0xa2U &&
           meta_bytes[2] == 0xb2U);
    assert(chain_bytes[0] == 0xd1U && chain_bytes[1] == 0xd3U &&
           chain_bytes[2] == 0xd2U);
    assert(zero_bytes[0] == 0xf0U);

    assert((test_elf_read_u64(meta + 8U) & LD_ELF_SHF_LINK_ORDER) != 0U);
    assert((test_elf_read_u64(chain + 8U) & LD_ELF_SHF_LINK_ORDER) != 0U);
    assert((test_elf_read_u64(zero + 8U) & LD_ELF_SHF_LINK_ORDER) != 0U);
    assert(test_elf_read_u32(meta + 40U) ==
           test_link_order_section_index(image, image_size, text));
    assert(test_elf_read_u32(chain + 40U) ==
           test_link_order_section_index(image, image_size, meta));
    assert(test_elf_read_u32(zero + 40U) == 0U);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void expect_invalid_link_order(test_elf_link_order_variant_t variant,
                                      const char *message_fragment) {
    size_t object_size;
    uint8_t *object = make_test_elf_link_order_object(variant, &object_size);
    char object_path[] = "/tmp/nature-ld-link-order-invalid-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-link-order-invalid-output";
    const char *inputs[] = {object_path};
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) ==
           LD_INVALID_INPUT);
    assert(capture.count != 0U);
    assert(strstr(capture.message, message_fragment) != NULL);

    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_link_order(void) {
    test_elf_link_order_layout();
    expect_invalid_link_order(TEST_ELF_LINK_ORDER_BAD_LINK,
                              "out-of-range section link");
    expect_invalid_link_order(TEST_ELF_LINK_ORDER_SELF_LINK,
                              "links to itself");
    expect_invalid_link_order(TEST_ELF_LINK_ORDER_CYCLE,
                              "dependency cycle");
}
