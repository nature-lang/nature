/*
 * Byte-level tests for Nature's portable AMD64 COFF object writer.
 *
 * Wire expectations are derived from LLVM's WinCOFFObjectWriter and COFF
 * definitions.
 *
 * Sources:
 *   llvm/lib/MC/WinCOFFObjectWriter.cpp
 *   llvm/include/llvm/Object/COFF.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "coff_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COFF_TEST_CHECK(condition)                                    \
    do {                                                              \
        if (!(condition)) {                                           \
            fprintf(stderr, "COFF writer test failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                  \
            return false;                                             \
        }                                                             \
    } while (0)

typedef struct {
    const uint8_t *image;
    size_t image_size;
    uint32_t symbol_table_offset;
    uint32_t symbol_count;
    uint32_t string_table_offset;
    uint32_t string_table_size;
    uint16_t section_count;
} coff_test_image_t;

typedef struct {
    uint32_t raw_index;
    uint32_t file_offset;
    uint8_t aux_count;
} coff_test_symbol_t;

static uint16_t coff_test_u16(const uint8_t *bytes) {
    return (uint16_t) bytes[0] | (uint16_t) ((uint16_t) bytes[1] << 8U);
}

static uint32_t coff_test_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8U |
           (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

static uint64_t coff_test_u64(const uint8_t *bytes) {
    return (uint64_t) coff_test_u32(bytes) |
           (uint64_t) coff_test_u32(bytes + 4U) << 32U;
}

static bool coff_test_range(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static bool coff_test_parse_image(const uint8_t *image, size_t image_size,
                                  coff_test_image_t *result) {
    if (!image || !result || !coff_test_range(image_size, 0U, LD_COFF_HEADER_SIZE))
        return false;
    memset(result, 0, sizeof(*result));
    result->image = image;
    result->image_size = image_size;
    result->section_count = coff_test_u16(image + 2U);
    result->symbol_table_offset = coff_test_u32(image + 8U);
    result->symbol_count = coff_test_u32(image + 12U);
    const uint64_t string_offset =
            (uint64_t) result->symbol_table_offset +
            (uint64_t) result->symbol_count * LD_COFF_SYMBOL_SIZE;
    if (string_offset > SIZE_MAX ||
        !coff_test_range(image_size, (size_t) string_offset, 4U))
        return false;
    result->string_table_offset = (uint32_t) string_offset;
    result->string_table_size =
            coff_test_u32(image + result->string_table_offset);
    return result->string_table_size >= 4U &&
           coff_test_range(image_size, result->string_table_offset,
                           result->string_table_size);
}

static bool coff_test_string(const coff_test_image_t *image, uint32_t offset,
                             const char **result) {
    if (!image || !result || offset < 4U ||
        offset >= image->string_table_size)
        return false;
    const char *value = (const char *) image->image +
                        image->string_table_offset + offset;
    const size_t remaining = image->string_table_size - offset;
    if (!memchr(value, '\0', remaining)) return false;
    *result = value;
    return true;
}

static bool coff_test_short_name(const uint8_t raw[LD_COFF_NAME_SIZE],
                                 char name[LD_COFF_NAME_SIZE + 1U]) {
    memcpy(name, raw, LD_COFF_NAME_SIZE);
    name[LD_COFF_NAME_SIZE] = '\0';
    return true;
}

static bool coff_test_section_name(const coff_test_image_t *image,
                                   const uint8_t *header,
                                   const char **long_name,
                                   char short_name[LD_COFF_NAME_SIZE + 1U]) {
    if (header[0] != '/') {
        *long_name = NULL;
        return coff_test_short_name(header, short_name);
    }
    char encoded[LD_COFF_NAME_SIZE + 1U];
    coff_test_short_name(header, encoded);
    char *end = NULL;
    unsigned long parsed = strtoul(encoded + 1U, &end, 10);
    if (end == encoded + 1U || (*end != '\0' && *end != ' ') ||
        parsed > UINT32_MAX)
        return false;
    short_name[0] = '\0';
    return coff_test_string(image, (uint32_t) parsed, long_name);
}

static bool coff_test_find_section(const coff_test_image_t *image,
                                   const char *name, uint16_t *section_index,
                                   uint32_t *header_offset) {
    if (!image || !name) return false;
    for (uint16_t i = 0U; i < image->section_count; i++) {
        const uint32_t offset = LD_COFF_HEADER_SIZE +
                                (uint32_t) i * LD_COFF_SECTION_HEADER_SIZE;
        if (!coff_test_range(image->image_size, offset,
                             LD_COFF_SECTION_HEADER_SIZE))
            return false;
        const char *long_name = NULL;
        char short_name[LD_COFF_NAME_SIZE + 1U];
        if (!coff_test_section_name(image, image->image + offset, &long_name,
                                    short_name))
            return false;
        const char *actual = long_name ? long_name : short_name;
        if (strcmp(actual, name) == 0) {
            if (section_index) *section_index = (uint16_t) (i + 1U);
            if (header_offset) *header_offset = offset;
            return true;
        }
    }
    return false;
}

static bool coff_test_symbol_name(const coff_test_image_t *image,
                                  const uint8_t *record,
                                  const char **long_name,
                                  char short_name[LD_COFF_NAME_SIZE + 1U]) {
    if (coff_test_u32(record) != 0U) {
        *long_name = NULL;
        return coff_test_short_name(record, short_name);
    }
    short_name[0] = '\0';
    return coff_test_string(image, coff_test_u32(record + 4U), long_name);
}

static bool coff_test_find_symbol(const coff_test_image_t *image,
                                  const char *name,
                                  coff_test_symbol_t *result) {
    if (!image || !name || !result) return false;
    uint32_t raw_index = 0U;
    while (raw_index < image->symbol_count) {
        const uint64_t offset64 =
                (uint64_t) image->symbol_table_offset +
                (uint64_t) raw_index * LD_COFF_SYMBOL_SIZE;
        if (offset64 > UINT32_MAX ||
            !coff_test_range(image->image_size, (size_t) offset64,
                             LD_COFF_SYMBOL_SIZE))
            return false;
        const uint8_t *record = image->image + (size_t) offset64;
        const char *long_name = NULL;
        char short_name[LD_COFF_NAME_SIZE + 1U];
        if (!coff_test_symbol_name(image, record, &long_name, short_name))
            return false;
        const char *actual = long_name ? long_name : short_name;
        const uint8_t aux_count = record[17];
        if ((uint64_t) raw_index + 1U + aux_count > image->symbol_count)
            return false;
        if (strcmp(actual, name) == 0) {
            result->raw_index = raw_index;
            result->file_offset = (uint32_t) offset64;
            result->aux_count = aux_count;
            return true;
        }
        raw_index += 1U + aux_count;
    }
    return false;
}

static bool coff_test_write_linkable_object(void) {
    static const uint8_t code[] = {
            0x48,
            0x83,
            0xec,
            0x28, /* sub rsp, 40 */
            0xb9,
            0x2a,
            0x00,
            0x00,
            0x00, /* mov ecx, 42 */
            0xe8,
            0x00,
            0x00,
            0x00,
            0x00, /* call ExitProcess */
            0xcc,
    };
    coff_object_t *object = coff_object_create_amd64(NULL);
    COFF_TEST_CHECK(object != NULL);
    coff_section_t *text = coff_object_text(object);
    uint32_t code_offset = UINT32_MAX;
    COFF_TEST_CHECK(coff_section_append(text, code, sizeof(code), 1U,
                                        &code_offset) == COFF_WRITER_OK);
    COFF_TEST_CHECK(code_offset == 0U);
    uint32_t entry_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t exit_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_add_defined_symbol(
                            object, "entry", text, 0U,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            LD_COFF_STORAGE_CLASS_EXTERNAL,
                            &entry_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(entry_index != COFF_SYMBOL_INDEX_NONE);
    COFF_TEST_CHECK(coff_object_get_or_add_symbol_reference(
                            object, "ExitProcess", true,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            &exit_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            text, 10U, exit_index, LD_COFF_REL_AMD64_REL32,
                            0) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_write_file(
                            object, "/tmp/nature-coff-writer-entry.obj") ==
                    COFF_WRITER_OK);
    coff_object_destroy(object);
    return true;
}

static bool coff_test_forward_symbol(void) {
    coff_object_t *object = coff_object_create_amd64(NULL);
    COFF_TEST_CHECK(object != NULL);
    coff_section_t *text = coff_object_text(object);
    uint32_t offset = UINT32_MAX;
    COFF_TEST_CHECK(coff_section_append_zeros(text, 4U, 1U, &offset) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 0U);
    uint32_t symbol_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_get_or_add_symbol_reference(
                            object, "local.forward", false,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            &symbol_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation(
                            text, 0U, symbol_index,
                            LD_COFF_REL_AMD64_REL32) == COFF_WRITER_OK);
    uint8_t *image = NULL;
    size_t image_size = 0U;
    COFF_TEST_CHECK(coff_object_serialize(object, &image, &image_size) ==
                    COFF_WRITER_INVALID_STATE);
    COFF_TEST_CHECK(image == NULL && image_size == 0U);
    uint32_t defined_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_define_symbol(
                            object, "local.forward", text, 0U,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            LD_COFF_STORAGE_CLASS_STATIC,
                            &defined_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(defined_index == symbol_index);
    COFF_TEST_CHECK(coff_object_serialize(object, &image, &image_size) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(image != NULL && image_size != 0U);
    free(image);
    coff_object_destroy(object);
    return true;
}

static bool coff_test_full_object(void) {
    static const uint8_t call[] = {0xe8, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t rip_load[] = {0xf2, 0x0f, 0x10, 0x05,
                                       0x00, 0x00, 0x00, 0x00};
    static const uint8_t rip_store_imm[] = {0x40, 0xc6, 0x05, 0x00,
                                            0x00, 0x00, 0x00, 0x18};
    static const uint8_t comdat_bytes[] = {0xaa, 0xbb, 0xcc};
    static const uint8_t associative_bytes[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t raw_aux[LD_COFF_SYMBOL_SIZE] = {
            0x11,
            0x22,
            0x33,
            0x44,
            0x55,
            0x66,
            0x77,
            0x88,
            0x99,
            0xaa,
            0xbb,
            0xcc,
            0xdd,
            0xee,
            0xf0,
            0x12,
            0x34,
            0x56,
    };

    coff_object_t *object =
            coff_object_create_amd64("test/very-long-source-file-name.n");
    COFF_TEST_CHECK(object != NULL);
    coff_section_t *text = coff_object_text(object);
    coff_section_t *rdata = coff_object_rdata(object);
    coff_section_t *data = coff_object_data(object);
    coff_section_t *bss = coff_object_bss(object);
    COFF_TEST_CHECK(text && rdata && data && bss);

    uint32_t offset = UINT32_MAX;
    COFF_TEST_CHECK(coff_section_append(text, call, sizeof(call), 1U,
                                        &offset) == COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 0U);
    COFF_TEST_CHECK(coff_section_append(text, rip_load, sizeof(rip_load), 1U,
                                        &offset) == COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 5U);
    COFF_TEST_CHECK(coff_section_append(text, rip_store_imm,
                                        sizeof(rip_store_imm), 1U,
                                        &offset) == COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 13U);
    COFF_TEST_CHECK(coff_section_append_zeros(rdata, 4U, 4U, &offset) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 0U);
    COFF_TEST_CHECK(coff_section_append_zeros(data, 16U, 8U, &offset) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 0U);
    COFF_TEST_CHECK(coff_section_append_zeros(bss, 23U, 16U, &offset) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(offset == 0U && coff_section_size(bss) == 23U);
    COFF_TEST_CHECK(coff_section_alignment(bss) == 16U);
    const uint8_t nonzero = 1U;
    COFF_TEST_CHECK(coff_section_append(bss, &nonzero, 1U, 1U, NULL) ==
                    COFF_WRITER_INVALID_ARGUMENT);
    COFF_TEST_CHECK(coff_section_write(bss, 0U, &nonzero, 1U) ==
                    COFF_WRITER_INVALID_ARGUMENT);

    coff_section_t *comdat = NULL;
    coff_section_t *associative = NULL;
    COFF_TEST_CHECK(coff_object_add_section(
                            object, ".verylongsection$name",
                            LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                    LD_COFF_SCN_MEM_READ,
                            4U, &comdat) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_append(comdat, comdat_bytes,
                                        sizeof(comdat_bytes), 1U,
                                        NULL) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_section(
                            object, ".xdata$verylong",
                            LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                    LD_COFF_SCN_MEM_READ |
                                    LD_COFF_SCN_MEM_DISCARDABLE,
                            4U, &associative) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_append(associative, associative_bytes,
                                        sizeof(associative_bytes), 1U,
                                        NULL) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_section(
                            object, ".data", LD_COFF_SCN_CNT_INITIALIZED_DATA,
                            1U, NULL) == COFF_WRITER_DUPLICATE);

    uint32_t entry_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t long_function_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t long_data_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t fallback_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t exit_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t weak_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_add_defined_symbol(
                            object, "entry", text, 0U,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            LD_COFF_STORAGE_CLASS_EXTERNAL,
                            &entry_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_defined_symbol(
                            object, "nature.really_long_function_name", text,
                            0U, COFF_SYMBOL_TYPE_FUNCTION,
                            LD_COFF_STORAGE_CLASS_EXTERNAL,
                            &long_function_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_defined_symbol(
                            object, "nature.really_long_global_data", data,
                            8U, 0U, LD_COFF_STORAGE_CLASS_EXTERNAL,
                            &long_data_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_defined_symbol(
                            object, "fallback", data, 12U, 0U,
                            LD_COFF_STORAGE_CLASS_EXTERNAL,
                            &fallback_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_get_or_add_symbol_reference(
                            object, "ExitProcess", true,
                            COFF_SYMBOL_TYPE_FUNCTION,
                            &exit_index) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_add_weak_external(
                            object, "weak_alias", fallback_index,
                            LD_COFF_WEAK_SEARCH_ALIAS,
                            &weak_index) == COFF_WRITER_OK);

    coff_symbol_desc_t raw_symbol = {
            .name = "raw_aux",
            .value = 7U,
            .section_number = LD_COFF_SYM_ABSOLUTE,
            .type = 0U,
            .storage_class = LD_COFF_STORAGE_CLASS_STATIC,
            .aux_records = raw_aux,
            .aux_count = 1U,
    };
    uint32_t raw_symbol_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_add_symbol(object, &raw_symbol,
                                           &raw_symbol_index) ==
                    COFF_WRITER_OK);

    uint32_t comdat_symbol_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t comdat_leader_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_mark_comdat(
                            object, comdat, LD_COFF_COMDAT_ANY, NULL,
                            "comdat_leader", UINT32_C(0x12345678),
                            &comdat_symbol_index,
                            &comdat_leader_index) == COFF_WRITER_OK);
    uint32_t associative_symbol_index = COFF_SYMBOL_INDEX_NONE;
    COFF_TEST_CHECK(coff_object_mark_comdat(
                            object, associative,
                            LD_COFF_COMDAT_ASSOCIATIVE, comdat, NULL, 0U,
                            &associative_symbol_index,
                            NULL) == COFF_WRITER_OK);
    COFF_TEST_CHECK(comdat_symbol_index != COFF_SYMBOL_INDEX_NONE &&
                    comdat_leader_index != COFF_SYMBOL_INDEX_NONE &&
                    associative_symbol_index != COFF_SYMBOL_INDEX_NONE);

    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            text, 1U, exit_index, LD_COFF_REL_AMD64_REL32,
                            0) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            text, 9U, long_data_index,
                            LD_COFF_REL_AMD64_REL32,
                            -4) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            text, 16U, long_data_index,
                            LD_COFF_REL_AMD64_REL32_1,
                            7) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            data, 0U, long_function_index,
                            LD_COFF_REL_AMD64_ADDR64,
                            INT64_C(0x1122334455667788)) == COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_section_add_relocation_with_addend(
                            rdata, 0U, entry_index,
                            LD_COFF_REL_AMD64_REL32,
                            INT64_C(0x100000000)) == COFF_WRITER_OVERFLOW);
    COFF_TEST_CHECK(coff_section_relocation_count(rdata) == 0U);
    COFF_TEST_CHECK(coff_section_add_relocation(
                            rdata, 0U, 1U,
                            LD_COFF_REL_AMD64_REL32) ==
                    COFF_WRITER_INVALID_ARGUMENT);

    uint8_t *first = NULL;
    uint8_t *second = NULL;
    size_t first_size = 0U;
    size_t second_size = 0U;
    COFF_TEST_CHECK(coff_object_serialize(object, &first, &first_size) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(coff_object_serialize(object, &second, &second_size) ==
                    COFF_WRITER_OK);
    COFF_TEST_CHECK(first_size == second_size &&
                    memcmp(first, second, first_size) == 0);

    coff_test_image_t image;
    COFF_TEST_CHECK(coff_test_parse_image(first, first_size, &image));
    COFF_TEST_CHECK(coff_test_u16(first) == LD_COFF_MACHINE_AMD64);
    COFF_TEST_CHECK(image.section_count == 6U);
    COFF_TEST_CHECK(coff_test_u32(first + 4U) == 0U);
    COFF_TEST_CHECK(image.symbol_count ==
                    coff_object_symbol_record_count(object));

    uint32_t text_header = 0U;
    uint32_t data_header = 0U;
    uint32_t bss_header = 0U;
    uint32_t comdat_header = 0U;
    uint16_t comdat_section_index = 0U;
    uint16_t associative_section_index = 0U;
    COFF_TEST_CHECK(coff_test_find_section(&image, ".text", NULL,
                                           &text_header));
    COFF_TEST_CHECK(coff_test_find_section(&image, ".data", NULL,
                                           &data_header));
    COFF_TEST_CHECK(coff_test_find_section(&image, ".bss", NULL,
                                           &bss_header));
    COFF_TEST_CHECK(coff_test_find_section(
            &image, ".verylongsection$name", &comdat_section_index,
            &comdat_header));
    COFF_TEST_CHECK(coff_test_find_section(
            &image, ".xdata$verylong", &associative_section_index, NULL));
    COFF_TEST_CHECK(comdat_section_index == 5U &&
                    associative_section_index == 6U);
    COFF_TEST_CHECK(first[comdat_header] == '/' &&
                    first[comdat_header + 1U] == '4');
    COFF_TEST_CHECK(coff_test_u32(first + bss_header + 16U) == 23U);
    COFF_TEST_CHECK(coff_test_u32(first + bss_header + 20U) == 0U);
    COFF_TEST_CHECK((coff_test_u32(first + bss_header + 36U) &
                     LD_COFF_SCN_ALIGN_MASK) == UINT32_C(0x00500000));
    COFF_TEST_CHECK(coff_test_u32(first + text_header + 16U) == 21U);
    COFF_TEST_CHECK(coff_test_u16(first + text_header + 32U) == 3U);
    COFF_TEST_CHECK(coff_test_u16(first + data_header + 32U) == 1U);
    COFF_TEST_CHECK((coff_test_u32(first + comdat_header + 36U) &
                     LD_COFF_SCN_LNK_COMDAT) != 0U);

    const uint32_t text_raw = coff_test_u32(first + text_header + 20U);
    const uint32_t text_relocations =
            coff_test_u32(first + text_header + 24U);
    const uint32_t data_raw = coff_test_u32(first + data_header + 20U);
    COFF_TEST_CHECK(coff_test_range(first_size, text_raw, 21U));
    COFF_TEST_CHECK(coff_test_range(first_size, text_relocations, 30U));
    COFF_TEST_CHECK(coff_test_range(first_size, data_raw, 16U));
    COFF_TEST_CHECK(coff_test_u32(first + text_raw + 1U) == 0U);
    COFF_TEST_CHECK(coff_test_u32(first + text_raw + 9U) == UINT32_MAX - 3U);
    COFF_TEST_CHECK(coff_test_u32(first + text_raw + 16U) == 7U);
    COFF_TEST_CHECK(coff_test_u64(first + data_raw) ==
                    UINT64_C(0x1122334455667788));
    COFF_TEST_CHECK(coff_test_u32(first + text_relocations) == 1U);
    COFF_TEST_CHECK(coff_test_u32(first + text_relocations + 4U) ==
                    exit_index);
    COFF_TEST_CHECK(coff_test_u16(first + text_relocations + 8U) ==
                    LD_COFF_REL_AMD64_REL32);
    COFF_TEST_CHECK(coff_test_u32(first + text_relocations + 10U) == 9U);
    COFF_TEST_CHECK(coff_test_u32(first + text_relocations + 14U) ==
                    long_data_index);
    COFF_TEST_CHECK(coff_test_u16(first + text_relocations + 18U) ==
                    LD_COFF_REL_AMD64_REL32);
    COFF_TEST_CHECK(coff_test_u32(first + text_relocations + 20U) == 16U);
    COFF_TEST_CHECK(coff_test_u16(first + text_relocations + 28U) ==
                    LD_COFF_REL_AMD64_REL32_1);

    coff_test_symbol_t file_symbol;
    coff_test_symbol_t long_function_symbol;
    coff_test_symbol_t raw_aux_symbol;
    coff_test_symbol_t weak_symbol;
    coff_test_symbol_t comdat_symbol;
    coff_test_symbol_t associative_symbol;
    COFF_TEST_CHECK(coff_test_find_symbol(&image, ".file", &file_symbol));
    COFF_TEST_CHECK(file_symbol.raw_index == 0U && file_symbol.aux_count == 2U);
    COFF_TEST_CHECK(coff_test_find_symbol(
            &image, "nature.really_long_function_name",
            &long_function_symbol));
    COFF_TEST_CHECK(coff_test_u32(first + long_function_symbol.file_offset) ==
                    0U);
    COFF_TEST_CHECK(coff_test_find_symbol(&image, "raw_aux",
                                          &raw_aux_symbol));
    COFF_TEST_CHECK(raw_aux_symbol.raw_index == raw_symbol_index &&
                    raw_aux_symbol.aux_count == 1U);
    COFF_TEST_CHECK(memcmp(first + raw_aux_symbol.file_offset +
                                   LD_COFF_SYMBOL_SIZE,
                           raw_aux, sizeof(raw_aux)) == 0);
    COFF_TEST_CHECK(coff_test_find_symbol(&image, "weak_alias",
                                          &weak_symbol));
    COFF_TEST_CHECK(weak_symbol.raw_index == weak_index &&
                    weak_symbol.aux_count == 1U);
    COFF_TEST_CHECK(coff_test_u32(first + weak_symbol.file_offset +
                                  LD_COFF_SYMBOL_SIZE) == fallback_index);
    COFF_TEST_CHECK(coff_test_u32(first + weak_symbol.file_offset +
                                  LD_COFF_SYMBOL_SIZE + 4U) ==
                    LD_COFF_WEAK_SEARCH_ALIAS);
    COFF_TEST_CHECK(coff_test_find_symbol(
            &image, ".verylongsection$name", &comdat_symbol));
    const uint8_t *comdat_aux = first + comdat_symbol.file_offset +
                                LD_COFF_SYMBOL_SIZE;
    COFF_TEST_CHECK(comdat_symbol.raw_index == comdat_symbol_index &&
                    comdat_symbol.aux_count == 1U);
    COFF_TEST_CHECK(coff_test_u32(comdat_aux) == sizeof(comdat_bytes));
    COFF_TEST_CHECK(coff_test_u32(comdat_aux + 8U) == UINT32_C(0x12345678));
    COFF_TEST_CHECK(coff_test_u16(comdat_aux + 12U) == 0U);
    COFF_TEST_CHECK(comdat_aux[14] == LD_COFF_COMDAT_ANY);
    COFF_TEST_CHECK(coff_test_find_symbol(
            &image, ".xdata$verylong", &associative_symbol));
    const uint8_t *associative_aux = first + associative_symbol.file_offset +
                                     LD_COFF_SYMBOL_SIZE;
    COFF_TEST_CHECK(associative_symbol.raw_index ==
                    associative_symbol_index);
    COFF_TEST_CHECK(coff_test_u32(associative_aux) ==
                    sizeof(associative_bytes));
    COFF_TEST_CHECK(coff_test_u16(associative_aux + 12U) ==
                    comdat_section_index);
    COFF_TEST_CHECK(associative_aux[14] == LD_COFF_COMDAT_ASSOCIATIVE);

    COFF_TEST_CHECK(coff_object_write_file(
                            object, "/tmp/nature-coff-writer-test.obj") ==
                    COFF_WRITER_OK);
    FILE *written = fopen("/tmp/nature-coff-writer-test.obj", "rb");
    COFF_TEST_CHECK(written != NULL);
    COFF_TEST_CHECK(fseek(written, 0L, SEEK_END) == 0);
    COFF_TEST_CHECK(ftell(written) == (long) first_size);
    COFF_TEST_CHECK(fclose(written) == 0);

    free(first);
    free(second);
    coff_object_destroy(object);
    return true;
}

int coff_writer_run_tests(void) {
    if (!coff_test_forward_symbol()) return 1;
    if (!coff_test_full_object()) return 1;
    if (!coff_test_write_linkable_object()) return 1;
    return 0;
}

#ifdef COFF_WRITER_TEST_MAIN
int main(void) {
    const int result = coff_writer_run_tests();
    if (result == 0)
        fprintf(stderr, "COFF writer byte-level tests passed\n");
    return result;
}
#endif
