#include "src/binary/coff/coff_writer.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_coff_reader.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Import-directory range fixtures corresponding to lld/COFF/Writer.cpp
 * addSyntheticIdata() and locateImportTables().
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

typedef struct {
    char message[4096];
} diagnostic_capture_t;

typedef struct {
    uint32_t rva;
    uint32_t raw_offset;
    uint32_t virtual_size;
} pe_section_t;

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

static uint16_t read16(ld_coff_view_t view, uint64_t offset) {
    uint16_t value;
    assert(ld_coff_read_u16(view, offset, &value));
    return value;
}

static uint32_t read32(ld_coff_view_t view, uint64_t offset) {
    uint32_t value;
    assert(ld_coff_read_u32(view, offset, &value));
    return value;
}

static uint64_t read64(ld_coff_view_t view, uint64_t offset) {
    uint64_t value;
    assert(ld_coff_read_u64(view, offset, &value));
    return value;
}

static void write_all(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

static uint8_t *read_all(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t) length);
    assert(bytes);
    assert(fread(bytes, 1U, (size_t) length, file) == (size_t) length);
    assert(fclose(file) == 0);
    *size = (size_t) length;
    return bytes;
}

static uint64_t pe_optional_offset(ld_coff_view_t view) {
    uint32_t pe = read32(view, 0x3cU);
    assert(pe == LD_PE_OFFSET);
    assert(ld_coff_range_ok(view, pe, 4U));
    assert(memcmp(view.bytes + pe, "PE\0\0", 4U) == 0);
    return (uint64_t) pe + 4U + LD_PE_COFF_HEADER_SIZE;
}

static uint32_t pe_directory_rva(ld_coff_view_t view, unsigned index) {
    return read32(view, pe_optional_offset(view) + 112U + index * 8U);
}

static uint32_t pe_directory_size(ld_coff_view_t view, unsigned index) {
    return read32(view, pe_optional_offset(view) + 116U + index * 8U);
}

static bool pe_find_section(ld_coff_view_t view, const char *name,
                            pe_section_t *result) {
    uint32_t pe = read32(view, 0x3cU);
    uint16_t count = read16(view, pe + 6U);
    uint64_t headers = pe + 4U + LD_PE_COFF_HEADER_SIZE +
                       LD_PE_OPTIONAL_HEADER64_SIZE;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t offset = headers + i * LD_PE_SECTION_HEADER_SIZE;
        assert(ld_coff_range_ok(view, offset, LD_PE_SECTION_HEADER_SIZE));
        char actual[9] = {0};
        memcpy(actual, view.bytes + offset, 8U);
        if (strcmp(actual, name) != 0) continue;
        result->virtual_size = read32(view, offset + 8U);
        result->rva = read32(view, offset + 12U);
        result->raw_offset = read32(view, offset + 20U);
        return true;
    }
    return false;
}

static uint64_t pe_rva_to_file(const pe_section_t *section, uint32_t rva) {
    assert(rva >= section->rva);
    uint32_t offset = rva - section->rva;
    assert(offset < section->virtual_size);
    return (uint64_t) section->raw_offset + offset;
}

static void add_bytes(coff_object_t *object, const char *name,
                      const void *bytes, size_t size, uint32_t alignment) {
    coff_section_t *section = NULL;
    uint32_t characteristics = LD_COFF_SCN_CNT_INITIALIZED_DATA |
                               LD_COFF_SCN_MEM_READ |
                               LD_COFF_SCN_MEM_WRITE;
    assert(coff_object_add_section(object, name, characteristics, alignment,
                                   &section) == COFF_WRITER_OK);
    assert(coff_section_append(section, bytes, size, alignment, NULL) ==
           COFF_WRITER_OK);
}

static void make_entry_object(const char *path, bool with_gnu_idata) {
    coff_object_t *object = coff_object_create("import-directories.c");
    assert(object);
    coff_section_t *text = NULL;
    assert(coff_object_add_section(
                   object, ".text",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &text) == COFF_WRITER_OK);
    static const uint8_t ret[] = {0xc3};
    assert(coff_section_append(text, ret, sizeof(ret), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "entry", text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);

    if (with_gnu_idata) {
        uint8_t prefix[12];
        uint8_t descriptor[20];
        uint8_t terminator[20] = {0};
        uint8_t lookup[8];
        uint8_t address[16];
        memset(prefix, 0xa1, sizeof(prefix));
        memset(descriptor, 0xd2, sizeof(descriptor));
        memset(lookup, 0xa4, sizeof(lookup));
        memset(address, 0xa5, sizeof(address));
        add_bytes(object, ".idata$1", prefix, sizeof(prefix), 1U);
        add_bytes(object, ".idata$2", descriptor, sizeof(descriptor), 4U);
        add_bytes(object, ".idata$3", terminator, sizeof(terminator), 4U);
        add_bytes(object, ".idata$4", lookup, sizeof(lookup), 8U);
        add_bytes(object, ".idata$5", address, sizeof(address), 8U);
    }

    assert(coff_object_write_file(object, path) == COFF_WRITER_OK);
    coff_object_destroy(object);
}

static void make_named_short_import(const char *path, const char *symbol,
                                    const char *dll) {
    size_t symbol_size = strlen(symbol) + 1U;
    size_t dll_size = strlen(dll) + 1U;
    size_t size = LD_COFF_IMPORT_HEADER_SIZE + symbol_size + dll_size;
    uint8_t *bytes = calloc(1, size);
    assert(bytes);
    put16(bytes, size, 2U, 0xffffU);
    put16(bytes, size, 6U, LD_COFF_MACHINE_AMD64);
    put32(bytes, size, 12U, (uint32_t) (symbol_size + dll_size));
    put16(bytes, size, 18U,
          LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE, symbol, symbol_size);
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE + symbol_size, dll, dll_size);
    write_all(path, bytes, size);
    free(bytes);
}

static void make_ordinal_short_import(const char *path, const char *symbol,
                                      const char *dll, uint16_t ordinal) {
    size_t symbol_size = strlen(symbol) + 1U;
    size_t dll_size = strlen(dll) + 1U;
    size_t size = LD_COFF_IMPORT_HEADER_SIZE + symbol_size + dll_size;
    uint8_t *bytes = calloc(1, size);
    assert(bytes);
    put16(bytes, size, 2U, 0xffffU);
    put16(bytes, size, 6U, LD_COFF_MACHINE_AMD64);
    put32(bytes, size, 12U, (uint32_t) (symbol_size + dll_size));
    put16(bytes, size, 16U, ordinal);
    put16(bytes, size, 18U,
          LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_ORDINAL << 2U));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE, symbol, symbol_size);
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE + symbol_size, dll, dll_size);
    write_all(path, bytes, size);
    free(bytes);
}

static void make_short_import(const char *path) {
    make_named_short_import(path, "ShortOnly", "SHORT.dll");
}

static ld_coff_view_t link_fixture(const char *object, const char *import,
                                   const char *output, uint8_t **image) {
    char *inputs[2] = {(char *) object, (char *) import};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = import ? 2U : 1U;
    options.inputs.capacity = options.inputs.count;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);
    size_t image_size;
    *image = read_all(output, &image_size);
    return (ld_coff_view_t) {*image, image_size};
}

static void assert_gnu_prefix(ld_coff_view_t view, const pe_section_t *idata,
                              uint32_t directory_rva) {
    assert(directory_rva == idata->rva + 12U);
    uint64_t offset = pe_rva_to_file(idata, directory_rva);
    for (size_t i = 0; i < 20U; i++) assert(view.bytes[offset + i] == 0xd2U);
}

static void test_pure_short(const char *directory) {
    char object[512], import[512], output[512];
    snprintf(object, sizeof(object), "%s/short-entry.obj", directory);
    snprintf(import, sizeof(import), "%s/short.obj", directory);
    snprintf(output, sizeof(output), "%s/short.exe", directory);
    make_entry_object(object, false);
    make_short_import(import);

    uint8_t *image;
    ld_coff_view_t view = link_fixture(object, import, output, &image);
    pe_section_t idata;
    assert(pe_find_section(view, ".idata", &idata));
    assert(pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT) == idata.rva);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IMPORT) == 40U);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IAT) == 16U);
    uint64_t descriptor = pe_rva_to_file(
            &idata, pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT));
    assert(read32(view, descriptor) != 0U);
    assert(read32(view, descriptor + 12U) != 0U);
    assert(read32(view, descriptor + 16U) != 0U);
    for (size_t i = 20U; i < 40U; i++)
        assert(view.bytes[descriptor + i] == 0U);
    free(image);
}

static void test_ordinal_short_import(const char *directory) {
    char object[512], import[512], output[512];
    snprintf(object, sizeof(object), "%s/ordinal-entry.obj", directory);
    snprintf(import, sizeof(import), "%s/ordinal.obj", directory);
    snprintf(output, sizeof(output), "%s/ordinal.exe", directory);
    make_entry_object(object, false);
    make_ordinal_short_import(import, "OrdinalOnly", "KERNEL32.dll", 77U);

    uint8_t *image;
    ld_coff_view_t view = link_fixture(object, import, output, &image);
    pe_section_t idata;
    assert(pe_find_section(view, ".idata", &idata));
    uint64_t descriptor = pe_rva_to_file(
            &idata, pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT));
    uint32_t ilt_rva = read32(view, descriptor);
    uint32_t iat_rva = read32(view, descriptor + 16U);
    uint64_t expected = UINT64_C(0x8000000000000000) | 77U;
    assert(read64(view, pe_rva_to_file(&idata, ilt_rva)) == expected);
    assert(read64(view, pe_rva_to_file(&idata, iat_rva)) == expected);
    free(image);
}

static void test_pure_gnu(const char *directory) {
    char object[512], output[512];
    snprintf(object, sizeof(object), "%s/gnu-entry.obj", directory);
    snprintf(output, sizeof(output), "%s/gnu.exe", directory);
    make_entry_object(object, true);

    uint8_t *image;
    ld_coff_view_t view = link_fixture(object, NULL, output, &image);
    pe_section_t idata;
    assert(pe_find_section(view, ".idata", &idata));
    uint32_t import_rva = pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT);
    assert_gnu_prefix(view, &idata, import_rva);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IMPORT) == 20U);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IAT) == 16U);
    uint64_t iat = pe_rva_to_file(
            &idata, pe_directory_rva(view, LD_PE_DIRECTORY_IAT));
    for (size_t i = 0; i < 16U; i++) assert(view.bytes[iat + i] == 0xa5U);
    free(image);
}

static void test_mixed(const char *directory) {
    char object[512], import[512], output[512];
    snprintf(object, sizeof(object), "%s/mixed-entry.obj", directory);
    snprintf(import, sizeof(import), "%s/mixed-short.obj", directory);
    snprintf(output, sizeof(output), "%s/mixed.exe", directory);
    make_entry_object(object, true);
    make_short_import(import);

    uint8_t *image;
    ld_coff_view_t view = link_fixture(object, import, output, &image);
    pe_section_t idata;
    assert(pe_find_section(view, ".idata", &idata));
    uint32_t import_rva = pe_directory_rva(view, LD_PE_DIRECTORY_IMPORT);
    assert_gnu_prefix(view, &idata, import_rva);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IMPORT) == 60U);
    uint64_t descriptors = pe_rva_to_file(&idata, import_rva);
    assert(read32(view, descriptors + 20U) != 0U);
    assert(read32(view, descriptors + 32U) != 0U);
    assert(read32(view, descriptors + 36U) != 0U);
    for (size_t i = 40U; i < 60U; i++)
        assert(view.bytes[descriptors + i] == 0U);

    assert(pe_directory_size(view, LD_PE_DIRECTORY_IAT) == 32U);
    uint64_t iat = pe_rva_to_file(
            &idata, pe_directory_rva(view, LD_PE_DIRECTORY_IAT));
    for (size_t i = 0; i < 16U; i++) assert(view.bytes[iat + i] == 0xa5U);
    assert(read64(view, iat + 16U) != 0U);
    assert(read64(view, iat + 24U) == 0U);
    free(image);
}

static void test_duplicate_short_import(const char *directory) {
    char object[512], first[512], second[512], output[512];
    snprintf(object, sizeof(object), "%s/duplicate-entry.obj", directory);
    snprintf(first, sizeof(first), "%s/duplicate-first.obj", directory);
    snprintf(second, sizeof(second), "%s/duplicate-second.obj", directory);
    snprintf(output, sizeof(output), "%s/duplicate.exe", directory);
    make_entry_object(object, false);
    make_short_import(first);
    make_short_import(second);

    char *inputs[] = {object, first, second};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = sizeof(inputs) / sizeof(*inputs);
    options.inputs.capacity = options.inputs.count;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);

    size_t image_size = 0U;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IMPORT) == 40U);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IAT) == 16U);
    free(image);
}

static void test_duplicate_short_import_across_dlls(const char *directory) {
    char object[512], first[512], second[512], output[512];
    snprintf(object, sizeof(object), "%s/duplicate-dll-entry.obj", directory);
    snprintf(first, sizeof(first), "%s/duplicate-ucrt.obj", directory);
    snprintf(second, sizeof(second), "%s/duplicate-apiset.obj", directory);
    snprintf(output, sizeof(output), "%s/duplicate-dll.exe", directory);
    make_entry_object(object, false);
    make_named_short_import(first, "ShortOnly", "ucrtbase.dll");
    make_named_short_import(second, "ShortOnly",
                            "api-ms-win-crt-time-l1-1-0.dll");

    char *inputs[] = {object, first, second};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = sizeof(inputs) / sizeof(*inputs);
    options.inputs.capacity = options.inputs.count;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);

    size_t image_size = 0U;
    uint8_t *image = read_all(output, &image_size);
    ld_coff_view_t view = {image, image_size};
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IMPORT) == 40U);
    assert(pe_directory_size(view, LD_PE_DIRECTORY_IAT) == 16U);
    free(image);
}

int main(void) {
    char directory[] = "/tmp/nature-ld-coff-import-directories-XXXXXX";
    assert(mkdtemp(directory));
    test_pure_short(directory);
    test_ordinal_short_import(directory);
    test_pure_gnu(directory);
    test_mixed(directory);
    test_duplicate_short_import(directory);
    test_duplicate_short_import_across_dlls(directory);
    return 0;
}
