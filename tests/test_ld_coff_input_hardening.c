#include "src/ld/coff_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_coff_internal.h"
#include "src/ld/ld_coff_reader.h"

#include <assert.h>
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

static void write_all(int descriptor, const uint8_t *bytes, size_t size) {
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = write(descriptor, bytes + offset, size - offset);
        assert(count > 0);
        offset += (size_t) count;
    }
}

static int load_bytes(const uint8_t *bytes, size_t size,
                      diagnostic_capture_t *capture) {
    char path[] = "/tmp/nature-coff-input-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    write_all(descriptor, bytes, size);
    assert(close(descriptor) == 0);

    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = diagnostic;
    options.diagnostic_context = capture;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);
    int status = ld_coff_load_input(&context, path);
    ld_coff_context_deinit(&context);
    assert(unlink(path) == 0);
    return status;
}

static void expect_status(const uint8_t *bytes, size_t size, int expected,
                          const char *message) {
    diagnostic_capture_t capture = {{0}};
    assert(load_bytes(bytes, size, &capture) == expected);
    if (message)
        assert(strstr(capture.message, message) != NULL);
    else
        assert(capture.message[0] == '\0');
}

static void build_bigobj(uint8_t bytes[LD_COFF_BIGOBJ_HEADER_SIZE],
                         uint16_t version) {
    memset(bytes, 0, LD_COFF_BIGOBJ_HEADER_SIZE);
    put16(bytes, LD_COFF_BIGOBJ_HEADER_SIZE, 2U, 0xffffU);
    put16(bytes, LD_COFF_BIGOBJ_HEADER_SIZE, 4U, version);
    put16(bytes, LD_COFF_BIGOBJ_HEADER_SIZE, 6U, LD_COFF_MACHINE_AMD64);
    memcpy(bytes + 12U, LD_COFF_BIGOBJ_MAGIC, 16U);
}

static size_t build_short_import(uint8_t *bytes, size_t capacity,
                                 uint16_t version, uint16_t type_info) {
    static const char symbol[] = "ExitProcess";
    static const char dll[] = "KERNEL32.dll";
    size_t data_size = sizeof(symbol) + sizeof(dll);
    size_t size = LD_COFF_IMPORT_HEADER_SIZE + data_size;
    assert(size <= capacity);
    memset(bytes, 0, size);
    put16(bytes, size, 2U, 0xffffU);
    put16(bytes, size, 4U, version);
    put16(bytes, size, 6U, LD_COFF_MACHINE_AMD64);
    put32(bytes, size, 12U, (uint32_t) data_size);
    put16(bytes, size, 18U, type_info);
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE, symbol, sizeof(symbol));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE + sizeof(symbol), dll,
           sizeof(dll));
    return size;
}

static size_t build_llvm_lto_object(uint8_t *bytes, size_t capacity) {
    static const char name[] = ".llvm.lto";
    const size_t string_table = LD_COFF_HEADER_SIZE +
                                LD_COFF_SECTION_HEADER_SIZE;
    const size_t string_size = 4U + sizeof(name);
    const size_t size = string_table + string_size;
    assert(size <= capacity);
    memset(bytes, 0, size);
    put16(bytes, size, 0U, LD_COFF_MACHINE_AMD64);
    put16(bytes, size, 2U, 1U);
    put32(bytes, size, 8U, (uint32_t) string_table);
    memcpy(bytes + LD_COFF_HEADER_SIZE, "/4", 2U);
    put32(bytes, size, LD_COFF_HEADER_SIZE + 36U,
          LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ);
    put32(bytes, size, string_table, (uint32_t) string_size);
    memcpy(bytes + string_table + 4U, name, sizeof(name));
    return size;
}

static void test_bigobj_version(void) {
    uint8_t bytes[LD_COFF_BIGOBJ_HEADER_SIZE];
    build_bigobj(bytes, 2U);
    expect_status(bytes, sizeof(bytes), LD_OK, NULL);
    build_bigobj(bytes, 3U);
    expect_status(bytes, sizeof(bytes), LD_INVALID_INPUT,
                  "BigObj header version 3; expected version 2");
}

static void test_bigobj_sections_symbols_aux_and_relocation(void) {
    const size_t section_table = LD_COFF_BIGOBJ_HEADER_SIZE;
    const size_t raw_data = section_table + LD_COFF_SECTION_HEADER_SIZE;
    const size_t relocations = raw_data + 4U;
    const size_t symbols = relocations + LD_COFF_RELOCATION_SIZE;
    const size_t string_table =
            symbols + 3U * LD_COFF_BIGOBJ_SYMBOL_SIZE;
    const size_t size = string_table + 16U;
    uint8_t *bytes = calloc(1, size);
    assert(bytes);
    build_bigobj(bytes, 2U);
    put32(bytes, size, 44U, 1U);
    put32(bytes, size, 48U, (uint32_t) symbols);
    put32(bytes, size, 52U, 3U);

    memcpy(bytes + section_table, ".text", 5U);
    put32(bytes, size, section_table + 8U, 4U);
    put32(bytes, size, section_table + 16U, 4U);
    put32(bytes, size, section_table + 20U, (uint32_t) raw_data);
    put32(bytes, size, section_table + 24U, (uint32_t) relocations);
    put16(bytes, size, section_table + 32U, 1U);
    put32(bytes, size, section_table + 36U,
          LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                  LD_COFF_SCN_MEM_READ);
    bytes[raw_data] = 0xc3U;
    put32(bytes, size, relocations, 0U);
    put32(bytes, size, relocations + 4U, 2U);
    put16(bytes, size, relocations + 8U, LD_COFF_REL_AMD64_REL32);

    memcpy(bytes + symbols, "entry", 5U);
    put32(bytes, size, symbols + 12U, 1U);
    put16(bytes, size, symbols + 16U, 0x20U);
    bytes[symbols + 18U] = LD_COFF_STORAGE_CLASS_EXTERNAL;
    bytes[symbols + 19U] = 1U;
    size_t target = symbols + 2U * LD_COFF_BIGOBJ_SYMBOL_SIZE;
    put32(bytes, size, target + 4U, 4U);
    bytes[target + 18U] = LD_COFF_STORAGE_CLASS_EXTERNAL;
    put32(bytes, size, string_table, 16U);
    memcpy(bytes + string_table + 4U, "ExitProcess", 12U);

    char path[] = "/tmp/nature-coff-bigobj-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    write_all(descriptor, bytes, size);
    assert(close(descriptor) == 0);
    free(bytes);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    ld_coff_context_t context;
    ld_coff_context_init(&context, &options);
    assert(ld_coff_load_input(&context, path) == LD_OK);
    assert(context.object_count == 1U && context.objects[0]->bigobj);
    assert(context.objects[0]->section_count == 1U);
    assert(context.objects[0]->sections[0].relocation_count == 1U);
    assert(context.objects[0]->sections[0].relocations[0].symbol_index == 2U);
    assert(context.objects[0]->symbols[0].aux_count == 1U);
    assert(context.objects[0]->symbols[1].auxiliary);
    assert(strcmp(context.objects[0]->symbols[2].name, "ExitProcess") == 0);
    ld_coff_context_deinit(&context);
    assert(unlink(path) == 0);
}

static void test_short_import_header(void) {
    uint8_t bytes[128];
    size_t size = build_short_import(
            bytes, sizeof(bytes), 0U,
            LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U));
    expect_status(bytes, size, LD_OK, NULL);

    size = build_short_import(
            bytes, sizeof(bytes), 1U,
            LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U));
    expect_status(bytes, size, LD_INVALID_INPUT,
                  "short import version 1; expected version 0");

    size = build_short_import(
            bytes, sizeof(bytes), 0U,
            LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U) | 0x20U);
    expect_status(bytes, size, LD_INVALID_INPUT,
                  "short import TypeInfo bits");
}

static void test_anonymous_object_is_not_import(void) {
    uint8_t bytes[LD_COFF_BIGOBJ_HEADER_SIZE] = {0};
    put16(bytes, sizeof(bytes), 2U, 0xffffU);
    put16(bytes, sizeof(bytes), 4U, 1U);
    put16(bytes, sizeof(bytes), 6U, LD_COFF_MACHINE_AMD64);
    memset(bytes + 12U, 0xa5, 16U);
    diagnostic_capture_t capture = {{0}};
    assert(load_bytes(bytes, sizeof(bytes), &capture) == LD_UNSUPPORTED);
    assert(strstr(capture.message, "unsupported anonymous COFF object"));
    assert(strstr(capture.message, "short import object") == NULL);
}

static void test_lto_rejection(void) {
    uint8_t bytes[128];
    size_t size = build_llvm_lto_object(bytes, sizeof(bytes));
    expect_status(bytes, size, LD_UNSUPPORTED, ".llvm.lto");
    diagnostic_capture_t capture = {{0}};
    assert(load_bytes(bytes, size, &capture) == LD_UNSUPPORTED);
    assert(strstr(capture.message, "-fno-lto"));

    memset(bytes, 0, LD_COFF_BIGOBJ_HEADER_SIZE);
    put16(bytes, sizeof(bytes), 2U, 0xffffU);
    put16(bytes, sizeof(bytes), 4U, 2U);
    put16(bytes, sizeof(bytes), 6U, LD_COFF_MACHINE_AMD64);
    memcpy(bytes + 12U, LD_COFF_CL_GL_MAGIC, 16U);
    capture.message[0] = '\0';
    assert(load_bytes(bytes, LD_COFF_BIGOBJ_HEADER_SIZE, &capture) ==
           LD_UNSUPPORTED);
    assert(strstr(capture.message, "MSVC LTCG"));
    assert(strstr(capture.message, "/GL-"));
}

int main(void) {
    test_bigobj_version();
    test_bigobj_sections_symbols_aux_and_relocation();
    test_short_import_header();
    test_anonymous_object_is_not_import();
    test_lto_rejection();
    return 0;
}
