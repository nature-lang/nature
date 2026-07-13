#include "src/ld/ld.h"
#include "src/ld/ld_internal.h"
#include "src/ld/sha256.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    unsigned count;
    char message[4096];
} diagnostic_capture_t;

static void capture_diagnostic(void *context, ld_diag_level_t level, const char *message) {
    (void) level;
    diagnostic_capture_t *capture = context;
    capture->count++;
    snprintf(capture->message, sizeof(capture->message), "%s", message);
}

static void write_fixture(char path[], const void *bytes, size_t size) {
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, bytes, size) == (ssize_t) size);
    assert(close(fd) == 0);
}

static void write_named_fixture(const char *path, const void *bytes, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, bytes, size) == (ssize_t) size);
    assert(close(fd) == 0);
}

static void expect_invalid_input(const char *path, int expected, const char *message_fragment) {
    static const char *output = "/tmp/nature-ld-invalid-output";
    unlink(output);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, path) == LD_OK);
    assert(ld_link(&options) == expected);
    assert(capture.count > 0);
    assert(strstr(capture.message, message_fragment) != NULL);
    assert(access(output, F_OK) != 0);
    ld_options_deinit(&options);
}

static bool dylib_has_symbol(char *const *symbols, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(symbols[i], name) == 0) return true;
    }
    return false;
}

static void parsed_context_deinit(ld_context_t *ctx) {
    for (size_t i = 0; i < ctx->files.count; i++) {
        free(ctx->files.items[i]->bytes);
        free(ctx->files.items[i]->path);
        free(ctx->files.items[i]);
    }
    for (size_t i = 0; i < ctx->dylibs.count; i++) {
        ld_dylib_input_t *dylib = &ctx->dylibs.items[i];
        free(dylib->path);
        free(dylib->install_name);
        ld_string_set_deinit(&dylib->export_set);
        for (size_t j = 0; j < dylib->export_count; j++) free(dylib->exports[j]);
        free(dylib->exports);
        ld_string_set_deinit(&dylib->weak_export_set);
        for (size_t j = 0; j < dylib->weak_export_count; j++) free(dylib->weak_exports[j]);
        free(dylib->weak_exports);
        ld_string_set_deinit(&dylib->reexport_set);
        for (size_t j = 0; j < dylib->reexport_count; j++) free(dylib->reexports[j]);
        free(dylib->reexports);
    }
    free(ctx->files.items);
    free(ctx->dylibs.items);
}

static void test_tbd_objc_exports(void) {
    static const char tbd_v4[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos, x86_64-macos ]\n"
            "install-name: '/System/Library/Frameworks/Test.framework/Test'\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    symbols: [ '$ld$add$os10.5$_AddedForCompatibility',\n"
            "               '$ld$hide$os10.5$_HiddenCompatibility' ]\n"
            "    objc-classes: [ NSApplication,\n"
            "                    'NSQuotedClass' ]\n"
            "    weak-objc-classes: [ NSWeakClass ]\n"
            "    objc-eh-types: [ NSException ]\n"
            "    objc-ivars: [ NSApplication._delegate ]\n"
            "    weak-objc-ivars: [ NSWeakClass._value ]\n"
            "  - targets: [ x86_64-macos ]\n"
            "    objc-classes: [ NSWrongArchitecture ]\n"
            "...\n";
    char v4_path[] = "/tmp/nature-ld-tbd-v4-XXXXXX";
    write_fixture(v4_path, tbd_v4, sizeof(tbd_v4) - 1U);

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, v4_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_NSApplication"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_METACLASS_$_NSApplication"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_NSQuotedClass"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_EHTYPE_$_NSException"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_IVAR_$_NSApplication._delegate"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_AddedForCompatibility"));
    assert(!dylib_has_symbol(dylib->exports, dylib->export_count,
                             "_HiddenCompatibility"));
    assert(!dylib_has_symbol(dylib->exports, dylib->export_count,
                             "_OBJC_CLASS_$_NSWrongArchitecture"));
    assert(dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_CLASS_$_NSWeakClass"));
    assert(dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_METACLASS_$_NSWeakClass"));
    assert(dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_IVAR_$_NSWeakClass._value"));
    parsed_context_deinit(&ctx);
    unlink(v4_path);

    static const char tbd_v3[] =
            "--- !tapi-tbd-v3\n"
            "archs: [ arm64, x86_64 ]\n"
            "platform: macosx\n"
            "install-name: '/usr/lib/libObjCTest.dylib'\n"
            "exports:\n"
            "  - archs: [ arm64 ]\n"
            "    objc-classes: [ LegacyClass ]\n"
            "    objc-eh-types: [ LegacyException ]\n"
            "  - archs: [ x86_64 ]\n"
            "    weak-objc-ivars: [ WrongArchitecture._value ]\n"
            "...\n";
    char v3_path[] = "/tmp/nature-ld-tbd-v3-XXXXXX";
    write_fixture(v3_path, tbd_v3, sizeof(tbd_v3) - 1U);
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, v3_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    dylib = &ctx.dylibs.items[0];
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_LegacyClass"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_METACLASS_$_LegacyClass"));
    assert(dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_EHTYPE_$_LegacyException"));
    assert(!dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                             "_OBJC_IVAR_$_WrongArchitecture._value"));
    parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(v3_path);
}

static void test_binary_dylib_reexport(void) {
    static const char install_name[] = "/usr/lib/libParent.dylib";
    static const char reexport_name[] = "/usr/lib/libChild.dylib";
    uint32_t id_size = (uint32_t) ((sizeof(ld_dylib_command_t) + sizeof(install_name) + 7U) & ~7U);
    uint32_t reexport_size =
            (uint32_t) ((sizeof(ld_dylib_command_t) + sizeof(reexport_name) + 7U) & ~7U);
    size_t file_size = sizeof(ld_mach_header_64_t) + id_size + reexport_size;
    uint8_t *bytes = calloc(1, file_size);
    assert(bytes != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    header.ncmds = 2;
    header.sizeofcmds = id_size + reexport_size;
    memcpy(bytes, &header, sizeof(header));

    ld_dylib_command_t id = {0};
    id.cmd = LD_LC_ID_DYLIB;
    id.cmdsize = id_size;
    id.name_offset = sizeof(id);
    id.current_version = ld_macos_version(7, 8, 9);
    id.compatibility_version = ld_macos_version(2, 3, 4);
    size_t offset = sizeof(header);
    memcpy(bytes + offset, &id, sizeof(id));
    memcpy(bytes + offset + sizeof(id), install_name, sizeof(install_name));

    ld_dylib_command_t reexport = {0};
    reexport.cmd = LD_LC_REEXPORT_DYLIB;
    reexport.cmdsize = reexport_size;
    reexport.name_offset = sizeof(reexport);
    offset += id_size;
    memcpy(bytes + offset, &reexport, sizeof(reexport));
    memcpy(bytes + offset + sizeof(reexport), reexport_name, sizeof(reexport_name));

    char path[] = "/tmp/nature-ld-reexport-dylib-XXXXXX";
    write_fixture(path, bytes, file_size);
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    assert(strcmp(ctx.dylibs.items[0].install_name, install_name) == 0);
    assert(ctx.dylibs.items[0].current_version == ld_macos_version(7, 8, 9));
    assert(ctx.dylibs.items[0].compatibility_version == ld_macos_version(2, 3, 4));
    assert(ctx.dylibs.items[0].reexport_count == 1U);
    assert(strcmp(ctx.dylibs.items[0].reexports[0], reexport_name) == 0);
    parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(path);

    /* MH_NO_REEXPORTED_DYLIBS makes LC_REEXPORT_DYLIB metadata inert.  Keep
       the command in the fixture to ensure the parser checks the header flag
       instead of blindly recording every reexport command. */
    ((ld_mach_header_64_t *) bytes)->flags = LD_MH_NO_REEXPORTED_DYLIBS;
    char no_reexport_path[] = "/tmp/nature-ld-no-reexport-dylib-XXXXXX";
    write_fixture(no_reexport_path, bytes, file_size);
    ld_options_init(&options);
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, no_reexport_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    assert(ctx.dylibs.items[0].reexport_count == 0U);
    parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(no_reexport_path);
    free(bytes);
}

static void test_framework_path_reexport(void) {
    static const char parent_tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: '/System/Library/Frameworks/Parent.framework/Versions/A/Parent'\n"
            "reexported-libraries:\n"
            "  - targets: [ arm64-macos ]\n"
            "    libraries: [ '/System/Library/Frameworks/Child.framework/Versions/A/Child' ]\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    symbols: [ _parent ]\n"
            "...\n";
    static const char child_tbd[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ arm64-macos ]\n"
            "install-name: '/System/Library/Frameworks/Child.framework/Versions/A/Child'\n"
            "exports:\n"
            "  - targets: [ arm64-macos ]\n"
            "    symbols: [ _child ]\n"
            "...\n";

    char root[] = "/tmp/nature-ld-framework-reexport-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char system[PATH_MAX], library[PATH_MAX], frameworks[PATH_MAX];
    char parent_dir[PATH_MAX], child_dir[PATH_MAX], parent_path[PATH_MAX], child_path[PATH_MAX];
    assert(snprintf(system, sizeof(system), "%s/System", root) > 0);
    assert(snprintf(library, sizeof(library), "%s/Library", system) > 0);
    assert(snprintf(frameworks, sizeof(frameworks), "%s/Frameworks", library) > 0);
    assert(snprintf(parent_dir, sizeof(parent_dir), "%s/Parent.framework", frameworks) > 0);
    assert(snprintf(child_dir, sizeof(child_dir), "%s/Child.framework", frameworks) > 0);
    assert(mkdir(system, 0700) == 0);
    assert(mkdir(library, 0700) == 0);
    assert(mkdir(frameworks, 0700) == 0);
    assert(mkdir(parent_dir, 0700) == 0);
    assert(mkdir(child_dir, 0700) == 0);
    char child_versions[PATH_MAX];
    assert(snprintf(child_versions, sizeof(child_versions), "%s/Versions", child_dir) > 0);
    assert(mkdir(child_versions, 0700) == 0);
    char child_version_a[PATH_MAX];
    assert(snprintf(child_version_a, sizeof(child_version_a), "%s/A", child_versions) > 0);
    assert(mkdir(child_version_a, 0700) == 0);
    assert(snprintf(parent_path, sizeof(parent_path), "%s/Parent.tbd", parent_dir) > 0);
    assert(snprintf(child_path, sizeof(child_path), "%s/Child.tbd", child_version_a) > 0);
    write_named_fixture(parent_path, parent_tbd, sizeof(parent_tbd) - 1U);
    write_named_fixture(child_path, child_tbd, sizeof(child_tbd) - 1U);

    ld_options_t options;
    ld_options_init(&options);
    char flags[PATH_MAX + 32U];
    assert(snprintf(flags, sizeof(flags), "-F '%s' -framework Parent", frameworks) > 0);
    assert(ld_parse_flags(&options, flags) == LD_OK);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_resolve_requested_libraries(&ctx) == LD_OK);
    assert(ctx.dylibs.count == 1U);

    ld_symbol_t unresolved = {.name = strdup("_child"), .kind = LD_SYMBOL_UNDEFINED};
    assert(unresolved.name != NULL);
    HASH_ADD_KEYPTR(hh, ctx.symbols, unresolved.name, strlen(unresolved.name), &unresolved);
    assert(ld_resolve_reexport_libraries(&ctx) == LD_OK);
    assert(ctx.dylibs.count == 2U);
    assert(dylib_has_symbol(ctx.dylibs.items[0].exports, ctx.dylibs.items[0].export_count, "_child"));
    HASH_DEL(ctx.symbols, &unresolved);
    free(unresolved.name);

    parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(child_path) == 0);
    assert(unlink(parent_path) == 0);
    assert(rmdir(child_version_a) == 0);
    assert(rmdir(child_versions) == 0);
    assert(rmdir(child_dir) == 0);
    assert(rmdir(parent_dir) == 0);
    assert(rmdir(frameworks) == 0);
    assert(rmdir(library) == 0);
    assert(rmdir(system) == 0);
    assert(rmdir(root) == 0);
}

static void test_sdk_appkit_objc_exports(void) {
    const char *sdkroot = getenv("SDKROOT");
    if (!sdkroot || !*sdkroot) return;
    char appkit_path[4096];
    int path_length = snprintf(appkit_path, sizeof(appkit_path),
                               "%s/System/Library/Frameworks/AppKit.framework/AppKit.tbd", sdkroot);
    if (path_length < 0 || (size_t) path_length >= sizeof(appkit_path) || access(appkit_path, R_OK) != 0) {
        return;
    }

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, appkit_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *appkit = &ctx.dylibs.items[0];
    assert(dylib_has_symbol(appkit->exports, appkit->export_count,
                            "_OBJC_CLASS_$_NSApplication"));
    assert(dylib_has_symbol(appkit->exports, appkit->export_count,
                            "_OBJC_METACLASS_$_NSApplication"));
    parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
}

static void test_compact_unwind_regular_page(void) {
    uint8_t compact_data[2U * sizeof(ld_compact_unwind_entry_t)] = {0};
    ld_compact_unwind_entry_t entries_in[2] = {
            {.range_length = 0x20U, .compact_encoding = 0x02000000U},
            {.range_start = 0x20U,
             .range_length = 0x10U,
             .compact_encoding = 0x04000001U},
    };
    memcpy(compact_data, entries_in, sizeof(entries_in));

    uint8_t relocations[16] = {0};
    uint32_t relocation_address = 0;
    uint32_t relocation_word = 1U | (3U << 25U);
    memcpy(relocations, &relocation_address, sizeof(relocation_address));
    memcpy(relocations + 4U, &relocation_word, sizeof(relocation_word));
    relocation_address = sizeof(ld_compact_unwind_entry_t);
    memcpy(relocations + 8U, &relocation_address, sizeof(relocation_address));
    memcpy(relocations + 12U, &relocation_word, sizeof(relocation_word));

    ld_output_section_t text_output = {0};
    memcpy(text_output.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(text_output.sectname, "__text", sizeof("__text"));
    text_output.addr = LD_IMAGE_BASE + 0x1000U;

    ld_input_section_t sections[2] = {0};
    memcpy(sections[0].header.segname, "__TEXT", sizeof("__TEXT"));
    memcpy(sections[0].header.sectname, "__text", sizeof("__text"));
    sections[0].output = &text_output;
    memcpy(sections[1].header.segname, "__LD", sizeof("__LD"));
    memcpy(sections[1].header.sectname, "__compact_unwind", 16U);
    sections[1].header.size = sizeof(compact_data);
    sections[1].header.nreloc = 2;
    sections[1].data = compact_data;
    sections[1].relocations = relocations;

    ld_file_t file = {.path = "/tmp/ld-unwind-unit.o"};
    ld_object_t object = {
            .file = &file,
            .selected = true,
            .sections = sections,
            .section_count = 2,
    };
    sections[0].object = &object;
    sections[1].object = &object;
    ld_object_t *objects[] = {&object};
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    ctx.objects.items = objects;
    ctx.objects.count = 1;

    assert(ld_unwind_prepare(&ctx) == LD_OK);
    assert(ctx.unwind.count == 2);
    assert(ctx.unwind.output != NULL);
    assert(strcmp(ctx.unwind.output->sectname, "__unwind_info") == 0);
    assert(ld_unwind_emit(&ctx) == LD_OK);

    ld_unwind_info_header_t header;
    memcpy(&header, ctx.unwind.output->data, sizeof(header));
    assert(header.version == LD_UNWIND_SECTION_VERSION);
    assert(header.common_encodings_count == 0);
    assert(header.personalities_count == 0);
    assert(header.index_count == 2);

    ld_unwind_info_index_entry_t index[2];
    memcpy(index, ctx.unwind.output->data + header.index_offset, sizeof(index));
    assert(index[0].function_offset == 0x1000U);
    assert(index[1].function_offset == 0x1030U);
    assert(index[1].second_level_page_offset == 0);

    ld_unwind_info_regular_page_header_t page;
    memcpy(&page, ctx.unwind.output->data + index[0].second_level_page_offset,
           sizeof(page));
    assert(page.kind == LD_UNWIND_SECOND_LEVEL_REGULAR);
    assert(page.entry_page_offset == sizeof(page));
    assert(page.entry_count == 2);
    ld_unwind_info_regular_entry_t entries_out[2];
    memcpy(entries_out,
           ctx.unwind.output->data + index[0].second_level_page_offset +
                   page.entry_page_offset,
           sizeof(entries_out));
    assert(entries_out[0].function_offset == 0x1000U);
    assert(entries_out[0].encoding == 0x02000000U);
    assert(entries_out[1].function_offset == 0x1020U);
    assert(entries_out[1].encoding == 0x04000001U);

    free(ctx.unwind.output->data);
    free(ctx.unwind.output);
    free(ctx.outputs.items);
    free(ctx.unwind.records);
    ld_options_deinit(&options);
}

static void test_arm64_branch_island(void) {
    static const char strings[] = "\0_main\0_far\0";
    const uint64_t far_address = LD_IMAGE_BASE + 0x10000000ULL;
    size_t segment_size = sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t relocation_offset = text_offset + sizeof(uint32_t);
    size_t symbol_offset = relocation_offset + 4U * sizeof(uint32_t);
    size_t strings_offset = symbol_offset + 2U * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + sizeof(strings);
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 2U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, "__TEXT", sizeof("__TEXT") - 1U);
    segment.vmsize = sizeof(uint32_t);
    segment.fileoff = text_offset;
    segment.filesize = sizeof(uint32_t);
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.initprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.nsects = 1U;
    size_t cursor = sizeof(header);
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = sizeof(uint32_t);
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.reloff = (uint32_t) relocation_offset;
    section.nreloc = 2U;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    cursor += segment_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));

    uint32_t branch = 0x94000000U;
    memcpy(object + text_offset, &branch, sizeof(branch));
    uint32_t relocation[4] = {
            0U,
            4U | (LD_ARM64_RELOC_ADDEND << 28U),
            0U,
            1U | (1U << 24U) | (2U << 25U) | (1U << 27U) |
                    (LD_ARM64_RELOC_BRANCH26 << 28U),
    };
    memcpy(object + relocation_offset, relocation, sizeof(relocation));
    ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U, .n_type = LD_N_SECT | LD_N_EXT, .n_sect = 1U, .n_value = 0U},
            {.n_strx = 7U, .n_type = LD_N_ABS | LD_N_EXT, .n_value = far_address},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));

    char object_path[] = "/tmp/nature-ld-branch-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    char output_path[] = "/tmp/nature-ld-branch-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);

    int fd = open(output_path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    uint8_t *image = malloc((size_t) st.st_size);
    assert(image != NULL);
    assert(read(fd, image, (size_t) st.st_size) == st.st_size);
    assert(close(fd) == 0);
    const ld_mach_header_64_t *output_header = (const ld_mach_header_64_t *) image;
    assert(output_header->magic == LD_MH_MAGIC_64);
    const ld_section_64_t *text = NULL;
    const ld_section_64_t *islands = NULL;
    cursor = sizeof(*output_header);
    for (uint32_t command_index = 0; command_index < output_header->ncmds; command_index++) {
        assert(cursor + sizeof(ld_load_command_t) <= (size_t) st.st_size);
        const ld_load_command_t *command = (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *output_segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (output_segment + 1);
            for (uint32_t section_index = 0; section_index < output_segment->nsects;
                 section_index++) {
                if (strncmp(sections[section_index].sectname, "__text", 16) == 0) {
                    text = &sections[section_index];
                } else if (strncmp(sections[section_index].sectname, "__branch_islands", 16) == 0) {
                    islands = &sections[section_index];
                }
            }
        }
        cursor += command->cmdsize;
    }
    assert(text != NULL && islands != NULL && islands->size == 12U);
    assert(text->offset + sizeof(uint32_t) <= (size_t) st.st_size);
    assert(islands->offset + islands->size <= (size_t) st.st_size);
    memcpy(&branch, image + text->offset, sizeof(branch));
    int64_t branch_immediate = branch & 0x03ffffffU;
    if (branch_immediate & 0x02000000U) branch_immediate -= 1LL << 26U;
    assert(text->addr + (uint64_t) (branch_immediate * 4LL) == islands->addr);

    uint32_t adrp, add, br;
    memcpy(&adrp, image + islands->offset, sizeof(adrp));
    memcpy(&add, image + islands->offset + 4U, sizeof(add));
    memcpy(&br, image + islands->offset + 8U, sizeof(br));
    assert((adrp & 0x9f00001fU) == 0x90000010U);
    assert((add & 0xffc003ffU) == 0x91000210U);
    assert(br == 0xd61f0200U);
    int64_t adrp_immediate = (int64_t) (((adrp >> 5U) & 0x7ffffU) << 2U) |
                             (int64_t) ((adrp >> 29U) & 3U);
    if (adrp_immediate & (1LL << 20U)) adrp_immediate -= 1LL << 21U;
    uint64_t thunk_target = (uint64_t) ((int64_t) (islands->addr & ~0xfffULL) +
                                        adrp_immediate * 0x1000LL) +
                            ((add >> 10U) & 0xfffU);
    assert(thunk_target == far_address + 4U);
    free(image);

    /* POINTER_TO_GOT has no absolute 64-bit encoding on arm64.  A malformed
       object must fail during relocation validation before an output file is
       created, instead of silently treating the field as an image pointer. */
    ld_section_64_t *input_section =
            (ld_section_64_t *) (object + sizeof(ld_mach_header_64_t) + sizeof(ld_segment_command_64_t));
    input_section->nreloc = 1U;
    uint32_t invalid_pointer_relocation[2] = {
            0U,
            1U | (2U << 25U) | (1U << 27U) | (LD_ARM64_RELOC_POINTER_TO_GOT << 28U),
    };
    memcpy(object + relocation_offset, invalid_pointer_relocation,
           sizeof(invalid_pointer_relocation));
    char invalid_object_path[] = "/tmp/nature-ld-pointer-object-XXXXXX";
    write_fixture(invalid_object_path, object, object_size);
    char invalid_output_path[] = "/tmp/nature-ld-pointer-output-XXXXXX";
    int invalid_output_fd = mkstemp(invalid_output_path);
    assert(invalid_output_fd >= 0);
    assert(close(invalid_output_fd) == 0);
    unlink(invalid_output_path);
    ld_options_init(&options);
    options.output_path = invalid_output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, invalid_object_path) == LD_OK);
    assert(ld_link(&options) == LD_RELOCATION_ERROR);
    assert(access(invalid_output_path, F_OK) != 0);
    ld_options_deinit(&options);
    unlink(invalid_object_path);
    free(object);
    unlink(object_path);
    unlink(output_path);
}

static void test_indirect_symbol_alias(void) {
    static const char strings[] = "\0_main\0_alias\0";
    size_t segment_size = sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t symbol_offset = text_offset + sizeof(uint32_t);
    size_t strings_offset = symbol_offset + 2U * sizeof(ld_nlist_64_t);
    size_t object_size = strings_offset + sizeof(strings);
    uint8_t *object = calloc(1, object_size);
    assert(object != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 2U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(object, &header, sizeof(header));

    ld_segment_command_64_t segment = {0};
    segment.cmd = LD_LC_SEGMENT_64;
    segment.cmdsize = (uint32_t) segment_size;
    memcpy(segment.segname, "__TEXT", sizeof("__TEXT") - 1U);
    segment.vmsize = sizeof(uint32_t);
    segment.fileoff = text_offset;
    segment.filesize = sizeof(uint32_t);
    segment.maxprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.initprot = LD_VM_PROT_READ | LD_VM_PROT_EXECUTE;
    segment.nsects = 1U;
    size_t cursor = sizeof(header);
    memcpy(object + cursor, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = sizeof(uint32_t);
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + cursor + sizeof(segment), &section, sizeof(section));

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 2U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    cursor += segment_size;
    memcpy(object + cursor, &symtab, sizeof(symtab));

    uint32_t ret = 0xd65f03c0U;
    memcpy(object + text_offset, &ret, sizeof(ret));
    ld_nlist_64_t symbols[2] = {
            {.n_strx = 1U, .n_type = LD_N_SECT | LD_N_EXT, .n_sect = 1U},
            {.n_strx = 7U, .n_type = LD_N_INDR | LD_N_EXT, .n_value = 1U},
    };
    memcpy(object + symbol_offset, symbols, sizeof(symbols));
    memcpy(object + strings_offset, strings, sizeof(strings));

    char object_path[] = "/tmp/nature-ld-alias-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    char output_path[] = "/tmp/nature-ld-alias-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.entry_symbol = "alias";
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    ld_options_deinit(&options);

    int fd = open(output_path, O_RDONLY);
    assert(fd >= 0);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size > 0);
    uint8_t *image = malloc((size_t) st.st_size);
    assert(image != NULL);
    assert(read(fd, image, (size_t) st.st_size) == st.st_size);
    assert(close(fd) == 0);

    const ld_mach_header_64_t *output_header = (const ld_mach_header_64_t *) image;
    const ld_section_64_t *text = NULL;
    const ld_entry_point_command_t *entry = NULL;
    const ld_symtab_command_t *output_symtab = NULL;
    cursor = sizeof(*output_header);
    for (uint32_t command_index = 0; command_index < output_header->ncmds; command_index++) {
        const ld_load_command_t *command = (const ld_load_command_t *) (image + cursor);
        assert(command->cmdsize >= sizeof(*command) &&
               cursor + command->cmdsize <= (size_t) st.st_size);
        if (command->cmd == LD_LC_SEGMENT_64) {
            const ld_segment_command_64_t *output_segment =
                    (const ld_segment_command_64_t *) command;
            const ld_section_64_t *sections =
                    (const ld_section_64_t *) (output_segment + 1);
            for (uint32_t i = 0; i < output_segment->nsects; i++) {
                if (strncmp(sections[i].sectname, "__text", 16) == 0) text = &sections[i];
            }
        } else if (command->cmd == LD_LC_MAIN) {
            entry = (const ld_entry_point_command_t *) command;
        } else if (command->cmd == LD_LC_SYMTAB) {
            output_symtab = (const ld_symtab_command_t *) command;
        }
        cursor += command->cmdsize;
    }
    assert(text != NULL && entry != NULL && output_symtab != NULL);
    assert(entry->entryoff == text->offset);
    assert(output_symtab->symoff +
                   (uint64_t) output_symtab->nsyms * sizeof(ld_nlist_64_t) <=
           (uint64_t) st.st_size);
    assert(output_symtab->stroff + output_symtab->strsize <= (uint64_t) st.st_size);
    const ld_nlist_64_t *output_symbols =
            (const ld_nlist_64_t *) (image + output_symtab->symoff);
    const char *output_strings = (const char *) image + output_symtab->stroff;
    uint64_t main_value = 0, alias_value = 0;
    for (uint32_t i = 0; i < output_symtab->nsyms; i++) {
        assert(output_symbols[i].n_strx < output_symtab->strsize);
        const char *name = output_strings + output_symbols[i].n_strx;
        if (strcmp(name, "_main") == 0) main_value = output_symbols[i].n_value;
        if (strcmp(name, "_alias") == 0) alias_value = output_symbols[i].n_value;
    }
    assert(main_value == text->addr);
    assert(alias_value == main_value);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

int main(void) {
    ld_options_init(NULL);
    ld_options_deinit(NULL);
    test_compact_unwind_regular_page();
    test_arm64_branch_island();
    test_indirect_symbol_alias();
    test_binary_dylib_reexport();
    test_framework_path_reexport();
    static const uint8_t expected_sha256[] = {
            0xba,
            0x78,
            0x16,
            0xbf,
            0x8f,
            0x01,
            0xcf,
            0xea,
            0x41,
            0x41,
            0x40,
            0xde,
            0x5d,
            0xae,
            0x22,
            0x23,
            0xb0,
            0x03,
            0x61,
            0xa3,
            0x96,
            0x17,
            0x7a,
            0x9c,
            0xb4,
            0x10,
            0xff,
            0x61,
            0xf2,
            0x00,
            0x15,
            0xad,
    };
    uint8_t digest[32];
    ld_sha256("abc", 3, digest);
    assert(memcmp(digest, expected_sha256, sizeof(digest)) == 0);
    static const uint8_t expected_empty_sha256[] = {
            0xe3,
            0xb0,
            0xc4,
            0x42,
            0x98,
            0xfc,
            0x1c,
            0x14,
            0x9a,
            0xfb,
            0xf4,
            0xc8,
            0x99,
            0x6f,
            0xb9,
            0x24,
            0x27,
            0xae,
            0x41,
            0xe4,
            0x64,
            0x9b,
            0x93,
            0x4c,
            0xa4,
            0x95,
            0x99,
            0x1b,
            0x78,
            0x52,
            0xb8,
            0x55,
    };
    ld_sha256("", 0, digest);
    assert(memcmp(digest, expected_empty_sha256, sizeof(digest)) == 0);

    test_tbd_objc_exports();
    test_sdk_appkit_objc_exports();

    ld_options_t options;
    ld_options_init(&options);

    assert(ld_parse_flags(&options,
                           "-framework Cocoa -F '/Library/Frameworks' -L\"/tmp/lib dir\" "
                           "-lz /tmp/input.o") == LD_OK);
    assert(options.frameworks.count == 1);
    assert(strcmp(options.frameworks.items[0], "Cocoa") == 0);
    assert(options.framework_paths.count == 1);
    assert(strcmp(options.framework_paths.items[0], "/Library/Frameworks") == 0);
    assert(options.library_paths.count == 1);
    assert(strcmp(options.library_paths.items[0], "/tmp/lib dir") == 0);
    assert(options.libraries.count == 1);
    assert(strcmp(options.libraries.items[0], "z") == 0);
    assert(options.inputs.count == 1);
    assert(strcmp(options.inputs.items[0], "/tmp/input.o") == 0);

    assert(ld_parse_flags(&options, "-F /tmp/Framework\\ dir -L'/tmp/lib'\" suffix\" -l System") == LD_OK);
    assert(options.framework_paths.count == 2);
    assert(strcmp(options.framework_paths.items[1], "/tmp/Framework dir") == 0);
    assert(options.library_paths.count == 2);
    assert(strcmp(options.library_paths.items[1], "/tmp/lib suffix") == 0);
    assert(options.libraries.count == 2);
    assert(strcmp(options.libraries.items[1], "System") == 0);

    assert(ld_parse_flags(&options, "-L '/tmp/a\\b' -L\"/tmp/c\\q\"") == LD_OK);
    assert(options.library_paths.count == 4);
    assert(strcmp(options.library_paths.items[2], "/tmp/a\\b") == 0);
    assert(strcmp(options.library_paths.items[3], "/tmp/c\\q") == 0);

    diagnostic_capture_t capture = {0};
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_parse_flags(&options, "-unknown") == LD_UNSUPPORTED);
    assert(capture.count == 1);
    assert(strstr(capture.message, "-unknown") != NULL);
    assert(ld_parse_flags(&options, "-framework") == LD_INVALID_ARGUMENT);
    assert(strstr(capture.message, "requires") != NULL);
    assert(ld_parse_flags(&options, "'unterminated") == LD_INVALID_ARGUMENT);
    assert(strstr(capture.message, "malformed") != NULL);
    assert(ld_parse_flags(&options, "/tmp/trailing\\") == LD_INVALID_ARGUMENT);
    assert(ld_parse_flags(&options, "''") == LD_INVALID_ARGUMENT);

    static const uint8_t truncated_macho[] = {0xcf, 0xfa, 0xed, 0xfe};
    char macho_path[] = "/tmp/nature-ld-macho-XXXXXX";
    write_fixture(macho_path, truncated_macho, sizeof(truncated_macho));
    expect_invalid_input(macho_path, LD_INVALID_INPUT, "truncated");
    unlink(macho_path);

    size_t aligned_segment_size = sizeof(ld_segment_command_64_t) +
                                  sizeof(ld_section_64_t);
    size_t aligned_object_size = sizeof(ld_mach_header_64_t) +
                                 aligned_segment_size;
    uint8_t *aligned_object = calloc(1, aligned_object_size);
    assert(aligned_object != NULL);
    ld_mach_header_64_t aligned_header = {0};
    aligned_header.magic = LD_MH_MAGIC_64;
    aligned_header.cputype = LD_CPU_TYPE_ARM64;
    aligned_header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    aligned_header.filetype = LD_MH_OBJECT;
    aligned_header.ncmds = 1U;
    aligned_header.sizeofcmds = (uint32_t) aligned_segment_size;
    memcpy(aligned_object, &aligned_header, sizeof(aligned_header));
    ld_segment_command_64_t aligned_segment = {0};
    aligned_segment.cmd = LD_LC_SEGMENT_64;
    aligned_segment.cmdsize = (uint32_t) aligned_segment_size;
    aligned_segment.nsects = 1U;
    memcpy(aligned_object + sizeof(aligned_header), &aligned_segment,
           sizeof(aligned_segment));
    ld_section_64_t over_aligned_section = {0};
    memcpy(over_aligned_section.segname, "__DATA", sizeof("__DATA") - 1U);
    memcpy(over_aligned_section.sectname, "__data", sizeof("__data") - 1U);
    over_aligned_section.align = 21U;
    memcpy(aligned_object + sizeof(aligned_header) + sizeof(aligned_segment),
           &over_aligned_section, sizeof(over_aligned_section));
    char aligned_object_path[] = "/tmp/nature-ld-align-XXXXXX";
    write_fixture(aligned_object_path, aligned_object, aligned_object_size);
    free(aligned_object);
    expect_invalid_input(aligned_object_path, LD_UNSUPPORTED,
                         "alignment exponent 21");
    unlink(aligned_object_path);

    uint8_t malformed_archive[68];
    memset(malformed_archive, ' ', sizeof(malformed_archive));
    memcpy(malformed_archive, "!<arch>\n", 8);
    memcpy(malformed_archive + 8 + 48, "abc       ", 10);
    memcpy(malformed_archive + 8 + 58, "`\n", 2);
    char archive_path[] = "/tmp/nature-ld-archive-XXXXXX";
    write_fixture(archive_path, malformed_archive, sizeof(malformed_archive));
    expect_invalid_input(archive_path, LD_INVALID_INPUT, "member size");
    unlink(archive_path);

    uint8_t missing_archive_padding[8 + 60 + 1];
    memset(missing_archive_padding, ' ', sizeof(missing_archive_padding));
    memcpy(missing_archive_padding, "!<arch>\n", 8);
    missing_archive_padding[8] = '/';
    missing_archive_padding[8 + 48] = '1';
    memcpy(missing_archive_padding + 8 + 58, "`\n", 2);
    missing_archive_padding[8 + 60] = 0;
    char missing_padding_path[] = "/tmp/nature-ld-archive-padding-XXXXXX";
    write_fixture(missing_padding_path, missing_archive_padding, sizeof(missing_archive_padding));
    expect_invalid_input(missing_padding_path, LD_INVALID_INPUT, "alignment padding");
    unlink(missing_padding_path);

    const char *long_member = "very_long_archive_member.o";
    size_t long_member_size = strlen(long_member);
    size_t long_payload_size = long_member_size + sizeof(truncated_macho);
    uint8_t long_archive[8 + 60 + 64];
    assert(long_payload_size <= 64);
    memset(long_archive, ' ', sizeof(long_archive));
    memcpy(long_archive, "!<arch>\n", 8);
    char decimal[32];
    int decimal_size = snprintf(decimal, sizeof(decimal), "#1/%zu", long_member_size);
    assert(decimal_size > 0 && decimal_size <= 16);
    memcpy(long_archive + 8, decimal, (size_t) decimal_size);
    decimal_size = snprintf(decimal, sizeof(decimal), "%zu", long_payload_size);
    assert(decimal_size > 0 && decimal_size <= 10);
    memcpy(long_archive + 8 + 48, decimal, (size_t) decimal_size);
    memcpy(long_archive + 8 + 58, "`\n", 2);
    memcpy(long_archive + 8 + 60, long_member, long_member_size);
    memcpy(long_archive + 8 + 60 + long_member_size, truncated_macho, sizeof(truncated_macho));
    char long_archive_path[] = "/tmp/nature-ld-long-archive-XXXXXX";
    write_fixture(long_archive_path, long_archive, 8 + 60 + long_payload_size);
    expect_invalid_input(long_archive_path, LD_INVALID_INPUT, long_member);
    unlink(long_archive_path);

    static const uint8_t wrong_arch_fat[] = {
            0xca,
            0xfe,
            0xba,
            0xbe,
            0x00,
            0x00,
            0x00,
            0x01,
            0x01,
            0x00,
            0x00,
            0x07,
            0x00,
            0x00,
            0x00,
            0x03,
            0x00,
            0x00,
            0x00,
            0x1c,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
    };
    char fat_path[] = "/tmp/nature-ld-fat-XXXXXX";
    write_fixture(fat_path, wrong_arch_fat, sizeof(wrong_arch_fat));
    expect_invalid_input(fat_path, LD_UNSUPPORTED, "no arm64 slice");
    unlink(fat_path);

    options.output_path = "/tmp/unsupported-ld-output";
    options.os = LD_OS_LINUX;
    assert(ld_link(&options) == LD_UNSUPPORTED);

    ld_options_deinit(&options);
    return 0;
}
