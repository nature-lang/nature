/*
 * Standalone byte-level tests for the deterministic COFF capability scanner.
 *
 * Build from the repository root:
 *
 *   cc -std=c11 -Wall -Wextra -Werror -I. \
 *     -DLD_COFF_CAPABILITY_TEST_MAIN \
 *     -DLD_COFF_CAPABILITY_TEST_STANDALONE_STUBS \
 *     src/binary/coff/coff_writer.c \
 *     src/ld/ld_coff_reader.c src/ld/ld_coff.c \
 *     src/ld/ld_coff_input.c src/ld/ld_coff_writer.c \
 *     src/ld/ld_coff_capability.c src/ld/ld_coff_capability_test.c \
 *     -o /tmp/ld_coff_capability_test
 *
 * Source semantics:
 *   llvm/lld/COFF/InputFiles.cpp
 *   llvm/include/llvm/BinaryFormat/COFF.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifdef LD_COFF_CAPABILITY_TEST_MAIN

#include "ld_coff_capability.h"

#include "src/binary/coff/coff_writer.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld_output.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LD_COFF_CAPABILITY_CHECK(condition)                               \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "COFF capability test failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                      \
            return false;                                                 \
        }                                                                 \
    } while (0)

#ifdef LD_COFF_CAPABILITY_TEST_STANDALONE_STUBS
int ld_write_file_atomic(const ld_options_t *options, const char *path,
                         const uint8_t *data, size_t size, bool executable) {
    (void) options;
    (void) path;
    (void) data;
    (void) size;
    (void) executable;
    return LD_OUTPUT_ERROR;
}

int ld_write_output_atomic(const ld_options_t *options, const uint8_t *image,
                           size_t size) {
    (void) options;
    (void) image;
    (void) size;
    return LD_OUTPUT_ERROR;
}
#endif

static void ld_coff_capability_test_put_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void ld_coff_capability_test_put_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static uint32_t ld_coff_capability_test_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | (uint32_t) bytes[1] << 8U |
           (uint32_t) bytes[2] << 16U | (uint32_t) bytes[3] << 24U;
}

static bool ld_coff_capability_test_write(const char *path,
                                          const uint8_t *bytes,
                                          size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool success = fwrite(bytes, 1U, size, file) == size;
    if (fclose(file) != 0) success = false;
    return success;
}

static uint8_t *ld_coff_capability_test_read(const char *path, size_t *size) {
    *size = 0U;
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0L, SEEK_END) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = length ? malloc((size_t) length) : NULL;
    if (length &&
        (!bytes || fread(bytes, 1U, (size_t) length, file) !=
                           (size_t) length)) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    *size = (size_t) length;
    return bytes;
}

static bool ld_coff_capability_test_object(const char *path,
                                           const char *directives) {
    static const uint8_t pseudo_v2_marker[12] = {
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
    };
    coff_object_t *object = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(object != NULL);
    coff_section_t *text = coff_object_text(object);
    coff_section_t *rdata = coff_object_rdata(object);
    LD_COFF_CAPABILITY_CHECK(text && rdata);
    LD_COFF_CAPABILITY_CHECK(coff_section_append_zeros(text, 16U, 8U, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_append(
                                     rdata, pseudo_v2_marker,
                                     sizeof(pseudo_v2_marker), 4U, NULL) ==
                             COFF_WRITER_OK);

    coff_section_t *leader = NULL;
    coff_section_t *associative = NULL;
    coff_section_t *drectve = NULL;
    LD_COFF_CAPABILITY_CHECK(coff_object_add_section(
                                     object, ".text$cap",
                                     LD_COFF_SCN_CNT_CODE |
                                             LD_COFF_SCN_MEM_EXECUTE |
                                             LD_COFF_SCN_MEM_READ,
                                     1U, &leader) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_append_zeros(leader, 1U, 1U, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_section(
                                     object, ".xdata$cap",
                                     LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                             LD_COFF_SCN_MEM_READ,
                                     4U, &associative) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(
            coff_section_append_zeros(associative, 4U, 4U, NULL) ==
            COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_mark_comdat(
                                     object, leader, LD_COFF_COMDAT_ANY, NULL,
                                     "cap_leader", 0U, NULL, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_mark_comdat(
                                     object, associative,
                                     LD_COFF_COMDAT_ASSOCIATIVE, leader, NULL,
                                     0U, NULL, NULL) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_section(
                                     object, ".drectve",
                                     LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                             LD_COFF_SCN_LNK_INFO |
                                             LD_COFF_SCN_LNK_REMOVE,
                                     1U, &drectve) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_append(
                                     drectve, directives, strlen(directives),
                                     1U, NULL) == COFF_WRITER_OK);

    uint32_t a_missing = COFF_SYMBOL_INDEX_NONE;
    uint32_t exit_process = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_get_or_add_symbol_reference(
                                     object, "ExitProcess", true, 0U,
                                     &exit_process) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_get_or_add_symbol_reference(
                                     object, "a_missing", true, 0U,
                                     &a_missing) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_defined_symbol(
                                     object, ".refptr.target", rdata, 0U, 0U,
                                     LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_defined_symbol(
                                     object,
                                     "__RUNTIME_PSEUDO_RELOC_LIST__", rdata,
                                     0U, 0U, LD_COFF_STORAGE_CLASS_EXTERNAL,
                                     NULL) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_add_relocation_with_addend(
                                     text, 0U, exit_process,
                                     LD_COFF_REL_AMD64_REL32, 0) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_add_relocation_with_addend(
                                     text, 8U, a_missing,
                                     LD_COFF_REL_AMD64_ADDR64, 0) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_define_symbol(
                                     object, "a_missing", text, 0U, 0U,
                                     LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_write_file(object, path) ==
                             COFF_WRITER_OK);
    coff_object_destroy(object);
    return true;
}

static bool ld_coff_capability_test_weak_alias_provider(
        const char *hard_path, const char *weak_path) {
    coff_object_t *hard = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(hard != NULL);
    coff_section_t *hard_text = coff_object_text(hard);
    LD_COFF_CAPABILITY_CHECK(hard_text != NULL);
    LD_COFF_CAPABILITY_CHECK(
            coff_section_append_zeros(hard_text, 4U, 4U, NULL) ==
            COFF_WRITER_OK);
    uint32_t missing = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_get_or_add_symbol_reference(
                                     hard, "weak_masked_hard_symbol", true, 0U,
                                     &missing) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_add_relocation(
                                     hard_text, 0U, missing,
                                     LD_COFF_REL_AMD64_REL32) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_write_file(hard, hard_path) ==
                             COFF_WRITER_OK);
    coff_object_destroy(hard);

    coff_object_t *weak = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(weak != NULL);
    coff_section_t *weak_text = coff_object_text(weak);
    LD_COFF_CAPABILITY_CHECK(weak_text != NULL);
    static const uint8_t return_instruction = 0xc3U;
    LD_COFF_CAPABILITY_CHECK(coff_section_append(
                                     weak_text, &return_instruction, 1U, 1U,
                                     NULL) == COFF_WRITER_OK);
    uint32_t fallback = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_add_defined_symbol(
                                     weak, "weak_fallback", weak_text, 0U, 0U,
                                     LD_COFF_STORAGE_CLASS_EXTERNAL,
                                     &fallback) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     weak, "weak_masked_hard_symbol", fallback,
                                     LD_COFF_WEAK_SEARCH_ALIAS, NULL) ==
                             COFF_WRITER_OK);
    uint32_t alias_a = COFF_SYMBOL_INDEX_NONE;
    uint32_t alias_b = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     weak, "weak_chain_a", fallback,
                                     LD_COFF_WEAK_SEARCH_ALIAS, &alias_a) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     weak, "weak_chain_b", alias_a,
                                     LD_COFF_WEAK_SEARCH_ALIAS, &alias_b) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     weak, "weak_chain_c", alias_b,
                                     LD_COFF_WEAK_SEARCH_ALIAS, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_write_file(weak, weak_path) ==
                             COFF_WRITER_OK);
    coff_object_destroy(weak);
    return true;
}

static bool ld_coff_capability_test_weak_alias_cycle(const char *first_path,
                                                     const char *second_path) {
    coff_object_t *first = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(first != NULL);
    uint32_t second_symbol = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_get_or_add_symbol_reference(
                                     first, "weak_cycle_b", true, 0U,
                                     &second_symbol) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     first, "weak_cycle_a", second_symbol,
                                     LD_COFF_WEAK_SEARCH_ALIAS, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_write_file(first, first_path) ==
                             COFF_WRITER_OK);
    coff_object_destroy(first);

    coff_object_t *second = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(second != NULL);
    uint32_t first_symbol = COFF_SYMBOL_INDEX_NONE;
    LD_COFF_CAPABILITY_CHECK(coff_object_get_or_add_symbol_reference(
                                     second, "weak_cycle_a", true, 0U,
                                     &first_symbol) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_weak_external(
                                     second, "weak_cycle_b", first_symbol,
                                     LD_COFF_WEAK_SEARCH_ALIAS, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_write_file(second, second_path) ==
                             COFF_WRITER_OK);
    coff_object_destroy(second);
    return true;
}

static uint8_t *ld_coff_capability_test_short_import(uint16_t type_info,
                                                     size_t *size) {
    static const char symbol[] = "ExitProcess";
    static const char dll[] = "KERNEL32.dll";
    *size = LD_COFF_IMPORT_HEADER_SIZE + sizeof(symbol) + sizeof(dll);
    uint8_t *bytes = calloc(*size, 1U);
    if (!bytes) return NULL;
    ld_coff_capability_test_put_u16(bytes + 2U, 0xffffU);
    ld_coff_capability_test_put_u16(bytes + 6U, LD_COFF_MACHINE_AMD64);
    ld_coff_capability_test_put_u32(
            bytes + 12U, (uint32_t) (sizeof(symbol) + sizeof(dll)));
    ld_coff_capability_test_put_u16(bytes + 18U, type_info);
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE, symbol, sizeof(symbol));
    memcpy(bytes + LD_COFF_IMPORT_HEADER_SIZE + sizeof(symbol), dll,
           sizeof(dll));
    return bytes;
}

static void ld_coff_capability_test_archive_header(uint8_t header[60],
                                                   const char *name,
                                                   size_t size) {
    memset(header, ' ', 60U);
    size_t name_length = strlen(name);
    if (name_length > 16U) name_length = 16U;
    memcpy(header, name, name_length);
    char size_text[32];
    int length = snprintf(size_text, sizeof(size_text), "%zu", size);
    if (length > 0 && length <= 10)
        memcpy(header + 48U, size_text, (size_t) length);
    header[58] = '`';
    header[59] = '\n';
}

static bool ld_coff_capability_test_archive(const char *path) {
    uint8_t bigobj[LD_COFF_BIGOBJ_HEADER_SIZE] = {0};
    ld_coff_capability_test_put_u16(bigobj + 2U, 0xffffU);
    ld_coff_capability_test_put_u16(bigobj + 4U, 2U);
    ld_coff_capability_test_put_u16(bigobj + 6U, LD_COFF_MACHINE_AMD64);
    memcpy(bigobj + 12U, LD_COFF_BIGOBJ_MAGIC, 16U);
    size_t import_size = 0U;
    uint8_t *import = ld_coff_capability_test_short_import(
            LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U), &import_size);
    LD_COFF_CAPABILITY_CHECK(import != NULL);
    size_t big_member_size = LD_COFF_ARCHIVE_HEADER_SIZE + sizeof(bigobj);
    size_t import_member_size = LD_COFF_ARCHIVE_HEADER_SIZE + import_size;
    size_t total = LD_COFF_ARCHIVE_MAGIC_SIZE + big_member_size +
                   (big_member_size & 1U) + import_member_size +
                   (import_member_size & 1U);
    uint8_t *archive = calloc(total, 1U);
    LD_COFF_CAPABILITY_CHECK(archive != NULL);
    memcpy(archive, LD_COFF_ARCHIVE_MAGIC, LD_COFF_ARCHIVE_MAGIC_SIZE);
    size_t offset = LD_COFF_ARCHIVE_MAGIC_SIZE;
    ld_coff_capability_test_archive_header(archive + offset, "big.obj/",
                                           sizeof(bigobj));
    offset += LD_COFF_ARCHIVE_HEADER_SIZE;
    memcpy(archive + offset, bigobj, sizeof(bigobj));
    offset += sizeof(bigobj);
    if (offset & 1U) archive[offset++] = '\n';
    ld_coff_capability_test_archive_header(archive + offset, "imp.obj/",
                                           import_size);
    offset += LD_COFF_ARCHIVE_HEADER_SIZE;
    memcpy(archive + offset, import, import_size);
    offset += import_size;
    if (offset & 1U) archive[offset++] = '\n';
    LD_COFF_CAPABILITY_CHECK(offset == total);
    bool success = ld_coff_capability_test_write(path, archive, total);
    free(import);
    free(archive);
    return success;
}

static bool ld_coff_capability_test_bad_relocation(const char *valid_path,
                                                   const char *bad_path) {
    size_t size = 0U;
    uint8_t *bytes = ld_coff_capability_test_read(valid_path, &size);
    LD_COFF_CAPABILITY_CHECK(bytes != NULL && size >= 60U);
    uint32_t relocations = ld_coff_capability_test_u32(bytes + 20U + 24U);
    LD_COFF_CAPABILITY_CHECK((uint64_t) relocations +
                                     LD_COFF_RELOCATION_SIZE <=
                             size);
    ld_coff_capability_test_put_u16(bytes + relocations + 8U, 0x7777U);
    bool success = ld_coff_capability_test_write(bad_path, bytes, size);
    free(bytes);
    return success;
}

static bool ld_coff_capability_test_bad_comdat(const char *valid_path,
                                               const char *bad_path) {
    size_t size = 0U;
    uint8_t *bytes = ld_coff_capability_test_read(valid_path, &size);
    LD_COFF_CAPABILITY_CHECK(bytes != NULL && size >= LD_COFF_HEADER_SIZE);
    uint32_t symbols = ld_coff_capability_test_u32(bytes + 8U);
    uint64_t selection = (uint64_t) symbols + LD_COFF_SYMBOL_SIZE + 14U;
    LD_COFF_CAPABILITY_CHECK(selection < size &&
                             bytes[selection] == LD_COFF_COMDAT_ANY);
    bytes[selection] = 0x7fU;
    bool success = ld_coff_capability_test_write(bad_path, bytes, size);
    free(bytes);
    return success;
}

static bool ld_coff_capability_test_fat_lto(const char *path) {
    static const uint8_t bitcode[] = {'B', 'C', 0xc0, 0xde, 0U};
    coff_object_t *object = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(object != NULL);
    coff_section_t *section = NULL;
    LD_COFF_CAPABILITY_CHECK(coff_object_add_section(
                                     object, ".llvm.lto",
                                     LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                             LD_COFF_SCN_MEM_READ,
                                     1U, &section) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_section_append(
                                     section, bitcode, sizeof(bitcode), 1U,
                                     NULL) == COFF_WRITER_OK);
    bool success =
            coff_object_write_file(object, path) == COFF_WRITER_OK;
    coff_object_destroy(object);
    return success;
}

static bool ld_coff_capability_test_nonempty_pseudo_reloc(const char *path) {
    static const uint8_t pseudo_table[] = {
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
    };
    coff_object_t *object = coff_object_create_amd64(NULL);
    LD_COFF_CAPABILITY_CHECK(object != NULL);
    coff_section_t *rdata = coff_object_rdata(object);
    LD_COFF_CAPABILITY_CHECK(rdata != NULL);
    LD_COFF_CAPABILITY_CHECK(coff_section_append(
                                     rdata, pseudo_table,
                                     sizeof(pseudo_table), 4U, NULL) ==
                             COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_defined_symbol(
                                     object,
                                     "__RUNTIME_PSEUDO_RELOC_LIST__", rdata,
                                     0U, 0U, LD_COFF_STORAGE_CLASS_EXTERNAL,
                                     NULL) == COFF_WRITER_OK);
    LD_COFF_CAPABILITY_CHECK(coff_object_add_defined_symbol(
                                     object,
                                     "__RUNTIME_PSEUDO_RELOC_LIST_END__",
                                     rdata, (uint32_t) sizeof(pseudo_table), 0U,
                                     LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) ==
                             COFF_WRITER_OK);
    bool success =
            coff_object_write_file(object, path) == COFF_WRITER_OK;
    coff_object_destroy(object);
    return success;
}

static bool ld_coff_capability_test_manifest(void) {
    const char *object_path = "/tmp/nature-coff-capabilities.obj";
    const char *archive_path = "/tmp/nature-coff-capabilities.lib";
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_object(
            object_path,
            "/DEFAULTLIB:kernel32.lib /INCLUDE:cap_leader "
            "-exclude-symbols:cap"));
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_archive(archive_path));

    const char *first_inputs[] = {object_path, archive_path};
    const char *second_inputs[] = {archive_path, object_path};
    ld_coff_capability_manifest_t first = {0};
    ld_coff_capability_manifest_t second = {0};
    char error[512] = {0};
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_scan_json(
                                     first_inputs, 2U, &first, error,
                                     sizeof(error)) == LD_OK);
    LD_COFF_CAPABILITY_CHECK(error[0] == '\0' && first.json && first.size);
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_scan_json(
                                     second_inputs, 2U, &second, error,
                                     sizeof(error)) == LD_OK);
    LD_COFF_CAPABILITY_CHECK(first.size == second.size &&
                             memcmp(first.json, second.json, first.size) == 0);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json,
                   "\"schema\":\"nature.windows-sysroot-coff-capabilities.v2\",\n"
                   "  \"target\":\"windows/amd64\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json,
                   "\"llvm_reference_commit\":"
                   "\"c58ba1cf51d2886994da7e667a05c1bfe4f4396b\",\n"
                   "  \"bitcode_lto\":\"absent\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(strstr(first.json,
                                    "\"bitcode_lto\":\"absent\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"type\":\"archive\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"type\":\"bigobj\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"type\":\"short_import\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json,
                   "\"container\":\"direct\",\"type\":\"coff\"") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "{\"name\":\"ADDR64\",\"value\":1}") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "{\"name\":\"REL32\",\"value\":4}") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "{\"name\":\"ANY\",\"value\":2}") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(strstr(
                                     first.json,
                                     "{\"name\":\"ASSOCIATIVE\",\"value\":5}") !=
                             NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "/DEFAULTLIB:kernel32.lib") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "-exclude-symbols:cap") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"dll\":\"KERNEL32.dll\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"type\":\"code\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"name_type\":\"name\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json,
                   "\"undefined_symbols\":[\"ExitProcess\",\"cap_leader\"]") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"hard_unresolved_symbols\":[]") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"system_dlls\":[\"kernel32.dll\"]") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"refptr_symbols\":[\".refptr.target\"]") !=
            NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"pseudo_reloc_v2_marker\":true") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json,
                   "\"pseudo_reloc_support\":\"empty-only\"") != NULL);
    LD_COFF_CAPABILITY_CHECK(
            strstr(first.json, "\"pseudo_reloc_runtime_symbols\":true") !=
            NULL);
    ld_coff_capability_manifest_deinit(&first);
    ld_coff_capability_manifest_deinit(&second);
    return true;
}

static bool ld_coff_capability_test_rejections(void) {
    const char *valid_path = "/tmp/nature-coff-capabilities.obj";
    const char *bad_reloc = "/tmp/nature-coff-capabilities-bad-reloc.obj";
    const char *bad_comdat = "/tmp/nature-coff-capabilities-bad-comdat.obj";
    const char *bad_directive =
            "/tmp/nature-coff-capabilities-bad-directive.obj";
    const char *bitcode = "/tmp/nature-coff-capabilities.bc";
    const char *fat_lto = "/tmp/nature-coff-capabilities-fat-lto.obj";
    const char *ltcg = "/tmp/nature-coff-capabilities-ltcg.obj";
    const char *bad_import = "/tmp/nature-coff-capabilities-bad-import.obj";
    const char *third_party_import =
            "/tmp/nature-coff-capabilities-third-party-import.obj";
    const char *bad_bigobj = "/tmp/nature-coff-capabilities-bigobj-v3.obj";
    const char *hard_unresolved =
            "/tmp/nature-coff-capabilities-hard-unresolved.obj";
    const char *weak_masked_hard =
            "/tmp/nature-coff-capabilities-weak-masked-hard.obj";
    const char *weak_provider =
            "/tmp/nature-coff-capabilities-weak-provider.obj";
    const char *weak_cycle_a =
            "/tmp/nature-coff-capabilities-weak-cycle-a.obj";
    const char *weak_cycle_b =
            "/tmp/nature-coff-capabilities-weak-cycle-b.obj";
    const char *pseudo_reloc =
            "/tmp/nature-coff-capabilities-pseudo-reloc.obj";
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_bad_relocation(valid_path, bad_reloc));
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_bad_comdat(valid_path, bad_comdat));
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_object(bad_directive, "/EXPORT:bad"));
    static const uint8_t raw_bitcode[] = {'B', 'C', 0xc0, 0xde};
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_write(
            bitcode, raw_bitcode, sizeof(raw_bitcode)));
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_fat_lto(fat_lto));
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_nonempty_pseudo_reloc(pseudo_reloc));
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_object(
            hard_unresolved, "/DEFAULTLIB:kernel32.lib"));
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_weak_alias_provider(
            weak_masked_hard, weak_provider));
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_weak_alias_cycle(
            weak_cycle_a, weak_cycle_b));
    uint8_t clgl[LD_COFF_BIGOBJ_HEADER_SIZE] = {0};
    static const uint8_t clgl_magic[16] = {
            0x38,
            0xfe,
            0xb3,
            0x0c,
            0xa5,
            0xd9,
            0xab,
            0x4d,
            0xac,
            0x9b,
            0xd6,
            0xb6,
            0x22,
            0x26,
            0x53,
            0xc2,
    };
    ld_coff_capability_test_put_u16(clgl + 2U, 0xffffU);
    memcpy(clgl + 12U, clgl_magic, sizeof(clgl_magic));
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_write(ltcg, clgl, sizeof(clgl)));
    size_t import_size = 0U;
    uint8_t *import = ld_coff_capability_test_short_import(
            UINT16_C(0x20) | LD_COFF_IMPORT_CODE |
                    (LD_COFF_IMPORT_NAME << 2U),
            &import_size);
    LD_COFF_CAPABILITY_CHECK(import != NULL);
    LD_COFF_CAPABILITY_CHECK(
            ld_coff_capability_test_write(bad_import, import, import_size));
    static const char third_party_dll[] = "EVIL.dll";
    size_t dll_offset = LD_COFF_IMPORT_HEADER_SIZE + sizeof("ExitProcess");
    memset(import + dll_offset, 0, import_size - dll_offset);
    memcpy(import + dll_offset, third_party_dll, sizeof(third_party_dll));
    ld_coff_capability_test_put_u16(
            import + 18U,
            LD_COFF_IMPORT_CODE | (LD_COFF_IMPORT_NAME << 2U));
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_write(
            third_party_import, import, import_size));
    free(import);
    uint8_t bigobj[LD_COFF_BIGOBJ_HEADER_SIZE] = {0};
    ld_coff_capability_test_put_u16(bigobj + 2U, 0xffffU);
    ld_coff_capability_test_put_u16(bigobj + 4U, 3U);
    ld_coff_capability_test_put_u16(bigobj + 6U, LD_COFF_MACHINE_AMD64);
    memcpy(bigobj + 12U, LD_COFF_BIGOBJ_MAGIC, 16U);
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_test_write(
            bad_bigobj, bigobj, sizeof(bigobj)));

    struct {
        const char *path;
        const char *message;
    } cases[] = {
            {bad_reloc, "unsupported AMD64 relocation"},
            {bad_comdat, "unsupported or missing COMDAT selection"},
            {bad_directive, "unsupported COFF directive"},
            {bitcode, "LLVM bitcode/LTO"},
            {fat_lto, ".llvm.lto"},
            {ltcg, "MSVC LTCG"},
            {bad_import, "unsupported short import TypeInfo bits"},
            {third_party_import, "system DLL allowlist"},
            {bad_bigobj, "unsupported BigObj header version"},
            {pseudo_reloc, "non-empty MinGW pseudo-relocation table"},
            {hard_unresolved, "unresolved symbol 'ExitProcess'"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(*cases); i++) {
        const char *inputs[] = {cases[i].path};
        ld_coff_capability_manifest_t manifest = {0};
        char error[512] = {0};
        LD_COFF_CAPABILITY_CHECK(ld_coff_capability_scan_json(
                                         inputs, 1U, &manifest, error,
                                         sizeof(error)) != LD_OK);
        LD_COFF_CAPABILITY_CHECK(manifest.json == NULL && manifest.size == 0U);
        LD_COFF_CAPABILITY_CHECK(strstr(error, cases[i].message) != NULL);
        ld_coff_capability_manifest_deinit(&manifest);
    }
    const char *weak_inputs[] = {weak_masked_hard, weak_provider};
    ld_coff_capability_manifest_t weak_manifest = {0};
    char weak_error[512] = {0};
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_scan_json(
                                     weak_inputs, 2U, &weak_manifest, weak_error,
                                     sizeof(weak_error)) == LD_OK);
    LD_COFF_CAPABILITY_CHECK(weak_error[0] == '\0');
    LD_COFF_CAPABILITY_CHECK(
            strstr(weak_manifest.json, "\"weak_unresolved_symbols\":[]") !=
            NULL);
    ld_coff_capability_manifest_deinit(&weak_manifest);

    const char *cycle_inputs[] = {weak_cycle_a, weak_cycle_b};
    ld_coff_capability_manifest_t cycle_manifest = {0};
    char cycle_error[512] = {0};
    LD_COFF_CAPABILITY_CHECK(ld_coff_capability_scan_json(
                                     cycle_inputs, 2U, &cycle_manifest,
                                     cycle_error, sizeof(cycle_error)) !=
                             LD_OK);
    LD_COFF_CAPABILITY_CHECK(
            strstr(cycle_error, "unresolved symbol 'weak_cycle_") != NULL);
    ld_coff_capability_manifest_deinit(&cycle_manifest);
    return true;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        ld_coff_capability_manifest_t manifest = {0};
        char error[1024] = {0};
        int status = ld_coff_capability_scan_json(
                (const char *const *) (argv + 1), (size_t) (argc - 1),
                &manifest, error, sizeof(error));
        if (status != LD_OK) {
            fprintf(stderr, "%s\n", error[0] ? error : "capability scan failed");
            return status;
        }
        bool written = fwrite(manifest.json, 1U, manifest.size, stdout) ==
                       manifest.size;
        ld_coff_capability_manifest_deinit(&manifest);
        return written ? 0 : 1;
    }
    if (!ld_coff_capability_test_manifest()) return 1;
    if (!ld_coff_capability_test_rejections()) return 1;
    fprintf(stderr, "COFF capability scanner tests passed\n");
    return 0;
}

#endif
