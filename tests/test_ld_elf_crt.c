#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * GCC's crtbeginT.o contains an allocated, writable, zero-sized
 * .tm_clone_table.  Its startup code references the section symbol and local
 * anchor, while other variants may expose a hidden global anchor.  Zig
 * commit 738d2be9 represents every such input section as a live zero-sized
 * atom and assigns it an output placement; this fixture keeps that behavior
 * covered without depending on a cross compiler being installed on the host.
 */
static uint8_t *make_test_elf_empty_alloc_object(size_t *result_size) {
    enum {
        text_index = 1,
        data_index = 2,
        empty_index = 3,
        rela_index = 4,
        symtab_index = 5,
        strtab_index = 6,
        shstrtab_index = 7,
        section_count = 8,
    };

    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t local_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "local_empty");
    const uint32_t global_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "global_empty");
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".data");
    const uint32_t empty_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".tm_clone_table");
    const uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.text");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_size = 3U * sizeof(uint64_t);
    const size_t data_size = sizeof(uint64_t);
    const size_t relocation_count = 3U;
    const size_t symbol_count = 5U;
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t data_offset = text_offset + text_size;
    const size_t empty_offset = data_offset + data_size;
    const size_t rela_offset = test_elf_align(empty_offset, 8U);
    const size_t symtab_offset =
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
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
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, shstrtab_index);

    test_elf_write_u64(bytes + data_offset, 0x1122334455667788ULL);
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *relocations = bytes + rela_offset;
    for (uint32_t i = 0U; i < relocation_count; i++) {
        uint8_t *relocation =
                relocations + (size_t) i * LD_ELF64_RELA_SIZE;
        test_elf_write_u64(relocation, (uint64_t) i * sizeof(uint64_t));
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(i + 1U, LD_ELF_R_AARCH64_ABS64));
    }

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *section_symbol = symbols + LD_ELF64_SYM_SIZE;
    section_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(section_symbol + 6U, empty_index);

    uint8_t *local_symbol = section_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(local_symbol, local_name);
    local_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(local_symbol + 6U, empty_index);

    uint8_t *global_symbol = local_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(global_symbol, global_name);
    global_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_OBJECT);
    global_symbol[5] = LD_ELF_STV_HIDDEN;
    test_elf_write_u16(global_symbol + 6U, empty_index);

    uint8_t *start_symbol = global_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start_symbol, start_name);
    start_symbol[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start_symbol + 6U, text_index);
    test_elf_write_u64(start_symbol + 16U, text_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + (size_t) text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, text_size,
            0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + (size_t) data_index * LD_ELF64_SHDR_SIZE, data_name,
            LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE,
            data_offset, data_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + (size_t) empty_index * LD_ELF64_SHDR_SIZE,
            empty_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, empty_offset, 0U, 0U, 0U,
            8U, 0U);
    test_elf_write_section(
            sections + (size_t) rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset,
            relocation_count * LD_ELF64_RELA_SIZE, symtab_index, text_index,
            8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 3U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, strtab_offset,
            symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void test_elf_empty_alloc_section_symbols(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_empty_alloc_object(&object_size);
    char object_path[] = "/tmp/nature-ld-empty-alloc-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-empty-alloc-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    const uint8_t *empty = test_elf_find_output_section(
            image, image_size, ".tm_clone_table");
    assert(text != NULL && data != NULL && empty != NULL);

    uint64_t text_offset = test_elf_read_u64(text + 24U);
    uint64_t empty_address = test_elf_read_u64(empty + 16U);
    assert(test_elf_read_u64(empty + 32U) == 0U);
    assert(test_elf_read_u64(empty + 48U) == 8U);
    assert(test_elf_read_u64(empty + 8U) ==
           (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE));
    assert(text_offset <= image_size &&
           3U * sizeof(uint64_t) <= image_size - text_offset);
    for (size_t i = 0U; i < 3U; i++) {
        assert(test_elf_read_u64(image + (size_t) text_offset +
                                 i * sizeof(uint64_t)) == empty_address);
    }

    uint64_t data_end = test_elf_read_u64(data + 16U) +
                        test_elf_read_u64(data + 32U);
    uint64_t expected_empty_address = (data_end + 7U) & ~7ULL;
    assert(empty_address == expected_empty_address);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_crt(void) {
    test_elf_empty_alloc_section_symbols();
}
