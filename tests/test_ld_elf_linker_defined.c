#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    TEST_START_STOP_TEXT = 1,
    TEST_START_STOP_REGISTRY,
    TEST_START_STOP_WRITE_REGISTRY,
    TEST_START_STOP_EXEC_REGISTRY,
    TEST_START_STOP_DATA,
    TEST_START_STOP_RELA,
    TEST_START_STOP_SYMTAB,
    TEST_START_STOP_STRTAB,
    TEST_START_STOP_SHSTRTAB,
    TEST_START_STOP_SECTION_COUNT,
};

typedef struct {
    bool found;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} test_elf_output_symbol_t;

static uint8_t *make_test_elf_start_stop_object(
        const char *registry_name, const char *start_name,
        const char *stop_name, bool define_start_override,
        size_t *result_size) {
    assert(registry_name && registry_name[0] && start_name && start_name[0] &&
           stop_name && stop_name[0] && result_size);

    char symbol_names[256] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t start_entry_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size, "_start");
    const uint32_t section_start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            start_name);
    const uint32_t section_stop_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            stop_name);

    char section_names[256] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t registry_section_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            registry_name);
    const uint32_t write_registry_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            "write_registry");
    const uint32_t exec_registry_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            "exec_registry");
    const uint32_t data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".data.refs");
    const uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.data.refs");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    static const uint8_t registry_bytes[] = {
            0x10U,
            0x21U,
            0x32U,
            0x43U,
            0x54U,
            0x65U,
            0x76U,
            0x87U,
            0x98U,
            0xa9U,
            0xbaU,
            0xcbU,
    };
    static const uint8_t write_registry_bytes[] = {
            0xd1U,
            0xd2U,
            0xd3U,
            0xd4U,
            0xd5U,
    };
    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t registry_offset = text_offset + sizeof(uint32_t);
    const size_t write_registry_offset = test_elf_align(
            registry_offset + sizeof(registry_bytes), 8U);
    const size_t exec_registry_offset = test_elf_align(
            write_registry_offset + sizeof(write_registry_bytes), 4U);
    const size_t data_offset =
            test_elf_align(exec_registry_offset + sizeof(uint32_t), 8U);
    const size_t data_size = 2U * sizeof(uint64_t);
    const size_t rela_offset = test_elf_align(data_offset + data_size, 8U);
    const size_t relocation_count = 2U;
    const size_t symtab_offset =
            rela_offset + relocation_count * LD_ELF64_RELA_SIZE;
    const size_t symbol_count = 4U;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        TEST_START_STOP_SECTION_COUNT * LD_ELF64_SHDR_SIZE;
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
    test_elf_write_u16(bytes + 60U, TEST_START_STOP_SECTION_COUNT);
    test_elf_write_u16(bytes + 62U, TEST_START_STOP_SHSTRTAB);

    test_elf_write_u32(bytes + text_offset, UINT32_C(0xd65f03c0));
    memcpy(bytes + registry_offset, registry_bytes, sizeof(registry_bytes));
    memcpy(bytes + write_registry_offset, write_registry_bytes,
           sizeof(write_registry_bytes));
    test_elf_write_u32(bytes + exec_registry_offset,
                       UINT32_C(0xd503201f));
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *relocations = bytes + rela_offset;
    test_elf_write_u64(relocations, 0U);
    test_elf_write_u64(
            relocations + 8U,
            LD_ELF_RELA_INFO(2U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(relocations + 16U, 0U);
    relocations += LD_ELF64_RELA_SIZE;
    test_elf_write_u64(relocations, sizeof(uint64_t));
    test_elf_write_u64(
            relocations + 8U,
            LD_ELF_RELA_INFO(3U, LD_ELF_R_AARCH64_ABS64));
    test_elf_write_u64(relocations + 16U, 0U);

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *entry = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(entry, start_entry_name);
    entry[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(entry + 6U, TEST_START_STOP_TEXT);
    test_elf_write_u64(entry + 16U, sizeof(uint32_t));
    uint8_t *start = entry + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, section_start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(start + 6U,
                       define_start_override ? LD_ELF_SHN_ABS
                                             : LD_ELF_SHN_UNDEF);
    if (define_start_override)
        test_elf_write_u64(start + 8U, UINT64_C(0x12345678));
    uint8_t *stop = start + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(stop, section_stop_name);
    stop[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(stop + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + TEST_START_STOP_TEXT * LD_ELF64_SHDR_SIZE,
            text_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + TEST_START_STOP_REGISTRY * LD_ELF64_SHDR_SIZE,
            registry_section_name, LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC,
            registry_offset, sizeof(registry_bytes), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections +
                    TEST_START_STOP_WRITE_REGISTRY * LD_ELF64_SHDR_SIZE,
            write_registry_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, write_registry_offset,
            sizeof(write_registry_bytes), 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + TEST_START_STOP_EXEC_REGISTRY * LD_ELF64_SHDR_SIZE,
            exec_registry_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
            exec_registry_offset, sizeof(uint32_t), 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + TEST_START_STOP_DATA * LD_ELF64_SHDR_SIZE, data_name,
            LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE,
            data_offset, data_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + TEST_START_STOP_RELA * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset,
            relocation_count * LD_ELF64_RELA_SIZE, TEST_START_STOP_SYMTAB,
            TEST_START_STOP_DATA, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + TEST_START_STOP_SYMTAB * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, TEST_START_STOP_STRTAB, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + TEST_START_STOP_STRTAB * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, strtab_offset,
            symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_START_STOP_SHSTRTAB * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint16_t test_elf_output_section_index(const uint8_t *image,
                                              size_t image_size,
                                              const uint8_t *section) {
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t entry_size = test_elf_read_u16(image + 58U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    assert(entry_size == LD_ELF64_SHDR_SIZE && table_offset <= image_size);
    const uint8_t *table = image + (size_t) table_offset;
    assert(section >= table && section < table +
                                                 (size_t) section_count * entry_size);
    size_t delta = (size_t) (section - table);
    assert(delta % entry_size == 0U && delta / entry_size <= UINT16_MAX);
    return (uint16_t) (delta / entry_size);
}

static test_elf_output_symbol_t test_elf_find_output_symbol(
        const uint8_t *image, size_t image_size, const char *name) {
    test_elf_output_symbol_t result = {0};
    const uint8_t *symtab =
            test_elf_find_output_section(image, image_size, ".symtab");
    const uint8_t *strtab =
            test_elf_find_output_section(image, image_size, ".strtab");
    assert(symtab != NULL && strtab != NULL);
    uint64_t symbols_offset = test_elf_read_u64(symtab + 24U);
    uint64_t symbols_size = test_elf_read_u64(symtab + 32U);
    uint64_t entry_size = test_elf_read_u64(symtab + 56U);
    uint64_t strings_offset = test_elf_read_u64(strtab + 24U);
    uint64_t strings_size = test_elf_read_u64(strtab + 32U);
    assert(entry_size == LD_ELF64_SYM_SIZE &&
           symbols_offset <= image_size &&
           symbols_size <= image_size - symbols_offset &&
           symbols_size % entry_size == 0U &&
           strings_offset <= image_size &&
           strings_size <= image_size - strings_offset);
    const char *strings = (const char *) image + strings_offset;
    for (uint64_t offset = 0U; offset < symbols_size;
         offset += entry_size) {
        const uint8_t *symbol = image + symbols_offset + offset;
        uint32_t name_offset = test_elf_read_u32(symbol);
        assert(name_offset < strings_size &&
               memchr(strings + name_offset, '\0',
                      (size_t) strings_size - name_offset));
        if (strcmp(strings + name_offset, name) != 0) continue;
        result.found = true;
        result.info = symbol[4];
        result.other = symbol[5];
        result.section_index = test_elf_read_u16(symbol + 6U);
        result.value = test_elf_read_u64(symbol + 8U);
        result.size = test_elf_read_u64(symbol + 16U);
        break;
    }
    return result;
}

static const uint8_t *test_elf_output_section_contents(
        const uint8_t *image, size_t image_size, const uint8_t *section) {
    assert(section != NULL);
    uint64_t offset = test_elf_read_u64(section + 24U);
    uint64_t size = test_elf_read_u64(section + 32U);
    assert(offset <= image_size && size <= image_size - offset);
    return image + (size_t) offset;
}

static void test_elf_start_stop_link(bool define_start_override) {
    size_t object_size;
    uint8_t *object = make_test_elf_start_stop_object(
            "nature_registry", "__start_nature_registry",
            "__stop_nature_registry", define_start_override, &object_size);
    char object_path[] = "/tmp/nature-ld-start-stop-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-start-stop-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *registry = test_elf_find_output_section(
            image, image_size, "nature_registry");
    const uint8_t *write_registry = test_elf_find_output_section(
            image, image_size, "write_registry");
    const uint8_t *exec_registry = test_elf_find_output_section(
            image, image_size, "exec_registry");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    assert(registry && write_registry && exec_registry && data);
    assert(test_elf_read_u64(registry + 32U) == 12U);
    assert(test_elf_read_u64(write_registry + 32U) == 5U);
    assert(test_elf_read_u64(exec_registry + 32U) == 4U);
    assert((test_elf_read_u64(registry + 8U) & LD_ELF_SHF_WRITE) == 0U);
    assert((test_elf_read_u64(write_registry + 8U) & LD_ELF_SHF_WRITE) !=
           0U);
    assert((test_elf_read_u64(exec_registry + 8U) &
            LD_ELF_SHF_EXECINSTR) != 0U);

    uint64_t registry_start = test_elf_read_u64(registry + 16U);
    uint64_t registry_stop =
            registry_start + test_elf_read_u64(registry + 32U);
    const uint8_t *references =
            test_elf_output_section_contents(image, image_size, data);
    assert(test_elf_read_u64(data + 32U) == 2U * sizeof(uint64_t));
    assert(test_elf_read_u64(references) == registry_start);
    assert(test_elf_read_u64(references + sizeof(uint64_t)) ==
           registry_stop);

    test_elf_output_symbol_t start = test_elf_find_output_symbol(
            image, image_size, "__start_nature_registry");
    test_elf_output_symbol_t stop = test_elf_find_output_symbol(
            image, image_size, "__stop_nature_registry");
    assert(start.found && stop.found);
    assert(start.value == registry_start && stop.value == registry_stop);
    uint16_t registry_index =
            test_elf_output_section_index(image, image_size, registry);
    assert(start.section_index == registry_index &&
           stop.section_index == registry_index);
    assert((start.info >> 4U) == LD_ELF_STB_LOCAL &&
           (stop.info >> 4U) == LD_ELF_STB_LOCAL);
    assert((start.other & 3U) == LD_ELF_STV_HIDDEN &&
           (stop.other & 3U) == LD_ELF_STV_HIDDEN);
    assert(start.size == 0U && stop.size == 0U);

    test_elf_output_symbol_t write_start = test_elf_find_output_symbol(
            image, image_size, "__start_write_registry");
    test_elf_output_symbol_t write_stop = test_elf_find_output_symbol(
            image, image_size, "__stop_write_registry");
    test_elf_output_symbol_t exec_start = test_elf_find_output_symbol(
            image, image_size, "__start_exec_registry");
    test_elf_output_symbol_t exec_stop = test_elf_find_output_symbol(
            image, image_size, "__stop_exec_registry");
    assert(write_start.found && write_stop.found && exec_start.found &&
           exec_stop.found);
    assert(write_start.value == test_elf_read_u64(write_registry + 16U));
    assert(write_stop.value == write_start.value + 5U);
    assert(exec_start.value == test_elf_read_u64(exec_registry + 16U));
    assert(exec_stop.value == exec_start.value + 4U);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_start_stop_failure(const char *section_name,
                                        const char *start_name,
                                        const char *stop_name) {
    size_t object_size;
    uint8_t *object = make_test_elf_start_stop_object(
            section_name, start_name, stop_name, false, &object_size);
    char object_path[] = "/tmp/nature-ld-start-stop-invalid-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path =
            "/tmp/nature-ld-start-stop-invalid-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    assert(link_test_elf_inputs(output_path, inputs, 1U, &capture) ==
           LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "undefined ELF symbol") != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(object_path);
}

void test_ld_elf_linker_defined(void) {
    test_elf_start_stop_link(false);
    test_elf_start_stop_link(true);
    test_elf_start_stop_failure(
            "nature-bad", "__start_nature-bad", "__stop_nature-bad");
    test_elf_start_stop_failure(
            "nature_registry", "__start_missing", "__stop_missing");
}
