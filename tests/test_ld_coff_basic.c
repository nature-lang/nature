#include "src/binary/coff/coff_writer.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_coff_internal.h"
#include "src/ld/ld_coff_reader.h"
#include "src/ld/ld_output.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char message[4096];
} diagnostic_capture_t;

static void diagnostic(void *context, ld_diag_level_t level,
                       const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static void put16(uint8_t *bytes, size_t size, size_t offset,
                  uint16_t value) {
    assert(ld_coff_write_u16(bytes, size, offset, value));
}

static void put32(uint8_t *bytes, size_t size, size_t offset,
                  uint32_t value) {
    assert(ld_coff_write_u32(bytes, size, offset, value));
}

static void write_all(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

static uint8_t *read_all(const char *path, size_t *size);

static void test_atomic_output_replace(const char *directory) {
    static const uint8_t first[] = {0x11U, 0x22U};
    static const uint8_t second[] = {0x33U, 0x44U, 0x55U};
    char output[512];
    snprintf(output, sizeof(output), "%s/atomic-output.bin", directory);
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;

    assert(ld_write_file_atomic(&options, output, first, sizeof(first),
                                false) == LD_OK);
    assert(ld_write_file_atomic(&options, output, second, sizeof(second),
                                false) == LD_OK);
    size_t size = 0U;
    uint8_t *bytes = read_all(output, &size);
    assert(size == sizeof(second));
    assert(memcmp(bytes, second, sizeof(second)) == 0);
    free(bytes);
}

static void put_archive_decimal(uint8_t *field, size_t width, size_t value) {
    char encoded[32];
    int length = snprintf(encoded, sizeof(encoded), "%zu", value);
    assert(length > 0 && (size_t) length <= width);
    memset(field, ' ', width);
    memcpy(field, encoded, (size_t) length);
}

static void write_archive_member(const char *path, const char *member_name,
                                 const uint8_t *bytes, size_t size) {
    uint8_t header[LD_COFF_ARCHIVE_HEADER_SIZE];
    size_t name_length = strlen(member_name);
    assert(name_length + 1U <= 16U);
    memset(header, ' ', sizeof(header));
    memcpy(header, member_name, name_length);
    header[name_length] = '/';
    put_archive_decimal(header + 16U, 12U, 0U);
    put_archive_decimal(header + 28U, 6U, 0U);
    put_archive_decimal(header + 34U, 6U, 0U);
    put_archive_decimal(header + 40U, 8U, 100644U);
    put_archive_decimal(header + 48U, 10U, size);
    header[58U] = '`';
    header[59U] = '\n';

    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(LD_COFF_ARCHIVE_MAGIC, 1U, LD_COFF_ARCHIVE_MAGIC_SIZE,
                  file) == LD_COFF_ARCHIVE_MAGIC_SIZE);
    assert(fwrite(header, 1U, sizeof(header), file) == sizeof(header));
    assert(fwrite(bytes, 1U, size, file) == size);
    if ((size & 1U) != 0U) assert(fputc('\n', file) == '\n');
    assert(fclose(file) == 0);
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t) length + 1U);
    assert(bytes);
    assert(fread(bytes, 1U, (size_t) length, file) == (size_t) length);
    bytes[length] = 0U;
    assert(fclose(file) == 0);
    *size = (size_t) length;
    return bytes;
}

static uint8_t *build_entry_object(bool with_base_relocation, size_t *size) {
    static const uint8_t code[] = {
            0x48, 0x83, 0xec, 0x28, /* sub rsp, 40 */
            0xb9, 0x2a, 0x00, 0x00, 0x00, /* mov ecx, 42 */
            0xe8, 0x00, 0x00, 0x00, 0x00, /* call ExitProcess */
            0xcc, /* unreachable */
    };
    const uint16_t section_count = with_base_relocation ? 2U : 1U;
    const size_t section_table = LD_COFF_HEADER_SIZE;
    const size_t text_raw = section_table +
                            section_count * LD_COFF_SECTION_HEADER_SIZE;
    const size_t text_reloc = text_raw + sizeof(code);
    const size_t data_raw = text_reloc + LD_COFF_RELOCATION_SIZE;
    const size_t data_reloc = data_raw + (with_base_relocation ? 8U : 0U);
    const size_t symbol_table =
            with_base_relocation ? data_reloc + LD_COFF_RELOCATION_SIZE
                                 : data_raw;
    const size_t string_table = symbol_table + 2U * LD_COFF_SYMBOL_SIZE;
    *size = string_table + 16U;
    uint8_t *bytes = calloc(1, *size);
    assert(bytes);

    put16(bytes, *size, 0U, LD_COFF_MACHINE_AMD64);
    put16(bytes, *size, 2U, section_count);
    put32(bytes, *size, 8U, (uint32_t) symbol_table);
    put32(bytes, *size, 12U, 2U);

    memcpy(bytes + section_table, ".text", 5U);
    put32(bytes, *size, section_table + 16U, sizeof(code));
    put32(bytes, *size, section_table + 20U, (uint32_t) text_raw);
    put32(bytes, *size, section_table + 24U, (uint32_t) text_reloc);
    put16(bytes, *size, section_table + 32U, 1U);
    put32(bytes, *size, section_table + 36U,
          LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                  LD_COFF_SCN_MEM_READ);
    memcpy(bytes + text_raw, code, sizeof(code));
    put32(bytes, *size, text_reloc, 10U);
    put32(bytes, *size, text_reloc + 4U, 1U);
    put16(bytes, *size, text_reloc + 8U, LD_COFF_REL_AMD64_REL32);

    if (with_base_relocation) {
        const size_t data_header =
                section_table + LD_COFF_SECTION_HEADER_SIZE;
        memcpy(bytes + data_header, ".data", 5U);
        put32(bytes, *size, data_header + 16U, 8U);
        put32(bytes, *size, data_header + 20U, (uint32_t) data_raw);
        put32(bytes, *size, data_header + 24U, (uint32_t) data_reloc);
        put16(bytes, *size, data_header + 32U, 1U);
        put32(bytes, *size, data_header + 36U,
              LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
                      LD_COFF_SCN_MEM_WRITE);
        put32(bytes, *size, data_reloc, 0U);
        put32(bytes, *size, data_reloc + 4U, 0U);
        put16(bytes, *size, data_reloc + 8U, LD_COFF_REL_AMD64_ADDR64);
    }

    memcpy(bytes + symbol_table, "entry", 5U);
    put16(bytes, *size, symbol_table + 12U, 1U);
    put16(bytes, *size, symbol_table + 14U, 0x20U);
    bytes[symbol_table + 16U] = LD_COFF_STORAGE_CLASS_EXTERNAL;
    const size_t exit_symbol = symbol_table + LD_COFF_SYMBOL_SIZE;
    put32(bytes, *size, exit_symbol + 4U, 4U);
    bytes[exit_symbol + 16U] = LD_COFF_STORAGE_CLASS_EXTERNAL;
    put32(bytes, *size, string_table, 16U);
    memcpy(bytes + string_table + 4U, "ExitProcess", 12U);
    return bytes;
}

static uint8_t *build_import_object(size_t *size) {
    static const char symbol[] = "ExitProcess";
    static const char dll[] = "KERNEL32.dll";
    *size = LD_COFF_IMPORT_HEADER_SIZE + sizeof(symbol) + sizeof(dll);
    uint8_t *bytes = calloc(1, *size);
    assert(bytes);
    put16(bytes, *size, 2U, 0xffffU);
    put16(bytes, *size, 6U, LD_COFF_MACHINE_AMD64);
    put32(bytes, *size, 12U, sizeof(symbol) + sizeof(dll));
    put16(bytes, *size, 18U,
          LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE, symbol, sizeof(symbol));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE + sizeof(symbol), dll,
           sizeof(dll));
    return bytes;
}

static uint8_t *build_regular_import_override_object(size_t *size) {
    coff_object_t *object = coff_object_create_amd64("regular-override.c");
    assert(object);
    coff_section_t *text = coff_object_text(object);
    assert(text);
    static const uint8_t code[] = {0xc3U, 0xc3U};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "ExitProcess", text, 1U,
                   COFF_SYMBOL_TYPE_FUNCTION, LD_COFF_STORAGE_CLASS_EXTERNAL,
                   NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_common_import_override_object(size_t *size) {
    coff_object_t *object = coff_object_create_amd64("common-override.c");
    assert(object);
    coff_section_t *text = coff_object_text(object);
    assert(text);
    static const uint8_t code[] = {0xc3U};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    coff_symbol_desc_t common = {
            .name = "ExitProcess",
            .value = 64U,
            .section_number = LD_COFF_SYM_UNDEFINED,
            .storage_class = LD_COFF_STORAGE_CLASS_EXTERNAL,
    };
    assert(coff_object_add_symbol(object, &common, NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_common_only_object(uint32_t common_size, size_t *size) {
    coff_object_t *object = coff_object_create_amd64("smaller-common.c");
    assert(object);
    coff_symbol_desc_t common = {
            .name = "ExitProcess",
            .value = common_size,
            .section_number = LD_COFF_SYM_UNDEFINED,
            .storage_class = LD_COFF_STORAGE_CLASS_EXTERNAL,
    };
    assert(coff_object_add_symbol(object, &common, NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_weak_external_object(bool defines_entry,
                                           const char *source_name,
                                           size_t *size) {
    coff_object_t *object = coff_object_create_amd64(source_name);
    assert(object);
    coff_section_t *text = coff_object_text(object);
    assert(text);
    static const uint8_t code[] = {0xc3U, 0xc3U};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    if (defines_entry) {
        assert(coff_object_define_symbol(
                       object, "entry", text, 0U,
                       COFF_SYMBOL_TYPE_FUNCTION,
                       LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) ==
               COFF_WRITER_OK);
    }
    uint32_t fallback = COFF_SYMBOL_INDEX_NONE;
    assert(coff_object_define_symbol(
                   object, ".weak.foo.default", text, 1U,
                   COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, &fallback) ==
           COFF_WRITER_OK);
    assert(coff_object_add_weak_external(
                   object, "foo", fallback, LD_COFF_WEAK_SEARCH_ALIAS,
                   NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_comdat_object(uint32_t marker, size_t *size) {
    coff_object_t *object = coff_object_create("comdat-fixture.c");
    assert(object);
    coff_section_t *text = NULL;
    coff_section_t *xdata = NULL;
    assert(coff_object_add_section(
                   object, ".text$entry",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &text) == COFF_WRITER_OK);
    assert(coff_object_add_section(
                   object, ".xdata$entry",
                   LD_COFF_SCN_CNT_INITIALIZED_DATA |
                           LD_COFF_SCN_MEM_READ,
                   4U, &xdata) == COFF_WRITER_OK);
    static const uint8_t code[] = {0xc3}; /* ret */
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(xdata, &marker, sizeof(marker), 4U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_object_mark_comdat(
                   object, text, LD_COFF_COMDAT_ANY, NULL, "entry", 0U,
                   NULL, NULL) == COFF_WRITER_OK);
    assert(coff_object_mark_comdat(
                   object, xdata, LD_COFF_COMDAT_ASSOCIATIVE, text, NULL,
                   0U, NULL, NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_long_debug_object(size_t *size) {
    coff_object_t *object = coff_object_create_amd64("long-debug.c");
    assert(object);
    coff_section_t *text = coff_object_text(object);
    coff_section_t *debug_addr = NULL;
    coff_section_t *debug_abbrev = NULL;
    assert(text);
    uint32_t debug_characteristics =
            LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
            LD_COFF_SCN_MEM_DISCARDABLE;
    assert(coff_object_add_section(object, ".debug_addr",
                                   debug_characteristics, 1U,
                                   &debug_addr) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".debug_abbrev",
                                   debug_characteristics, 1U,
                                   &debug_abbrev) == COFF_WRITER_OK);
    static const uint8_t code[] = {0xc3};
    static const uint8_t address_marker[] = {0xa1};
    static const uint8_t abbreviation_marker[] = {0xb2};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(debug_addr, address_marker,
                               sizeof(address_marker), 1U,
                               NULL) == COFF_WRITER_OK);
    assert(coff_section_append(debug_abbrev, abbreviation_marker,
                               sizeof(abbreviation_marker), 1U,
                               NULL) == COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_ordering_object(size_t *size) {
    coff_object_t *object = coff_object_create("ordering-fixture.c");
    assert(object);
    coff_section_t *text = NULL;
    coff_section_t *data = NULL;
    coff_section_t *ctor_priority_zero = NULL;
    coff_section_t *ctor_priority_hundred = NULL;
    coff_section_t *crt_first = NULL;
    coff_section_t *crt_second = NULL;
    const uint32_t code_flags = LD_COFF_SCN_CNT_CODE |
                                LD_COFF_SCN_MEM_EXECUTE |
                                LD_COFF_SCN_MEM_READ;
    const uint32_t data_flags = LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                LD_COFF_SCN_MEM_READ |
                                LD_COFF_SCN_MEM_WRITE;
    assert(coff_object_add_section(object, ".text", code_flags, 16U,
                                   &text) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".data", data_flags, 8U,
                                   &data) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".ctors.65435", data_flags, 8U,
                                   &ctor_priority_hundred) ==
           COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".ctors.65535", data_flags, 8U,
                                   &ctor_priority_zero) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".CRT$XCU", data_flags, 1U,
                                   &crt_first) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".CRT$XCV", data_flags, 1U,
                                   &crt_second) == COFF_WRITER_OK);

    static const uint8_t code[] = {0xc3, 0xc3, 0xc3};
    static const uint8_t crt_first_marker = 0x11U;
    static const uint8_t crt_second_marker = 0x22U;
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append_zeros(data, 8U, 8U, NULL) == COFF_WRITER_OK);
    assert(coff_section_append_zeros(ctor_priority_hundred, 8U, 8U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append_zeros(ctor_priority_zero, 8U, 8U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(crt_first, &crt_first_marker, 1U, 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(crt_second, &crt_second_marker, 1U, 1U,
                               NULL) == COFF_WRITER_OK);

    uint32_t entry = COFF_SYMBOL_INDEX_NONE;
    uint32_t ctor_zero = COFF_SYMBOL_INDEX_NONE;
    uint32_t ctor_hundred = COFF_SYMBOL_INDEX_NONE;
    uint32_t ctor_list = COFF_SYMBOL_INDEX_NONE;
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, &entry) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "ctor_priority_zero", text, 1U,
                   COFF_SYMBOL_TYPE_FUNCTION, LD_COFF_STORAGE_CLASS_EXTERNAL,
                   &ctor_zero) == COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "ctor_priority_hundred", text, 2U,
                   COFF_SYMBOL_TYPE_FUNCTION, LD_COFF_STORAGE_CLASS_EXTERNAL,
                   &ctor_hundred) == COFF_WRITER_OK);
    assert(coff_object_get_or_add_symbol_reference(
                   object, "__CTOR_LIST__", true, 0U, &ctor_list) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   data, 0U, ctor_list, LD_COFF_REL_AMD64_ADDR64, 0) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   ctor_priority_zero, 0U, ctor_zero,
                   LD_COFF_REL_AMD64_ADDR64, 0) == COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   ctor_priority_hundred, 0U, ctor_hundred,
                   LD_COFF_REL_AMD64_ADDR64, 0) == COFF_WRITER_OK);
    (void) entry;

    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    /* The object builder intentionally rejects duplicate names.  Turn the
       sixth wire-format section into a second .CRT$XCU contribution to
       exercise parser/layout ordering for compiler-produced COFF input. */
    const size_t duplicate_name =
            LD_COFF_HEADER_SIZE + 5U * LD_COFF_SECTION_HEADER_SIZE;
    assert(duplicate_name + 8U <= *size);
    memcpy(bytes + duplicate_name, ".CRT$XCU", 8U);
    coff_object_destroy(object);
    return bytes;
}

static uint8_t *build_relocation_matrix_object(size_t *size) {
    coff_object_t *object = coff_object_create("relocation-matrix.c");
    assert(object);
    coff_section_t *text = NULL;
    coff_section_t *data = NULL;
    assert(coff_object_add_section(
                   object, ".text",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &text) == COFF_WRITER_OK);
    assert(coff_object_add_section(
                   object, ".data",
                   LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
                           LD_COFF_SCN_MEM_WRITE,
                   8U, &data) == COFF_WRITER_OK);
    static const uint8_t code[] = {0xc3U, 0xc3U};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append_zeros(data, 48U, 8U, NULL) == COFF_WRITER_OK);

    uint32_t entry = COFF_SYMBOL_INDEX_NONE;
    uint32_t target = COFF_SYMBOL_INDEX_NONE;
    uint32_t absolute = COFF_SYMBOL_INDEX_NONE;
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, &entry) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "matrix_target", text, 1U,
                   COFF_SYMBOL_TYPE_FUNCTION, LD_COFF_STORAGE_CLASS_EXTERNAL,
                   &target) == COFF_WRITER_OK);
    coff_symbol_desc_t absolute_symbol = {
            .name = "matrix_absolute",
            .value = 0x1234U,
            .section_number = LD_COFF_SYM_ABSOLUTE,
            .storage_class = LD_COFF_STORAGE_CLASS_EXTERNAL,
    };
    assert(coff_object_add_symbol(object, &absolute_symbol, &absolute) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 0U, target,
                                       LD_COFF_REL_AMD64_ADDR64) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 8U, absolute,
                                       LD_COFF_REL_AMD64_ADDR32) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 12U, target,
                                       LD_COFF_REL_AMD64_ADDR32NB) ==
           COFF_WRITER_OK);
    static const uint16_t rel32_types[] = {
            LD_COFF_REL_AMD64_REL32,
            LD_COFF_REL_AMD64_REL32_1,
            LD_COFF_REL_AMD64_REL32_2,
            LD_COFF_REL_AMD64_REL32_3,
            LD_COFF_REL_AMD64_REL32_4,
            LD_COFF_REL_AMD64_REL32_5,
    };
    for (size_t i = 0U; i < sizeof(rel32_types) / sizeof(*rel32_types); i++)
        assert(coff_section_add_relocation(data, 16U + (uint32_t) i * 4U,
                                           target, rel32_types[i]) ==
               COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 40U, target,
                                       LD_COFF_REL_AMD64_SECTION) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 42U, target,
                                       LD_COFF_REL_AMD64_ABSOLUTE) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation(data, 44U, target,
                                       LD_COFF_REL_AMD64_SECREL) ==
           COFF_WRITER_OK);
    (void) entry;

    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static int link_inputs(const char *object, const char *import,
                       const char *output, const char *map,
                       diagnostic_capture_t *capture) {
    char *inputs[] = {(char *) object, (char *) import};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.map_path = map;
    options.entry_symbol = "entry";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = 2U;
    options.inputs.capacity = 2U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = capture;
    return ld_link(&options);
}

static uint32_t read32(ld_coff_view_t view, uint64_t offset) {
    uint32_t value;
    assert(ld_coff_read_u32(view, offset, &value));
    return value;
}

static uint16_t read16(ld_coff_view_t view, uint64_t offset) {
    uint16_t value;
    assert(ld_coff_read_u16(view, offset, &value));
    return value;
}

static uint64_t read64(ld_coff_view_t view, uint64_t offset) {
    uint64_t value;
    assert(ld_coff_read_u64(view, offset, &value));
    return value;
}

static uint64_t pe_optional_offset(ld_coff_view_t view) {
    uint32_t pe = read32(view, 0x3cU);
    assert(pe == LD_PE_OFFSET);
    assert(ld_coff_range_ok(view, pe, 4U));
    assert(memcmp(view.bytes + pe, "PE\0\0", 4U) == 0);
    return (uint64_t) pe + 4U + LD_PE_COFF_HEADER_SIZE;
}

static uint32_t pe_directory_rva(ld_coff_view_t view, unsigned index) {
    uint64_t optional = pe_optional_offset(view);
    return read32(view, optional + 112U + index * 8U);
}

static uint32_t pe_directory_size(ld_coff_view_t view, unsigned index) {
    uint64_t optional = pe_optional_offset(view);
    return read32(view, optional + 116U + index * 8U);
}

static bool pe_section_name(ld_coff_view_t view, uint64_t header, char *name,
                            size_t capacity) {
    assert(capacity != 0U && ld_coff_range_ok(view, header, 8U));
    char encoded[9] = {0};
    memcpy(encoded, view.bytes + header, 8U);
    if (encoded[0] != '/') {
        snprintf(name, capacity, "%s", encoded);
        return true;
    }
    char *end = NULL;
    unsigned long offset = strtoul(encoded + 1U, &end, 10);
    if (!end || *end != '\0' || offset > UINT32_MAX) return false;
    uint32_t pe = read32(view, 0x3cU);
    uint32_t symbol_table = read32(view, pe + 12U);
    uint32_t symbol_count = read32(view, pe + 16U);
    uint64_t string_table =
            (uint64_t) symbol_table + symbol_count * LD_COFF_SYMBOL_SIZE;
    uint64_t string_offset = string_table + offset;
    if (!ld_coff_range_ok(view, string_offset, 1U)) return false;
    size_t available = view.size - (size_t) string_offset;
    const char *value = (const char *) view.bytes + string_offset;
    const char *terminator = memchr(value, '\0', available);
    if (!terminator) return false;
    size_t length = (size_t) (terminator - value);
    if (length >= capacity) return false;
    memcpy(name, value, length + 1U);
    return true;
}

static bool pe_has_section(ld_coff_view_t view, const char *name,
                           uint32_t *rva, uint32_t *raw_offset) {
    uint32_t pe = read32(view, 0x3cU);
    uint16_t count = read16(view, pe + 6U);
    uint64_t headers = pe + 4U + LD_PE_COFF_HEADER_SIZE +
                       LD_PE_OPTIONAL_HEADER64_SIZE;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t offset = headers + i * LD_PE_SECTION_HEADER_SIZE;
        char actual[128] = {0};
        assert(pe_section_name(view, offset, actual, sizeof(actual)));
        if (strcmp(actual, name) != 0) continue;
        if (rva) *rva = read32(view, offset + 12U);
        if (raw_offset) *raw_offset = read32(view, offset + 20U);
        return true;
    }
    return false;
}

static uint32_t pe_section_virtual_size(ld_coff_view_t view,
                                        const char *name) {
    uint32_t pe = read32(view, 0x3cU);
    uint16_t count = read16(view, pe + 6U);
    uint64_t headers = pe + 4U + LD_PE_COFF_HEADER_SIZE +
                       LD_PE_OPTIONAL_HEADER64_SIZE;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t offset = headers + i * LD_PE_SECTION_HEADER_SIZE;
        char actual[128] = {0};
        assert(pe_section_name(view, offset, actual, sizeof(actual)));
        if (strcmp(actual, name) == 0) return read32(view, offset + 8U);
    }
    return 0U;
}

static uint16_t pe_section_index(ld_coff_view_t view, const char *name) {
    uint32_t pe = read32(view, 0x3cU);
    uint16_t count = read16(view, pe + 6U);
    uint64_t headers = pe + 4U + LD_PE_COFF_HEADER_SIZE +
                       LD_PE_OPTIONAL_HEADER64_SIZE;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t offset = headers + i * LD_PE_SECTION_HEADER_SIZE;
        char actual[128] = {0};
        assert(pe_section_name(view, offset, actual, sizeof(actual)));
        if (strcmp(actual, name) == 0) return i + 1U;
    }
    return 0U;
}

static void test_minimal_import_and_determinism(const char *directory) {
    char object[512], import[512], first_output[512], second_output[512];
    char first_map[512], second_map[512];
    snprintf(object, sizeof(object), "%s/entry.obj", directory);
    snprintf(import, sizeof(import), "%s/kernel32.obj", directory);
    snprintf(first_output, sizeof(first_output), "%s/first.exe", directory);
    snprintf(second_output, sizeof(second_output), "%s/second.exe", directory);
    snprintf(first_map, sizeof(first_map), "%s/first.map", directory);
    snprintf(second_map, sizeof(second_map), "%s/second.map", directory);
    size_t object_size, import_size;
    uint8_t *object_bytes = build_entry_object(false, &object_size);
    uint8_t *import_bytes = build_import_object(&import_size);
    write_all(object, object_bytes, object_size);
    write_all(import, import_bytes, import_size);
    free(object_bytes);
    free(import_bytes);
    diagnostic_capture_t capture = {{0}};
    {
        char *inputs[] = {object};
        ld_options_t unsupported = {0};
        unsupported.os = LD_OS_WINDOWS;
        unsupported.arch = LD_ARCH_ARM64;
        unsupported.output_path = first_output;
        unsupported.entry_symbol = "entry";
        unsupported.inputs.items = inputs;
        unsupported.inputs.count = 1U;
        unsupported.inputs.capacity = 1U;
        unsupported.diagnostic = diagnostic;
        unsupported.diagnostic_context = &capture;
        assert(ld_link(&unsupported) == LD_UNSUPPORTED);
        assert(strstr(capture.message, "windows/amd64"));
        capture.message[0] = '\0';
    }
    assert(link_inputs(object, import, first_output, first_map, &capture) ==
           LD_OK);
    assert(link_inputs(object, import, second_output, second_map, &capture) ==
           LD_OK);
    size_t first_size, second_size;
    uint8_t *first = read_all(first_output, &first_size);
    uint8_t *second = read_all(second_output, &second_size);
    assert(first_size == second_size && memcmp(first, second, first_size) == 0);
    size_t first_map_size, second_map_size;
    uint8_t *first_map_bytes = read_all(first_map, &first_map_size);
    uint8_t *second_map_bytes = read_all(second_map, &second_map_size);
    assert(first_map_size == second_map_size &&
           memcmp(first_map_bytes, second_map_bytes, first_map_size) == 0);
    assert(strstr((const char *) first_map_bytes, "Nature COFF/PE map v1"));
    assert(strstr((const char *) first_map_bytes, ".text    entry"));
    assert(strstr((const char *) first_map_bytes, ".text    ExitProcess"));
    assert(strstr((const char *) first_map_bytes, "entry entry.obj"));
    assert(strstr((const char *) first_map_bytes,
                  "ExitProcess kernel32.obj"));
    assert(!strstr((const char *) first_map_bytes, directory));
    ld_coff_view_t view = {first, first_size};
    uint64_t optional = pe_optional_offset(view);
    assert(read32(view, optional + 16U) != 0U);
    assert(pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT) != 0U);
    assert(pe_directory_rva(view, LD_PE_DIRECTORY_IAT) != 0U);
    assert(pe_directory_rva(view, LD_PE_DIRECTORY_BASERELOC) != 0U);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_BASERELOC) == 12U);
    uint32_t reloc_raw;
    assert(pe_has_section(view, ".reloc", NULL, &reloc_raw));
    assert(read32(view, reloc_raw) == 0U);
    assert(read32(view, reloc_raw + 4U) == 12U);
    uint16_t file_flags = read16(view, LD_PE_OFFSET + 4U + 18U);
    uint16_t dll_flags = read16(view, optional + 70U);
    assert((file_flags & LD_PE_FILE_RELOCS_STRIPPED) == 0U);
    assert((dll_flags & LD_PE_DLL_DYNAMIC_BASE) != 0U);
    assert((dll_flags & LD_PE_DLL_HIGH_ENTROPY_VA) != 0U);
    free(first);
    free(second);
    free(first_map_bytes);
    free(second_map_bytes);
}

static void test_base_relocation(const char *directory) {
    char object[512], import[512], output[512];
    snprintf(object, sizeof(object), "%s/baserel.obj", directory);
    snprintf(import, sizeof(import), "%s/baserel-kernel32.obj", directory);
    snprintf(output, sizeof(output), "%s/baserel.exe", directory);
    size_t object_size, import_size;
    uint8_t *object_bytes = build_entry_object(true, &object_size);
    uint8_t *import_bytes = build_import_object(&import_size);
    write_all(object, object_bytes, object_size);
    write_all(import, import_bytes, import_size);
    free(object_bytes);
    free(import_bytes);
    diagnostic_capture_t capture = {{0}};
    assert(link_inputs(object, import, output, NULL, &capture) == LD_OK);
    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint64_t optional = pe_optional_offset(view);
    uint32_t reloc_rva = pe_directory_rva(view, LD_PE_DIRECTORY_BASERELOC);
    uint32_t reloc_size = pe_directory_size(view, LD_PE_DIRECTORY_BASERELOC);
    uint32_t section_rva, raw_offset;
    assert(reloc_rva != 0U && reloc_size >= 12U);
    assert(pe_has_section(view, ".reloc", &section_rva, &raw_offset));
    assert(section_rva == reloc_rva);
    assert(read32(view, raw_offset + 4U) == 12U);
    assert((read16(view, raw_offset + 8U) >> 12U) ==
           LD_PE_BASE_RELOC_DIR64);
    uint16_t file_flags = read16(view, LD_PE_OFFSET + 4U + 18U);
    uint16_t dll_flags = read16(view, optional + 70U);
    assert((file_flags & LD_PE_FILE_RELOCS_STRIPPED) == 0U);
    assert((dll_flags & LD_PE_DLL_DYNAMIC_BASE) != 0U);
    assert((dll_flags & LD_PE_DLL_HIGH_ENTROPY_VA) != 0U);
    free(image);
}

static void test_object_definitions_override_earlier_import(
        const char *directory) {
    char import[512], object[512], smaller[512], output[512], map[512];
    snprintf(import, sizeof(import), "%s/override-kernel32.obj", directory);
    snprintf(object, sizeof(object), "%s/regular-override.obj", directory);
    snprintf(output, sizeof(output), "%s/regular-override.exe", directory);
    snprintf(map, sizeof(map), "%s/regular-override.map", directory);

    size_t import_size, object_size;
    uint8_t *import_bytes = build_import_object(&import_size);
    uint8_t *object_bytes =
            build_regular_import_override_object(&object_size);
    write_all(import, import_bytes, import_size);
    write_all(object, object_bytes, object_size);
    free(import_bytes);
    free(object_bytes);

    char *inputs[] = {import, object};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.map_path = map;
    options.entry_symbol = "entry";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = 2U;
    options.inputs.capacity = 2U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_OK);

    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint32_t entry_rva = read32(view, pe_optional_offset(view) + 16U);
    free(image);

    size_t map_size;
    uint8_t *map_bytes = read_all(map, &map_size);
    (void) map_size;
    const char *line = strstr((const char *) map_bytes,
                              " ExitProcess regular-override.obj");
    assert(line != NULL);
    while (line > (const char *) map_bytes && line[-1] != '\n') line--;
    unsigned long long symbol_va = 0U;
    unsigned symbol_rva = 0U;
    assert(sscanf(line, "0x%llx 0x%x", &symbol_va, &symbol_rva) == 2);
    assert(symbol_va == LD_PE_IMAGE_BASE64 + symbol_rva);
    assert(symbol_rva == entry_rva + 1U);
    free(map_bytes);

    snprintf(object, sizeof(object), "%s/common-override.obj", directory);
    snprintf(output, sizeof(output), "%s/common-override.exe", directory);
    snprintf(map, sizeof(map), "%s/common-override.map", directory);
    snprintf(smaller, sizeof(smaller), "%s/smaller-common.obj", directory);
    object_bytes = build_common_import_override_object(&object_size);
    write_all(object, object_bytes, object_size);
    free(object_bytes);
    size_t smaller_size;
    uint8_t *smaller_bytes =
            build_common_only_object(16U, &smaller_size);
    write_all(smaller, smaller_bytes, smaller_size);
    free(smaller_bytes);
    inputs[1] = object;
    char *common_inputs[] = {import, object, smaller};
    options.inputs.items = common_inputs;
    options.inputs.count = 3U;
    options.inputs.capacity = 3U;
    options.output_path = output;
    options.map_path = map;
    capture.message[0] = '\0';
    assert(ld_link(&options) == LD_OK);

    map_bytes = read_all(map, &map_size);
    const char *common_line = strstr((const char *) map_bytes,
                                     " ExitProcess common-override.obj");
    assert(common_line != NULL);
    while (common_line > (const char *) map_bytes && common_line[-1] != '\n')
        common_line--;
    char section_name[16] = {0};
    assert(sscanf(common_line, "0x%*llx 0x%*x %15s", section_name) == 1);
    assert(strcmp(section_name, ".bss") == 0);
    free(map_bytes);
}

static void test_later_archive_does_not_override_weak_external(
        const char *directory) {
    char direct[512], archive[512], output[512], map[512];
    snprintf(direct, sizeof(direct), "%s/weak-direct.obj", directory);
    snprintf(archive, sizeof(archive), "%s/weak-later.lib", directory);
    snprintf(output, sizeof(output), "%s/weak-later.exe", directory);
    snprintf(map, sizeof(map), "%s/weak-later.map", directory);

    size_t direct_size, member_size;
    uint8_t *direct_bytes = build_weak_external_object(
            true, "weak-direct.c", &direct_size);
    uint8_t *member_bytes = build_weak_external_object(
            false, "weak-later.c", &member_size);
    write_all(direct, direct_bytes, direct_size);
    write_archive_member(archive, "weak-later.obj", member_bytes,
                         member_size);
    free(direct_bytes);
    free(member_bytes);

    char *inputs[] = {direct, archive};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.map_path = map;
    options.entry_symbol = "entry";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = 2U;
    options.inputs.capacity = 2U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_OK);

    size_t map_size;
    uint8_t *map_bytes = read_all(map, &map_size);
    (void) map_size;
    assert(strstr((const char *) map_bytes,
                  ".weak.foo.default weak-direct.obj"));
    assert(!strstr((const char *) map_bytes, "weak-later.obj"));
    free(map_bytes);
}

static void test_long_debug_section_names(const char *directory) {
    char object[512], output[512];
    snprintf(object, sizeof(object), "%s/long-debug.obj", directory);
    snprintf(output, sizeof(output), "%s/long-debug.exe", directory);
    size_t object_size;
    uint8_t *object_bytes = build_long_debug_object(&object_size);
    write_all(object, object_bytes, object_size);
    free(object_bytes);

    char *inputs[] = {object};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.pie = true;
    options.debug_mode = LD_DEBUG_DWARF;
    options.inputs.items = inputs;
    options.inputs.count = 1U;
    options.inputs.capacity = 1U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_OK);

    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint32_t address_rva, address_raw, abbreviation_rva, abbreviation_raw;
    uint32_t reloc_rva;
    assert(pe_has_section(view, ".debug_addr", &address_rva, &address_raw));
    assert(pe_has_section(view, ".debug_abbrev", &abbreviation_rva,
                          &abbreviation_raw));
    assert(pe_has_section(view, ".reloc", &reloc_rva, NULL));
    assert(address_raw != abbreviation_raw);
    assert(address_rva != abbreviation_rva);
    assert(reloc_rva > address_rva && reloc_rva > abbreviation_rva);
    assert(read32(view, pe_optional_offset(view) + 56U) >=
           reloc_rva + pe_section_virtual_size(view, ".reloc"));
    assert(image[address_raw] == 0xa1U);
    assert(image[abbreviation_raw] == 0xb2U);
    free(image);
}

static void test_legacy_ctor_and_stable_subsection_order(
        const char *directory) {
    char object[512], output[512];
    snprintf(object, sizeof(object), "%s/ordering.obj", directory);
    snprintf(output, sizeof(output), "%s/ordering.exe", directory);
    size_t object_size;
    uint8_t *object_bytes = build_ordering_object(&object_size);
    write_all(object, object_bytes, object_size);
    free(object_bytes);

    char *inputs[] = {object};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = 1U;
    options.inputs.capacity = 1U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_OK);

    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint32_t text_rva, ctor_raw, crt_raw;
    assert(pe_has_section(view, ".text", &text_rva, NULL));
    assert(pe_has_section(view, ".ctors", NULL, &ctor_raw));
    assert(!pe_has_section(view, ".ctors.6", NULL, NULL));
    assert(pe_section_virtual_size(view, ".ctors") == 32U);
    assert(read64(view, ctor_raw) == UINT64_MAX);
    assert(read64(view, ctor_raw + 8U) ==
           LD_PE_IMAGE_BASE64 + text_rva + 1U);
    assert(read64(view, ctor_raw + 16U) ==
           LD_PE_IMAGE_BASE64 + text_rva + 2U);
    assert(read64(view, ctor_raw + 24U) == 0U);

    assert(pe_has_section(view, ".CRT", NULL, &crt_raw));
    assert(image[crt_raw] == 0x11U);
    assert(image[crt_raw + 1U] == 0x22U);
    free(image);
}

static void test_relocation_apply_matrix(const char *directory) {
    char object[512], output[512];
    snprintf(object, sizeof(object), "%s/relocation-matrix.obj", directory);
    snprintf(output, sizeof(output), "%s/relocation-matrix.exe", directory);
    size_t object_size;
    uint8_t *object_bytes = build_relocation_matrix_object(&object_size);
    write_all(object, object_bytes, object_size);
    free(object_bytes);

    char *inputs[] = {object};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.pie = true;
    options.inputs.items = inputs;
    options.inputs.count = 1U;
    options.inputs.capacity = 1U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == LD_OK);

    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint32_t text_rva, data_rva, data_raw;
    assert(pe_has_section(view, ".text", &text_rva, NULL));
    assert(pe_has_section(view, ".data", &data_rva, &data_raw));
    uint32_t target_rva = text_rva + 1U;
    assert(read64(view, data_raw) == LD_PE_IMAGE_BASE64 + target_rva);
    assert(read32(view, data_raw + 8U) == 0x1234U);
    assert(read32(view, data_raw + 12U) == target_rva);
    for (uint32_t extra = 0U; extra <= 5U; extra++) {
        uint32_t field = 16U + extra * 4U;
        int64_t expected = (int64_t) target_rva -
                           (int64_t) (data_rva + field) - 4 - extra;
        assert((int32_t) read32(view, data_raw + field) ==
               (int32_t) expected);
    }
    assert(read16(view, data_raw + 40U) ==
           pe_section_index(view, ".text"));
    assert(read16(view, data_raw + 42U) == 0U);
    assert(read32(view, data_raw + 44U) == 1U);
    free(image);
}

static void test_comdat_association(const char *directory) {
    char first_object[512], second_object[512], output[512];
    snprintf(first_object, sizeof(first_object), "%s/comdat-first.obj",
             directory);
    snprintf(second_object, sizeof(second_object), "%s/comdat-second.obj",
             directory);
    snprintf(output, sizeof(output), "%s/comdat.exe", directory);
    size_t first_size, second_size;
    uint8_t *first = build_comdat_object(UINT32_C(0x11111111), &first_size);
    uint8_t *second =
            build_comdat_object(UINT32_C(0x22222222), &second_size);
    write_all(first_object, first, first_size);
    write_all(second_object, second, second_size);
    free(first);
    free(second);

    char *inputs[] = {first_object, second_object};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = 2U;
    options.inputs.capacity = 2U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;

    /* Verify the standard 18-byte section-definition aux layout directly:
       NumberLowPart is at +12 and Selection is at +14. */
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);
    assert(ld_coff_load_input(&context, first_object) == LD_OK);
    assert(context.object_count == 1U);
    assert(context.objects[0]->section_count == 2U);
    assert(context.objects[0]->sections[0].comdat_selection ==
           LD_COFF_COMDAT_ANY);
    assert(context.objects[0]->sections[0].associative_section == 0U);
    assert(context.objects[0]->sections[1].comdat_selection ==
           LD_COFF_COMDAT_ASSOCIATIVE);
    assert(context.objects[0]->sections[1].associative_section == 1U);
    ld_coff_context_deinit(&context);

    assert(ld_link(&options) == LD_OK);
    size_t image_size;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    uint32_t xdata_raw;
    assert(pe_has_section(view, ".xdata", NULL, &xdata_raw));
    assert(pe_section_virtual_size(view, ".xdata") == sizeof(uint32_t));
    assert(read32(view, xdata_raw) == UINT32_C(0x11111111));
    free(image);
}

static void test_rejected_input(const char *directory, const char *name,
                                const uint8_t *bytes, size_t size,
                                int expected_status,
                                const char *diagnostic_fragment) {
    char input[512], output[512];
    snprintf(input, sizeof(input), "%s/%s", directory, name);
    snprintf(output, sizeof(output), "%s/%s.exe", directory, name);
    write_all(input, bytes, size);
    unlink(output);
    char *inputs[] = {input};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = 1U;
    options.inputs.capacity = 1U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_link(&options) == expected_status);
    assert(strstr(capture.message, diagnostic_fragment));
    assert(access(output, F_OK) != 0);
}

static void test_malformed_and_bitcode(const char *directory) {
    static const uint8_t truncated[] = {0x64, 0x86, 0x01, 0x00};
    test_rejected_input(directory, "truncated.obj", truncated,
                        sizeof(truncated), LD_INVALID_INPUT, "truncated");
    static const uint8_t bitcode[] = {'B', 'C', 0xc0, 0xde};
    test_rejected_input(directory, "lto.obj", bitcode, sizeof(bitcode),
                        LD_UNSUPPORTED, "-fno-lto");
    static const uint8_t wrapped_bitcode[] = {0xde, 0xc0, 0x17, 0x0b};
    test_rejected_input(directory, "wrapped-lto.obj", wrapped_bitcode,
                        sizeof(wrapped_bitcode), LD_UNSUPPORTED, "-fno-lto");

    size_t size;
    uint8_t *bad_relocation = build_entry_object(false, &size);
    const size_t text_raw = LD_COFF_HEADER_SIZE + LD_COFF_SECTION_HEADER_SIZE;
    const size_t text_reloc = text_raw + 15U;
    put32(bad_relocation, size, text_reloc + 4U, 99U);
    test_rejected_input(directory, "bad-reloc.obj", bad_relocation, size,
                        LD_INVALID_INPUT, "invalid symbol index");
    free(bad_relocation);

    uint8_t *bad_raw_data = build_entry_object(false, &size);
    put32(bad_raw_data, size, LD_COFF_HEADER_SIZE + 20U, UINT32_MAX);
    test_rejected_input(directory, "bad-raw-data.obj", bad_raw_data, size,
                        LD_INVALID_INPUT, "raw data is out of range");
    free(bad_raw_data);
}

static void check_relocation_diagnostic(const diagnostic_capture_t *capture,
                                        const char *object,
                                        const char *member) {
    char object_field[640];
    int length = snprintf(object_field, sizeof(object_field), "object='%s'",
                          object);
    assert(length > 0 && (size_t) length < sizeof(object_field));
    assert(strstr(capture->message, "relocation error:"));
    assert(strstr(capture->message, object_field));
    assert(strstr(capture->message, member));
    assert(strstr(capture->message, "section='.text'"));
    assert(strstr(capture->message, "offset=0x0000000a"));
    assert(strstr(capture->message, "symbol='ExitProcess'"));
    assert(strstr(capture->message, "type=UNKNOWN(0x7777)"));
    assert(strstr(capture->message, "unsupported AMD64 relocation"));
}

static void test_relocation_diagnostics(const char *directory) {
    char direct[512], archive[512], import[512], output[512];
    snprintf(direct, sizeof(direct), "%s/bad-direct.obj", directory);
    snprintf(archive, sizeof(archive), "%s/bad-archive.lib", directory);
    snprintf(import, sizeof(import), "%s/bad-kernel32.obj", directory);
    snprintf(output, sizeof(output), "%s/bad-relocation.exe", directory);

    size_t object_size, import_size;
    uint8_t *object_bytes = build_entry_object(false, &object_size);
    uint8_t *import_bytes = build_import_object(&import_size);
    const size_t text_raw = LD_COFF_HEADER_SIZE + LD_COFF_SECTION_HEADER_SIZE;
    const size_t text_reloc = text_raw + 15U;
    put16(object_bytes, object_size, text_reloc + 8U, 0x7777U);
    write_all(direct, object_bytes, object_size);
    write_archive_member(archive, "bad-member.obj", object_bytes,
                         object_size);
    write_all(import, import_bytes, import_size);
    free(object_bytes);
    free(import_bytes);

    diagnostic_capture_t capture = {{0}};
    assert(link_inputs(direct, import, output, NULL, &capture) ==
           LD_UNSUPPORTED);
    check_relocation_diagnostic(&capture, direct, "member='<direct>'");

    capture.message[0] = '\0';
    assert(link_inputs(archive, import, output, NULL, &capture) ==
           LD_UNSUPPORTED);
    check_relocation_diagnostic(&capture, archive,
                                "member='bad-member.obj'");
}

int main(void) {
    char directory[] = "/tmp/nature-ld-coff-XXXXXX";
    assert(mkdtemp(directory));
    test_atomic_output_replace(directory);
    test_minimal_import_and_determinism(directory);
    test_base_relocation(directory);
    test_object_definitions_override_earlier_import(directory);
    test_later_archive_does_not_override_weak_external(directory);
    test_long_debug_section_names(directory);
    test_legacy_ctor_and_stable_subsection_order(directory);
    test_relocation_apply_matrix(directory);
    test_comdat_association(directory);
    test_malformed_and_bitcode(directory);
    test_relocation_diagnostics(directory);
    return 0;
}
