#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * End-to-end symbol resolver fixtures.  The expected precedence follows
 * MachO/file.zig getSymbolRank from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224:
 *
 *   direct strong, archive/dylib strong, direct weak,
 *   archive/dylib weak, direct tentative, archive tentative.
 *
 * These tests intentionally inspect the final Mach-O rather than only the
 * rank helper.  That covers archive extraction, dylib ordinals, tentative
 * allocation, and the final symbol-table classification together.
 */

typedef struct {
    uint8_t *bytes;
    size_t size;
} test_resolver_fixture_t;

typedef struct {
    uint8_t *bytes;
    size_t size;
    const ld_mach_header_64_t *header;
    const ld_section_64_t *common;
    const ld_section_64_t *objc_catlist;
    const ld_symtab_command_t *symtab;
} test_resolver_output_t;

static test_resolver_fixture_t test_resolver_make_object(
        const char *segname, const char *sectname, uint32_t section_flags,
        uint32_t section_align, const void *section_data,
        size_t section_size, const ld_nlist_64_t *symbols,
        uint32_t symbol_count, const char *strings, uint32_t strings_size) {
    size_t segment_size =
            sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t) +
                           sizeof(ld_build_version_command_t);
    size_t section_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t symbol_offset = section_offset + section_size;
    size_t strings_offset =
            symbol_offset + (size_t) symbol_count * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + strings_size;
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 3U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    size_t cursor = sizeof(header);
    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, segname, strlen(segname));
    segment.vmsize = section_size;
    segment.fileoff = section_offset;
    segment.filesize = section_size;
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_WRITE |
                      LD_VM_PROT_EXECUTE;
    segment.initprot = segment.maxprot;
    segment.nsects = 1U;
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, sectname, strlen(sectname));
    memcpy(section.segname, segname, strlen(segname));
    section.size = section_size;
    section.offset = (uint32_t) section_offset;
    section.align = section_align;
    section.flags = section_flags;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));
    cursor += segment_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = symbol_count;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = strings_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));
    cursor += sizeof(symtab);

    ld_build_version_command_t build = {0};
    build.cmd = LD_LC_BUILD_VERSION;
    build.cmdsize = sizeof(build);
    build.platform = LD_PLATFORM_MACOS;
    build.minos = 11U << 16U;
    build.sdk = 14U << 16U;
    memcpy(object + cursor, &build, sizeof(build));

    memcpy(object + section_offset, section_data, section_size);
    memcpy(object + symbol_offset, symbols,
           (size_t) symbol_count * sizeof(*symbols));
    memcpy(object + strings_offset, strings, strings_size);
    return (test_resolver_fixture_t) {
            .bytes = object,
            .size = object_size,
    };
}

static test_resolver_fixture_t test_resolver_make_text_object(
        const ld_nlist_64_t *symbols, uint32_t symbol_count,
        const char *strings, uint32_t strings_size) {
    static const uint32_t ret = 0xd65f03c0U;
    return test_resolver_make_object(
            "__TEXT", "__text",
            LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS, 2U,
            &ret, sizeof(ret), symbols, symbol_count, strings, strings_size);
}

static void test_resolver_write_object(char path[],
                                       test_resolver_fixture_t fixture) {
    test_ld_write_fixture(path, fixture.bytes, fixture.size);
    free(fixture.bytes);
}

static void test_resolver_write_archive(char path[], const char *member_name,
                                        test_resolver_fixture_t fixture) {
    size_t padding = fixture.size & 1U;
    size_t archive_size = 8U + 60U + fixture.size + padding;
    uint8_t *archive = malloc(archive_size);
    assert(archive != NULL);
    memcpy(archive, "!<arch>\n", 8U);
    memset(archive + 8U, ' ', 60U);
    size_t name_length = strlen(member_name);
    assert(name_length < 15U);
    memcpy(archive + 8U, member_name, name_length);
    archive[8U + name_length] = '/';
    char size_field[32];
    int size_length = snprintf(size_field, sizeof(size_field), "%zu",
                               fixture.size);
    assert(size_length > 0 && size_length <= 10);
    memcpy(archive + 8U + 48U, size_field, (size_t) size_length);
    memcpy(archive + 8U + 58U, "`\n", 2U);
    memcpy(archive + 8U + 60U, fixture.bytes, fixture.size);
    if (padding) archive[archive_size - 1U] = '\n';
    test_ld_write_fixture(path, archive, archive_size);
    free(archive);
    free(fixture.bytes);
}

static void test_resolver_write_dylib(char path[], const char *install_name,
                                      const char *symbol, bool weak) {
    char tbd[1024];
    int length = snprintf(
            tbd, sizeof(tbd),
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: '%s'\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    %s: [ %s ]\n"
            "...\n",
            install_name, weak ? "weak-symbols" : "symbols", symbol);
    assert(length > 0 && (size_t) length < sizeof(tbd));
    test_ld_write_fixture(path, tbd, (size_t) length);
}

static void test_resolver_output_path(char path[]) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);
}

static void test_resolver_link(const char *const *inputs, size_t input_count,
                               const char *output_path) {
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    for (size_t i = 0; i < input_count; i++) {
        assert(ld_add_input(&options, inputs[i]) == LD_OK);
    }
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);
}

static test_resolver_output_t test_resolver_read_output(const char *path) {
    test_resolver_output_t output = {0};
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    output.size = (size_t) st.st_size;
    output.bytes = malloc(output.size);
    assert(output.bytes != NULL);
    size_t read_size = 0;
    while (read_size < output.size) {
        ssize_t count = read(fd, output.bytes + read_size,
                             output.size - read_size);
        assert(count > 0);
        read_size += (size_t) count;
    }
    assert(close(fd) == 0);
    output.header = (const ld_mach_header_64_t *) output.bytes;
    assert(output.header->magic == LD_MH_MAGIC_64);

    size_t cursor = sizeof(*output.header);
    for (uint32_t i = 0; i < output.header->ncmds; i++) {
        assert(cursor <= output.size &&
               sizeof(ld_load_command_t) <= output.size - cursor);
        const ld_load_command_t *command =
                (const ld_load_command_t *) (output.bytes + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               command->cmdsize <= output.size - cursor);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *segment =
                    (const ld_segment_command_64_t *) command;
            assert(segment->cmdsize >= sizeof(*segment) &&
                   (size_t) segment->nsects * sizeof(ld_section_64_t) <=
                           segment->cmdsize - sizeof(*segment));
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (segment + 1);
            for (uint32_t j = 0; j < segment->nsects; j++) {
                if (strncmp(sections[j].sectname, "__common", 16U) == 0) {
                    output.common = &sections[j];
                } else if (strncmp(sections[j].sectname, "__objc_catlist",
                                   16U) == 0) {
                    output.objc_catlist = &sections[j];
                }
            }
        } else if (command->cmd == LD_LC_SYMTAB) {
            output.symtab = (const ld_symtab_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    return output;
}

static const ld_nlist_64_t *test_resolver_find_symbol(
        const test_resolver_output_t *output, const char *name) {
    assert(output->symtab != NULL);
    assert(output->symtab->symoff <= output->size &&
           (size_t) output->symtab->nsyms * sizeof(ld_nlist_64_t) <=
                   output->size - output->symtab->symoff);
    assert(output->symtab->stroff <= output->size &&
           output->symtab->strsize <=
                   output->size - output->symtab->stroff);
    const ld_nlist_64_t *symbols = (const ld_nlist_64_t *) (output->bytes + output->symtab->symoff);
    const char *strings =
            (const char *) output->bytes + output->symtab->stroff;
    for (uint32_t i = 0; i < output->symtab->nsyms; i++) {
        assert(symbols[i].n_strx < output->symtab->strsize);
        const char *candidate = strings + symbols[i].n_strx;
        assert(memchr(candidate, '\0',
                      output->symtab->strsize - symbols[i].n_strx) != NULL);
        if (strcmp(candidate, name) == 0) return &symbols[i];
    }
    return NULL;
}

static void test_resolver_free_output(test_resolver_output_t *output) {
    free(output->bytes);
    memset(output, 0, sizeof(*output));
}

static void test_resolver_make_main(char path[], const char *unresolved) {
    char strings[128] = "\0_main\0";
    size_t strings_size = sizeof("\0_main\0");
    ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U,
             .n_value = 0U},
    };
    uint32_t symbol_count = 1U;
    if (unresolved) {
        size_t length = strlen(unresolved) + 1U;
        assert(length <= sizeof(strings) - strings_size);
        symbols[1].n_strx = (uint32_t) strings_size;
        symbols[1].n_type = LD_N_UNDF | LD_N_EXT;
        memcpy(strings + strings_size, unresolved, length);
        strings_size += length;
        symbol_count++;
    }
    test_resolver_write_object(
            path, test_resolver_make_text_object(
                          symbols, symbol_count, strings,
                          (uint32_t) strings_size));
}

static void test_resolver_make_definition(char path[], const char *name,
                                          bool weak) {
    char strings[128] = {0};
    size_t length = strlen(name) + 1U;
    assert(length + 1U <= sizeof(strings));
    memcpy(strings + 1U, name, length);
    ld_nlist_64_t symbol = {
            .n_strx = 1U,
            .n_type = LD_N_SECT | LD_N_EXT,
            .n_sect = 1U,
            .n_desc = weak ? LD_N_WEAK_DEF : 0U,
            .n_value = 0U,
    };
    test_resolver_write_object(
            path, test_resolver_make_text_object(
                          &symbol, 1U, strings, (uint32_t) (length + 1U)));
}

static test_resolver_fixture_t test_resolver_make_definition_fixture(
        const char *name) {
    char strings[128] = {0};
    size_t length = strlen(name) + 1U;
    assert(length + 1U <= sizeof(strings));
    memcpy(strings + 1U, name, length);
    ld_nlist_64_t symbol = {
            .n_strx = 1U,
            .n_type = LD_N_SECT | LD_N_EXT,
            .n_sect = 1U,
    };
    return test_resolver_make_text_object(
            &symbol, 1U, strings, (uint32_t) (length + 1U));
}

static void test_entry_symbol_from_archive(void) {
    char archive_path[] = "/tmp/nature-ld-resolver-entry-archive-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_write_archive(
            archive_path, "entry.o",
            test_resolver_make_definition_fixture("_main"));
    test_resolver_output_path(output_path);
    const char *inputs[] = {archive_path};
    test_resolver_link(inputs, 1U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *entry =
            test_resolver_find_symbol(&output, "_main");
    assert(entry != NULL && (entry->n_type & LD_N_TYPE) == LD_N_SECT);
    test_resolver_free_output(&output);
    unlink(archive_path);
    unlink(output_path);
}

static void test_resolver_make_common(char path[], const char *name,
                                      uint64_t size,
                                      uint32_t alignment_log2) {
    char strings[128] = {0};
    size_t length = strlen(name) + 1U;
    assert(length + 1U <= sizeof(strings));
    assert(alignment_log2 <= 15U);
    memcpy(strings + 1U, name, length);
    ld_nlist_64_t symbol = {
            .n_strx = 1U,
            .n_type = LD_N_UNDF | LD_N_EXT,
            .n_desc = (uint16_t) (alignment_log2 << 8U),
            .n_value = size,
    };
    test_resolver_write_object(
            path, test_resolver_make_text_object(
                          &symbol, 1U, strings, (uint32_t) (length + 1U)));
}

static void test_weak_object_vs_strong_dylib(void) {
    char main_path[] = "/tmp/nature-ld-resolver-main-XXXXXX";
    char weak_path[] = "/tmp/nature-ld-resolver-weak-XXXXXX";
    char dylib_path[] = "/tmp/nature-ld-resolver-strong-tbd-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_make_main(main_path, "_pick");
    test_resolver_make_definition(weak_path, "_pick", true);
    test_resolver_write_dylib(dylib_path,
                              "/usr/lib/libResolverStrong.dylib", "_pick",
                              false);
    test_resolver_output_path(output_path);
    const char *inputs[] = {main_path, weak_path, dylib_path};
    test_resolver_link(inputs, 3U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *pick = test_resolver_find_symbol(&output, "_pick");
    assert(pick != NULL);
    assert((pick->n_type & LD_N_TYPE) == LD_N_UNDF);
    assert((pick->n_type & LD_N_EXT) != 0);
    assert((pick->n_desc >> 8U) == 2U);
    assert((output.header->flags & LD_MH_WEAK_DEFINES) == 0);
    test_resolver_free_output(&output);
    unlink(main_path);
    unlink(weak_path);
    unlink(dylib_path);
    unlink(output_path);
}

static void test_common_vs_strong_dylib(void) {
    char main_path[] = "/tmp/nature-ld-resolver-main-XXXXXX";
    char common_path[] = "/tmp/nature-ld-resolver-common-XXXXXX";
    char dylib_path[] = "/tmp/nature-ld-resolver-common-tbd-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_make_main(main_path, "_pick");
    test_resolver_make_common(common_path, "_pick", 64U, 6U);
    test_resolver_write_dylib(dylib_path,
                              "/usr/lib/libResolverCommon.dylib", "_pick",
                              false);
    test_resolver_output_path(output_path);
    const char *inputs[] = {main_path, common_path, dylib_path};
    test_resolver_link(inputs, 3U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *pick = test_resolver_find_symbol(&output, "_pick");
    assert(pick != NULL && (pick->n_type & LD_N_TYPE) == LD_N_UNDF);
    assert((pick->n_desc >> 8U) == 2U);
    assert(output.common == NULL);
    test_resolver_free_output(&output);
    unlink(main_path);
    unlink(common_path);
    unlink(dylib_path);
    unlink(output_path);
}

static void test_archive_vs_dylib_input_order(bool archive_first) {
    char main_path[] = "/tmp/nature-ld-resolver-main-XXXXXX";
    char archive_path[] = "/tmp/nature-ld-resolver-archive-XXXXXX";
    char dylib_path[] = "/tmp/nature-ld-resolver-archive-tbd-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_make_main(main_path, "_pick");
    test_resolver_write_archive(
            archive_path, "winner.o",
            test_resolver_make_definition_fixture("_pick"));
    test_resolver_write_dylib(dylib_path,
                              "/usr/lib/libResolverArchive.dylib", "_pick",
                              false);
    test_resolver_output_path(output_path);
    const char *archive_first_inputs[] = {main_path, archive_path,
                                          dylib_path};
    const char *dylib_first_inputs[] = {main_path, dylib_path, archive_path};
    test_resolver_link(archive_first ? archive_first_inputs
                                     : dylib_first_inputs,
                       3U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *pick = test_resolver_find_symbol(&output, "_pick");
    assert(pick != NULL);
    if (archive_first) {
        assert((pick->n_type & LD_N_TYPE) == LD_N_SECT);
        assert((pick->n_type & LD_N_EXT) != 0);
    } else {
        assert((pick->n_type & LD_N_TYPE) == LD_N_UNDF);
        assert((pick->n_desc >> 8U) == 2U);
    }
    test_resolver_free_output(&output);
    unlink(main_path);
    unlink(archive_path);
    unlink(dylib_path);
    unlink(output_path);
}

static void test_two_dylib_providers_case(bool first_weak, bool second_weak,
                                          uint32_t expected_ordinal) {
    char main_path[] = "/tmp/nature-ld-resolver-main-XXXXXX";
    char first_path[] = "/tmp/nature-ld-resolver-first-tbd-XXXXXX";
    char second_path[] = "/tmp/nature-ld-resolver-second-tbd-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_make_main(main_path, "_pick");
    test_resolver_write_dylib(first_path,
                              "/usr/lib/libResolverFirst.dylib", "_pick",
                              first_weak);
    test_resolver_write_dylib(second_path,
                              "/usr/lib/libResolverSecond.dylib", "_pick",
                              second_weak);
    test_resolver_output_path(output_path);
    const char *inputs[] = {main_path, first_path, second_path};
    test_resolver_link(inputs, 3U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *pick = test_resolver_find_symbol(&output, "_pick");
    assert(pick != NULL && (pick->n_type & LD_N_TYPE) == LD_N_UNDF);
    assert((pick->n_desc >> 8U) == expected_ordinal);
    test_resolver_free_output(&output);
    unlink(main_path);
    unlink(first_path);
    unlink(second_path);
    unlink(output_path);
}

static void test_two_dylib_providers(void) {
    /* Strength outranks input order; equal-strength providers use input
       order, which is Zig's file-index tie breaker. */
    test_two_dylib_providers_case(true, false, 3U);
    test_two_dylib_providers_case(false, true, 2U);
    test_two_dylib_providers_case(false, false, 2U);
    test_two_dylib_providers_case(true, true, 2U);
}

static void test_duplicate_direct_strong(void) {
    static const char first_strings[] = "\0_main\0_dup\0";
    ld_nlist_64_t first_symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
    };
    char first_path[] = "/tmp/nature-ld-resolver-duplicate-a-XXXXXX";
    char second_path[] = "/tmp/nature-ld-resolver-duplicate-b-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_write_object(
            first_path,
            test_resolver_make_text_object(
                    first_symbols, 2U, first_strings,
                    (uint32_t) sizeof(first_strings)));
    test_resolver_make_definition(second_path, "_dup", false);
    test_resolver_output_path(output_path);

    test_ld_diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, first_path) == LD_OK);
    assert(ld_add_input(&options, second_path) == LD_OK);
    assert(ld_link(&options) == LD_SYMBOL_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "duplicate symbol definition: _dup") !=
           NULL);
    assert(access(output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(first_path);
    unlink(second_path);
}

static void test_common_max_size_and_alignment(void) {
    static const char first_strings[] = "\0_main\0_common\0";
    ld_nlist_64_t first_symbols[] = {
            {.n_strx = 1U,
             .n_type = LD_N_SECT | LD_N_EXT,
             .n_sect = 1U},
            {.n_strx = 7U,
             .n_type = LD_N_UNDF | LD_N_EXT,
             .n_desc = 2U << 8U,
             .n_value = 32U},
    };
    char first_path[] = "/tmp/nature-ld-resolver-common-a-XXXXXX";
    char second_path[] = "/tmp/nature-ld-resolver-common-b-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_write_object(
            first_path,
            test_resolver_make_text_object(
                    first_symbols, 2U, first_strings,
                    (uint32_t) sizeof(first_strings)));
    /* The later common contributes the larger alignment but a smaller size;
       both maxima must survive independently. */
    test_resolver_make_common(second_path, "_common", 8U, 5U);
    test_resolver_output_path(output_path);
    const char *inputs[] = {first_path, second_path};
    test_resolver_link(inputs, 2U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    const ld_nlist_64_t *common =
            test_resolver_find_symbol(&output, "_common");
    assert(common != NULL && (common->n_type & LD_N_TYPE) == LD_N_SECT);
    assert(output.common != NULL);
    assert(output.common->size == 32U);
    assert(output.common->align == 5U);
    assert(common->n_value == output.common->addr);
    assert((common->n_value & 31U) == 0U);
    test_resolver_free_output(&output);
    unlink(first_path);
    unlink(second_path);
    unlink(output_path);
}

static void test_objc_category_archive_extraction(void) {
    char main_path[] = "/tmp/nature-ld-resolver-main-XXXXXX";
    char archive_path[] = "/tmp/nature-ld-resolver-objc-archive-XXXXXX";
    char output_path[] = "/tmp/nature-ld-resolver-output-XXXXXX";
    test_resolver_make_main(main_path, NULL);

    static const uint64_t category_pointer = 0U;
    static const char strings[] = "\0_objc_category_marker\0";
    ld_nlist_64_t marker = {
            .n_strx = 1U,
            .n_type = LD_N_SECT | LD_N_EXT,
            .n_sect = 1U,
    };
    test_resolver_fixture_t category = test_resolver_make_object(
            "__DATA", "__objc_catlist", 0U, 3U, &category_pointer,
            sizeof(category_pointer), &marker, 1U, strings,
            (uint32_t) sizeof(strings));
    test_resolver_write_archive(archive_path, "category.o", category);
    test_resolver_output_path(output_path);
    const char *inputs[] = {main_path, archive_path};
    test_resolver_link(inputs, 2U, output_path);

    test_resolver_output_t output = test_resolver_read_output(output_path);
    assert(output.objc_catlist != NULL);
    assert(output.objc_catlist->size == sizeof(category_pointer));
    const ld_nlist_64_t *marker_output =
            test_resolver_find_symbol(&output, "_objc_category_marker");
    assert(marker_output != NULL &&
           (marker_output->n_type & LD_N_TYPE) == LD_N_SECT);
    test_resolver_free_output(&output);
    unlink(main_path);
    unlink(archive_path);
    unlink(output_path);
}

void test_ld_macho_resolver(void) {
    test_entry_symbol_from_archive();
    test_weak_object_vs_strong_dylib();
    test_common_vs_strong_dylib();
    test_archive_vs_dylib_input_order(true);
    test_archive_vs_dylib_input_order(false);
    test_two_dylib_providers();
    test_duplicate_direct_strong();
    test_common_max_size_and_alignment();
    test_objc_category_archive_extraction();
}
