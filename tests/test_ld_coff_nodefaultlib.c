#include "src/binary/coff/coff_writer.h"
#include "src/ld/coff_format.h"
#include "src/ld/ld.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * /DEFAULTLIB and /NODEFAULTLIB ordering follows the ordinary library queue
 * semantics in lld/COFF/Driver.cpp.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

typedef struct {
    char message[4096];
} diagnostic_capture_t;

static void diagnostic(void *context, ld_diag_level_t level,
                       const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static uint8_t *build_object(const char *definition,
                             const char *undefined_symbol,
                             const char *directives, size_t *size) {
    coff_object_t *object = coff_object_create("nodefaultlib.c");
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
                   object, definition, text, 0U, COFF_SYMBOL_TYPE_FUNCTION,
                   LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) == COFF_WRITER_OK);
    if (undefined_symbol) {
        assert(coff_object_get_or_add_symbol_reference(
                       object, undefined_symbol, true, 0U, NULL) ==
               COFF_WRITER_OK);
    }
    if (directives) {
        coff_section_t *section = NULL;
        assert(coff_object_add_section(
                       object, ".drectve",
                       LD_COFF_SCN_CNT_INITIALIZED_DATA |
                               LD_COFF_SCN_LNK_INFO |
                               LD_COFF_SCN_LNK_REMOVE,
                       1U, &section) == COFF_WRITER_OK);
        assert(coff_section_append(section, directives, strlen(directives),
                                   1U, NULL) == COFF_WRITER_OK);
    }
    uint8_t *bytes = NULL;
    assert(coff_object_serialize(object, &bytes, size) == COFF_WRITER_OK);
    coff_object_destroy(object);
    return bytes;
}

static void write_all(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

static void write_object(const char *path, const char *definition,
                         const char *undefined_symbol,
                         const char *directives) {
    size_t size;
    uint8_t *bytes =
            build_object(definition, undefined_symbol, directives, &size);
    write_all(path, bytes, size);
    free(bytes);
}

static void archive_header(uint8_t header[LD_COFF_ARCHIVE_HEADER_SIZE],
                           const char *name, size_t size) {
    memset(header, ' ', LD_COFF_ARCHIVE_HEADER_SIZE);
    size_t name_length = strlen(name);
    assert(name_length + 1U <= 16U);
    memcpy(header, name, name_length);
    header[name_length] = '/';
    memcpy(header + 16U, "0", 1U);
    memcpy(header + 28U, "0", 1U);
    memcpy(header + 34U, "0", 1U);
    memcpy(header + 40U, "644", 3U);
    char encoded[32];
    int length = snprintf(encoded, sizeof(encoded), "%zu", size);
    assert(length > 0 && length <= 10);
    memcpy(header + 48U, encoded, (size_t) length);
    header[58U] = '`';
    header[59U] = '\n';
}

static void write_archive(const char *path, const uint8_t *first,
                          size_t first_size, const uint8_t *second,
                          size_t second_size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(LD_COFF_ARCHIVE_MAGIC, 1U, LD_COFF_ARCHIVE_MAGIC_SIZE,
                  file) == LD_COFF_ARCHIVE_MAGIC_SIZE);
    uint8_t header[LD_COFF_ARCHIVE_HEADER_SIZE];
    archive_header(header, "trigger.obj", first_size);
    assert(fwrite(header, 1U, sizeof(header), file) == sizeof(header));
    assert(fwrite(first, 1U, first_size, file) == first_size);
    if (first_size & 1U) assert(fputc('\n', file) != EOF);
    archive_header(header, "suppress.obj", second_size);
    assert(fwrite(header, 1U, sizeof(header), file) == sizeof(header));
    assert(fwrite(second, 1U, second_size, file) == second_size);
    if (second_size & 1U) assert(fputc('\n', file) != EOF);
    assert(fclose(file) == 0);
}

static void link_success(const char *output, char **inputs, size_t input_count,
                         const char *library_directory) {
    char *library_paths[] = {(char *) library_directory};
    diagnostic_capture_t capture = {{0}};
    ld_options_t options = {0};
    options.os = LD_OS_WINDOWS;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output;
    options.entry_symbol = "entry";
    options.inputs.items = inputs;
    options.inputs.count = input_count;
    options.inputs.capacity = input_count;
    options.library_paths.items = library_paths;
    options.library_paths.count = 1U;
    options.library_paths.capacity = 1U;
    options.diagnostic = diagnostic;
    options.diagnostic_context = &capture;
    int status = ld_link(&options);
    if (status != LD_OK) fprintf(stderr, "%s\n", capture.message);
    assert(status == LD_OK);
}

static void test_direct_object_order(const char *directory) {
    char entry[512], suppress[512], first_output[512], second_output[512];
    snprintf(entry, sizeof(entry), "%s/direct-entry.obj", directory);
    snprintf(suppress, sizeof(suppress), "%s/direct-suppress.obj", directory);
    snprintf(first_output, sizeof(first_output), "%s/direct-first.exe",
             directory);
    snprintf(second_output, sizeof(second_output), "%s/direct-second.exe",
             directory);
    write_object(entry, "entry", NULL, "/DEFAULTLIB:never_load_me");
    write_object(suppress, "suppress_marker", NULL,
                 "/NODEFAULTLIB:NEVER_LOAD_ME.LIB");
    char *first_inputs[] = {entry, suppress};
    char *second_inputs[] = {suppress, entry};
    link_success(first_output, first_inputs, 2U, directory);
    link_success(second_output, second_inputs, 2U, directory);
}

static void test_directive_order(const char *directory) {
    char first[512], second[512], first_output[512], second_output[512];
    snprintf(first, sizeof(first), "%s/directives-first.obj", directory);
    snprintf(second, sizeof(second), "%s/directives-second.obj", directory);
    snprintf(first_output, sizeof(first_output), "%s/directives-first.exe",
             directory);
    snprintf(second_output, sizeof(second_output), "%s/directives-second.exe",
             directory);
    write_object(first, "entry", NULL,
                 "/DEFAULTLIB:never_load_me "
                 "/NODEFAULTLIB:NEVER_LOAD_ME.LIB");
    write_object(second, "entry", NULL,
                 "/NODEFAULTLIB:never_load_me.lib "
                 "/DEFAULTLIB:NEVER_LOAD_ME");
    char *first_inputs[] = {first};
    char *second_inputs[] = {second};
    link_success(first_output, first_inputs, 1U, directory);
    link_success(second_output, second_inputs, 1U, directory);
}

static void test_wildcard(const char *directory) {
    char object[512], output[512];
    snprintf(object, sizeof(object), "%s/wildcard.obj", directory);
    snprintf(output, sizeof(output), "%s/wildcard.exe", directory);
    write_object(object, "entry", NULL,
                 "/DEFAULTLIB:never_load_me /NODEFAULTLIB");
    char *inputs[] = {object};
    link_success(output, inputs, 1U, directory);
}

static void test_name_normalization(const char *directory) {
    char archive_style[512], path_style[512];
    char archive_output[512], path_output[512];
    snprintf(archive_style, sizeof(archive_style), "%s/archive-style.obj",
             directory);
    snprintf(path_style, sizeof(path_style), "%s/path-style.obj", directory);
    snprintf(archive_output, sizeof(archive_output), "%s/archive-style.exe",
             directory);
    snprintf(path_output, sizeof(path_output), "%s/path-style.exe", directory);
    write_object(archive_style, "entry", NULL,
                 "/DEFAULTLIB:never_load_me.a "
                 "/NODEFAULTLIB:NEVER_LOAD_ME");
    write_object(path_style, "entry", NULL,
                 "/DEFAULTLIB:never_load_me "
                 "/NODEFAULTLIB:C:\\SDK\\NEVER_LOAD_ME.LIB");
    char *archive_inputs[] = {archive_style};
    char *path_inputs[] = {path_style};
    link_success(archive_output, archive_inputs, 1U, directory);
    link_success(path_output, path_inputs, 1U, directory);
}

static void test_archive_member_discovery(const char *directory) {
    char entry[512], archive[512], reverse_archive[512];
    char output[512], reverse_output[512];
    snprintf(entry, sizeof(entry), "%s/archive-entry.obj", directory);
    snprintf(archive, sizeof(archive), "%s/directives.lib", directory);
    snprintf(reverse_archive, sizeof(reverse_archive),
             "%s/directives-reverse.lib", directory);
    snprintf(output, sizeof(output), "%s/archive.exe", directory);
    snprintf(reverse_output, sizeof(reverse_output), "%s/archive-reverse.exe",
             directory);
    write_object(entry, "entry", "trigger", NULL);

    size_t trigger_size, suppress_size;
    uint8_t *trigger = build_object(
            "trigger", "next",
            "/DEFAULTLIB:never_load_me", &trigger_size);
    uint8_t *suppress = build_object(
            "next", NULL, "/NODEFAULTLIB:NEVER_LOAD_ME.LIB",
            &suppress_size);
    write_archive(archive, trigger, trigger_size, suppress, suppress_size);
    write_archive(reverse_archive, suppress, suppress_size, trigger,
                  trigger_size);
    free(trigger);
    free(suppress);

    char *inputs[] = {entry, archive};
    char *reverse_inputs[] = {entry, reverse_archive};
    link_success(output, inputs, 2U, directory);
    link_success(reverse_output, reverse_inputs, 2U, directory);
}

int main(void) {
    char directory[] = "/tmp/nature-ld-coff-nodefaultlib-XXXXXX";
    assert(mkdtemp(directory));
    test_direct_object_order(directory);
    test_directive_order(directory);
    test_wildcard(directory);
    test_name_normalization(directory);
    test_archive_member_discovery(directory);
    return 0;
}
