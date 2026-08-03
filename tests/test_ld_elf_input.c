#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    TEST_ELF_SHNDX_VALID = 0,
    TEST_ELF_SHNDX_MISSING,
    TEST_ELF_SHNDX_BAD_LINK,
    TEST_ELF_SHNDX_BAD_ENTSIZE,
    TEST_ELF_SHNDX_BAD_SIZE,
    TEST_ELF_SHNDX_BAD_INDEX,
} test_elf_shndx_variant_t;

/*
 * Build an ET_REL that uses both extended section-table header fields and
 * SHN_XINDEX for _start. The valid variant places .text at SHN_LORESERVE, so
 * the symbol's real section index cannot fit in Elf64_Sym.st_shndx. Invalid
 * variants use a compact table to keep all negative cases inexpensive.
 */
static uint8_t *make_test_elf_symtab_shndx_object(
        test_elf_shndx_variant_t variant, size_t *result_size) {
    enum {
        compact_text_index = 1,
        symtab_index = 2,
        strtab_index = 3,
        shndx_index = 4,
        compact_shstrtab_index = 5,
        compact_section_count = 6,
        symbol_count = 2,
    };
    const uint32_t text_index =
            variant == TEST_ELF_SHNDX_VALID ? LD_ELF_SHN_LORESERVE
                                            : compact_text_index;
    const uint32_t shstrtab_index =
            variant == TEST_ELF_SHNDX_VALID ? text_index + 1U
                                            : compact_shstrtab_index;
    const uint32_t section_count =
            variant == TEST_ELF_SHNDX_VALID ? shstrtab_index + 1U
                                            : compact_section_count;
    static const char symbol_names[] = "\0_start";
    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shndx_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab_shndx");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t symtab_offset = test_elf_align(text_offset + 4U, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shndx_offset =
            test_elf_align(strtab_offset + sizeof(symbol_names), 4U);
    const size_t shstrtab_offset =
            shndx_offset + symbol_count * sizeof(uint32_t);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
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
    /* Section zero carries the real count and section-name table index. */
    test_elf_write_u16(bytes + 60U, 0U);
    test_elf_write_u16(bytes + 62U, LD_ELF_SHN_XINDEX);

    test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
    uint8_t *start = bytes + symtab_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, 1U);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, LD_ELF_SHN_XINDEX);
    test_elf_write_u64(start + 16U, 4U);
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    test_elf_write_u32(bytes + shndx_offset, 0U);
    test_elf_write_u32(
            bytes + shndx_offset + sizeof(uint32_t),
            variant == TEST_ELF_SHNDX_BAD_INDEX ? section_count : text_index);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections, 0U, LD_ELF_SHT_NULL, 0U, 0U,
                           section_count, shstrtab_index, 0U, 0U, 0U);
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, 4U, 0U,
            0U, 4U, 0U);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, sizeof(symbol_names), 0U,
            0U, 1U, 0U);

    const uint32_t shndx_type = variant == TEST_ELF_SHNDX_MISSING
                                        ? LD_ELF_SHT_PROGBITS
                                        : LD_ELF_SHT_SYMTAB_SHNDX;
    const uint32_t shndx_link = variant == TEST_ELF_SHNDX_BAD_LINK
                                        ? strtab_index
                                        : symtab_index;
    const uint64_t shndx_size = variant == TEST_ELF_SHNDX_BAD_SIZE
                                        ? sizeof(uint32_t)
                                        : symbol_count * sizeof(uint32_t);
    const uint64_t shndx_entsize = variant == TEST_ELF_SHNDX_BAD_ENTSIZE
                                           ? 8U
                                           : sizeof(uint32_t);
    test_elf_write_section(
            sections + shndx_index * LD_ELF64_SHDR_SIZE, shndx_name,
            shndx_type, 0U, shndx_offset, shndx_size, shndx_link, 0U, 4U,
            shndx_entsize);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE, shstrtab_name,
            LD_ELF_SHT_STRTAB, 0U, shstrtab_offset, section_names_size, 0U,
            0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void expect_invalid_symtab_shndx(test_elf_shndx_variant_t variant,
                                        const char *message_fragment) {
    size_t object_size;
    uint8_t *object =
            make_test_elf_symtab_shndx_object(variant, &object_size);
    char object_path[] = "/tmp/nature-ld-elf-shndx-invalid-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, object_path) == LD_INVALID_INPUT);
    assert(capture.count != 0U);
    assert(strstr(capture.message, message_fragment) != NULL);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(object_path);
}

static void test_elf_extended_symbol_section_indexes(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_symtab_shndx_object(
            TEST_ELF_SHNDX_VALID, &object_size);
    char object_path[] = "/tmp/nature-ld-elf-shndx-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    assert(ld_elf_load_input(&ctx, object_path) == LD_OK);
    assert(capture.count == 0U);
    assert(ctx.objects.count == 1U);
    ld_elf_object_t *parsed = ctx.objects.items[0];
    assert(parsed->header.e_shnum == 0U);
    assert(parsed->header.e_shstrndx == LD_ELF_SHN_XINDEX);
    assert(parsed->section_count == (size_t) LD_ELF_SHN_LORESERVE + 2U);
    assert(parsed->symbol_count == 2U);
    assert(parsed->symbols[1].entry.st_shndx == LD_ELF_SHN_XINDEX);
    assert(parsed->symbols[1].section ==
           &parsed->sections[LD_ELF_SHN_LORESERVE]);
    assert(strcmp(parsed->symbols[1].section->name, ".text") == 0);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);

    static const char *output_path = "/tmp/nature-ld-elf-shndx-output";
    unlink(output_path);
    memset(&capture, 0, sizeof(capture));
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
    assert(read_test_elf_entry_word(output_path) == UINT32_C(0xd65f03c0));
    ld_options_deinit(&options);

    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_input(void) {
    test_elf_extended_symbol_section_indexes();
    expect_invalid_symtab_shndx(TEST_ELF_SHNDX_MISSING, "without a linked");
    expect_invalid_symtab_shndx(TEST_ELF_SHNDX_BAD_LINK,
                                "does not link to a symbol table");
    expect_invalid_symtab_shndx(TEST_ELF_SHNDX_BAD_ENTSIZE,
                                "invalid entry size or count");
    expect_invalid_symtab_shndx(TEST_ELF_SHNDX_BAD_SIZE,
                                "invalid entry size or count");
    expect_invalid_symtab_shndx(TEST_ELF_SHNDX_BAD_INDEX,
                                "out-of-range extended section index");
}
