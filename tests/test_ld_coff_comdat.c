#include "src/binary/coff/coff_writer.h"
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

static void write_all(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

static void build_comdat_object(const char *path, const char *leader,
                                uint8_t selection, const uint8_t *data,
                                size_t data_size, uint32_t checksum,
                                const char *companion,
                                uint32_t companion_value,
                                bool associative, uint32_t child_marker) {
    coff_object_t *object = coff_object_create(path);
    assert(object);
    coff_section_t *section = NULL;
    coff_section_t *child = NULL;
    assert(coff_object_add_section(
                   object, ".text$comdat",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &section) == COFF_WRITER_OK);
    assert(coff_section_append(section, data, data_size, 1U, NULL) ==
           COFF_WRITER_OK);
    if (associative) {
        assert(coff_object_add_section(
                       object, ".rdata$assoc",
                       LD_COFF_SCN_CNT_INITIALIZED_DATA |
                               LD_COFF_SCN_MEM_READ,
                       4U, &child) == COFF_WRITER_OK);
        assert(coff_section_append(child, &child_marker,
                                   sizeof(child_marker), 4U, NULL) ==
               COFF_WRITER_OK);
    }

    uint32_t section_symbol = COFF_SYMBOL_INDEX_NONE;
    uint8_t emitted_selection =
            selection == LD_COFF_COMDAT_NEWEST ? LD_COFF_COMDAT_ANY
                                                : selection;
    assert(coff_object_mark_comdat(
                   object, section, emitted_selection, NULL, leader, checksum,
                   &section_symbol, NULL) == COFF_WRITER_OK);
    if (companion) {
        assert(companion_value <= data_size);
        assert(coff_object_add_defined_symbol(
                       object, companion, section, companion_value, 0U,
                       LD_COFF_STORAGE_CLASS_EXTERNAL, NULL) ==
               COFF_WRITER_OK);
    }
    if (associative) {
        assert(coff_object_mark_comdat(
                       object, child, LD_COFF_COMDAT_ASSOCIATIVE, section,
                       NULL, 0U, NULL, NULL) == COFF_WRITER_OK);
    }

    uint8_t *bytes = NULL;
    size_t size = 0U;
    assert(coff_object_serialize(object, &bytes, &size) == COFF_WRITER_OK);
    if (selection == LD_COFF_COMDAT_NEWEST) {
        ld_coff_view_t view = {bytes, size};
        uint32_t symbol_table = 0U;
        assert(ld_coff_read_u32(view, 8U, &symbol_table));
        size_t selection_offset = symbol_table +
                                  ((size_t) section_symbol + 1U) *
                                          LD_COFF_SYMBOL_SIZE +
                                  14U;
        assert(selection_offset < size);
        bytes[selection_offset] = LD_COFF_COMDAT_NEWEST;
    }
    write_all(path, bytes, size);
    free(bytes);
    coff_object_destroy(object);
}

static void build_associative_family(const char *path, const char *leader,
                                     uint32_t marker) {
    coff_object_t *object = coff_object_create(path);
    assert(object);
    coff_section_t *text = NULL;
    coff_section_t *pdata = NULL;
    coff_section_t *xdata = NULL;
    coff_section_t *debug_info = NULL;
    const uint32_t read_only = LD_COFF_SCN_CNT_INITIALIZED_DATA |
                               LD_COFF_SCN_MEM_READ;
    assert(coff_object_add_section(
                   object, ".text$family",
                   LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                           LD_COFF_SCN_MEM_READ,
                   16U, &text) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".pdata$family", read_only, 4U,
                                   &pdata) == COFF_WRITER_OK);
    assert(coff_object_add_section(object, ".xdata$family", read_only, 4U,
                                   &xdata) == COFF_WRITER_OK);
    assert(coff_object_add_section(
                   object, ".debug_info$family",
                   read_only | LD_COFF_SCN_MEM_DISCARDABLE, 1U,
                   &debug_info) == COFF_WRITER_OK);

    static const uint8_t code[] = {0xc3};
    uint8_t unwind[] = {1U, 0U, 0U, 0U};
    assert(coff_section_append(text, code, sizeof(code), 1U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append_zeros(pdata, 12U, 4U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(xdata, unwind, sizeof(unwind), 4U, NULL) ==
           COFF_WRITER_OK);
    assert(coff_section_append(debug_info, &marker, sizeof(marker), 1U,
                               NULL) == COFF_WRITER_OK);

    uint32_t leader_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t end_index = COFF_SYMBOL_INDEX_NONE;
    uint32_t unwind_index = COFF_SYMBOL_INDEX_NONE;
    assert(coff_object_mark_comdat(object, text, LD_COFF_COMDAT_ANY, NULL,
                                   leader, 0U, NULL, &leader_index) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "family_end", text, sizeof(code), 0U,
                   LD_COFF_STORAGE_CLASS_STATIC, &end_index) ==
           COFF_WRITER_OK);
    assert(coff_object_define_symbol(
                   object, "family_unwind", xdata, 0U, 0U,
                   LD_COFF_STORAGE_CLASS_STATIC, &unwind_index) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   pdata, 0U, leader_index, LD_COFF_REL_AMD64_ADDR32NB,
                   0) == COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   pdata, 4U, end_index, LD_COFF_REL_AMD64_ADDR32NB, 0) ==
           COFF_WRITER_OK);
    assert(coff_section_add_relocation_with_addend(
                   pdata, 8U, unwind_index, LD_COFF_REL_AMD64_ADDR32NB,
                   0) == COFF_WRITER_OK);
    assert(coff_object_mark_comdat(object, pdata,
                                   LD_COFF_COMDAT_ASSOCIATIVE, text, NULL,
                                   0U, NULL, NULL) == COFF_WRITER_OK);
    assert(coff_object_mark_comdat(object, xdata,
                                   LD_COFF_COMDAT_ASSOCIATIVE, text, NULL,
                                   0U, NULL, NULL) == COFF_WRITER_OK);
    assert(coff_object_mark_comdat(object, debug_info,
                                   LD_COFF_COMDAT_ASSOCIATIVE, text, NULL,
                                   0U, NULL, NULL) == COFF_WRITER_OK);

    uint8_t *bytes = NULL;
    size_t size = 0U;
    assert(coff_object_serialize(object, &bytes, &size) == COFF_WRITER_OK);
    write_all(path, bytes, size);
    free(bytes);
    coff_object_destroy(object);
}

static void context_init(ld_coff_context_t *context, ld_options_t *options,
                         diagnostic_capture_t *capture,
                         const char *entry_symbol) {
    memset(options, 0, sizeof(*options));
    options->os = LD_OS_WINDOWS;
    options->arch = LD_ARCH_AMD64;
    options->entry_symbol = entry_symbol;
    options->diagnostic = diagnostic;
    options->diagnostic_context = capture;
    ld_coff_context_init(context, options);
}

static ld_coff_global_t *defined_global(ld_coff_context_t *context,
                                        const char *name) {
    ld_coff_global_t *global = ld_coff_get_global(context, name, false);
    assert(global && global->kind == LD_COFF_GLOBAL_DEFINED);
    return global;
}

static ld_coff_output_section_t *output_section(ld_coff_context_t *context,
                                                const char *name) {
    for (size_t i = 0U; i < context->output_count; i++) {
        if (strcmp(context->outputs[i]->name, name) == 0)
            return context->outputs[i];
    }
    return NULL;
}

static void test_noduplicates(const char *directory) {
    char first[1024], second[1024];
    snprintf(first, sizeof(first), "%s/nodup-first.obj", directory);
    snprintf(second, sizeof(second), "%s/nodup-second.obj", directory);
    const uint8_t data[] = {0xc3};
    build_comdat_object(first, "nodup_key", LD_COFF_COMDAT_NODUPLICATES,
                        data, sizeof(data), 0U, NULL, 0U, false, 0U);
    build_comdat_object(second, "nodup_key", LD_COFF_COMDAT_NODUPLICATES,
                        data, sizeof(data), 0U, NULL, 0U, false, 0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "nodup_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, second) == LD_SYMBOL_ERROR);
    assert(strstr(capture.message, "NODUPLICATES"));
    ld_coff_context_deinit(&context);
}

static void test_any(const char *directory) {
    char first[1024], second[1024];
    snprintf(first, sizeof(first), "%s/any-first.obj", directory);
    snprintf(second, sizeof(second), "%s/any-second.obj", directory);
    const uint8_t first_data[] = {0xc3, 0x11};
    const uint8_t second_data[] = {0xc3, 0x22, 0x33};
    build_comdat_object(first, "any_key", LD_COFF_COMDAT_ANY, first_data,
                        sizeof(first_data), 1U, "any_companion", 1U, false,
                        0U);
    build_comdat_object(second, "any_key", LD_COFF_COMDAT_ANY, second_data,
                        sizeof(second_data), 2U, "any_companion", 2U, false,
                        0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "any_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, second) == LD_OK);
    assert(!context.objects[0]->sections[0].discarded);
    assert(context.objects[1]->sections[0].discarded);
    assert(defined_global(&context, "any_key")->section ==
           &context.objects[0]->sections[0]);
    ld_coff_global_t *companion =
            defined_global(&context, "any_companion");
    assert(companion->section == &context.objects[0]->sections[0]);
    assert(companion->value == 1U);
    ld_coff_context_deinit(&context);
}

static void test_same_size(const char *directory) {
    char first[1024], same[1024], different[1024];
    snprintf(first, sizeof(first), "%s/same-size-first.obj", directory);
    snprintf(same, sizeof(same), "%s/same-size-same.obj", directory);
    snprintf(different, sizeof(different), "%s/same-size-different.obj",
             directory);
    const uint8_t first_data[] = {0xc3, 0x11};
    const uint8_t same_data[] = {0xc3, 0x22};
    const uint8_t different_data[] = {0xc3, 0x33, 0x44};
    build_comdat_object(first, "same_size_key", LD_COFF_COMDAT_SAME_SIZE,
                        first_data, sizeof(first_data), 0U, NULL, 0U, false,
                        0U);
    build_comdat_object(same, "same_size_key", LD_COFF_COMDAT_SAME_SIZE,
                        same_data, sizeof(same_data), 0U, NULL, 0U, false,
                        0U);
    build_comdat_object(different, "same_size_key",
                        LD_COFF_COMDAT_SAME_SIZE, different_data,
                        sizeof(different_data), 0U, NULL, 0U, false, 0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "same_size_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, same) == LD_OK);
    assert(context.objects[1]->sections[0].discarded);
    ld_coff_context_deinit(&context);

    memset(&capture, 0, sizeof(capture));
    context_init(&context, &options, &capture, "same_size_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, different) == LD_SYMBOL_ERROR);
    assert(strstr(capture.message, "SAME_SIZE"));
    ld_coff_context_deinit(&context);
}

static void test_exact_match(const char *directory) {
    char first[1024], same[1024], different[1024];
    snprintf(first, sizeof(first), "%s/exact-first.obj", directory);
    snprintf(same, sizeof(same), "%s/exact-same.obj", directory);
    snprintf(different, sizeof(different), "%s/exact-different.obj",
             directory);
    const uint8_t exact_data[] = {0xc3, 0x11, 0x22};
    const uint8_t different_data[] = {0xc3, 0x11, 0x23};
    build_comdat_object(first, "exact_key", LD_COFF_COMDAT_EXACT_MATCH,
                        exact_data, sizeof(exact_data), UINT32_C(0x11111111),
                        NULL, 0U, false, 0U);
    build_comdat_object(same, "exact_key", LD_COFF_COMDAT_EXACT_MATCH,
                        exact_data, sizeof(exact_data), UINT32_C(0x22222222),
                        NULL, 0U, false, 0U);
    build_comdat_object(different, "exact_key", LD_COFF_COMDAT_EXACT_MATCH,
                        different_data, sizeof(different_data),
                        UINT32_C(0x11111111), NULL, 0U, false, 0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "exact_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, same) == LD_OK);
    assert(context.objects[1]->sections[0].discarded);
    ld_coff_context_deinit(&context);

    memset(&capture, 0, sizeof(capture));
    context_init(&context, &options, &capture, "exact_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, different) == LD_SYMBOL_ERROR);
    assert(strstr(capture.message, "EXACT_MATCH"));
    ld_coff_context_deinit(&context);
}

static void test_associative(const char *directory) {
    char first[1024], second[1024];
    snprintf(first, sizeof(first), "%s/assoc-first.obj", directory);
    snprintf(second, sizeof(second), "%s/assoc-second.obj", directory);
    const uint8_t data[] = {0xc3};
    build_comdat_object(first, "assoc_key", LD_COFF_COMDAT_ANY, data,
                        sizeof(data), 0U, NULL, 0U, true,
                        UINT32_C(0x11111111));
    build_comdat_object(second, "assoc_key", LD_COFF_COMDAT_ANY, data,
                        sizeof(data), 0U, NULL, 0U, true,
                        UINT32_C(0x22222222));

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "assoc_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, second) == LD_OK);
    assert(context.objects[1]->sections[0].discarded);
    assert(!context.objects[0]->sections[1].discarded);
    assert(!context.objects[1]->sections[1].discarded);

    uint8_t *image = NULL;
    size_t image_size = 0U;
    assert(ld_coff_build_image(&context, &image, &image_size) == LD_OK);
    assert(image && image_size != 0U);
    assert(!context.objects[0]->sections[1].discarded);
    assert(context.objects[1]->sections[1].discarded);
    free(image);
    ld_coff_context_deinit(&context);
}

static void test_associative_unwind_and_debug(const char *directory) {
    char first[1024], second[1024];
    snprintf(first, sizeof(first), "%s/family-first.obj", directory);
    snprintf(second, sizeof(second), "%s/family-second.obj", directory);
    build_associative_family(first, "family_key", UINT32_C(0x11111111));
    build_associative_family(second, "family_key", UINT32_C(0x22222222));

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "family_key");
    options.debug_mode = LD_DEBUG_DWARF;
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, second) == LD_OK);
    assert(context.objects[1]->sections[0].discarded);

    uint8_t *image = NULL;
    size_t image_size = 0U;
    assert(ld_coff_build_image(&context, &image, &image_size) == LD_OK);
    assert(image && image_size != 0U);
    for (size_t i = 1U; i < 4U; i++) {
        assert(!context.objects[0]->sections[i].discarded);
        assert(context.objects[1]->sections[i].discarded);
    }

    ld_coff_output_section_t *pdata = output_section(&context, ".pdata");
    ld_coff_output_section_t *xdata = output_section(&context, ".xdata");
    ld_coff_output_section_t *debug =
            output_section(&context, ".debug_info");
    assert(pdata && pdata->input_count == 1U);
    assert(xdata && xdata->input_count == 1U);
    assert(debug && debug->input_count == 1U);
    assert(debug->virtual_size == sizeof(uint32_t));
    uint32_t debug_marker = 0U;
    memcpy(&debug_marker, debug->data, sizeof(debug_marker));
    assert(debug_marker == UINT32_C(0x11111111));

    free(image);
    ld_coff_context_deinit(&context);
}

static void test_largest(const char *directory) {
    char first[1024], second[1024];
    snprintf(first, sizeof(first), "%s/largest-first.obj", directory);
    snprintf(second, sizeof(second), "%s/largest-second.obj", directory);
    const uint8_t first_data[] = {0xc3, 0x11};
    const uint8_t second_data[] = {0xc3, 0x22, 0x33, 0x44};
    build_comdat_object(first, "largest_key", LD_COFF_COMDAT_LARGEST,
                        first_data, sizeof(first_data), 0U,
                        "largest_companion", 1U, false, 0U);
    build_comdat_object(second, "largest_key", LD_COFF_COMDAT_LARGEST,
                        second_data, sizeof(second_data), 0U,
                        "largest_companion", 3U, false, 0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "largest_key");
    assert(ld_coff_load_input(&context, first) == LD_OK);
    assert(ld_coff_load_input(&context, second) == LD_OK);
    assert(context.objects[0]->sections[0].discarded);
    assert(!context.objects[1]->sections[0].discarded);
    assert(defined_global(&context, "largest_key")->section ==
           &context.objects[1]->sections[0]);
    ld_coff_global_t *companion =
            defined_global(&context, "largest_companion");
    assert(companion->section == &context.objects[1]->sections[0]);
    assert(companion->value == 3U);
    ld_coff_context_deinit(&context);
}

static void test_newest(const char *directory) {
    char object[1024];
    snprintf(object, sizeof(object), "%s/newest.obj", directory);
    const uint8_t data[] = {0xc3};
    build_comdat_object(object, "newest_key", LD_COFF_COMDAT_NEWEST, data,
                        sizeof(data), 0U, NULL, 0U, false, 0U);

    diagnostic_capture_t capture = {{0}};
    ld_options_t options;
    ld_coff_context_t context;
    context_init(&context, &options, &capture, "newest_key");
    assert(ld_coff_load_input(&context, object) == LD_UNSUPPORTED);
    assert(strstr(capture.message, "NEWEST"));
    ld_coff_context_deinit(&context);
}

int main(void) {
    char directory[] = "/tmp/nature-coff-comdat-XXXXXX";
    assert(mkdtemp(directory));
    test_noduplicates(directory);
    test_any(directory);
    test_same_size(directory);
    test_exact_match(directory);
    test_associative(directory);
    test_associative_unwind_and_debug(directory);
    test_largest(directory);
    test_newest(directory);
    return 0;
}
