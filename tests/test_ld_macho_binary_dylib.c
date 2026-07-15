#include "test_ld_macho_common.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    uint8_t *bytes;
    size_t size;
} dylib_fixture_t;

static dylib_fixture_t make_dylib_with_symbols(bool with_trie);

static uint32_t dylib_string_command_size(size_t header_size,
                                          const char *value) {
    return (uint32_t) ((header_size + strlen(value) + 1U + 7U) & ~7U);
}

static dylib_fixture_t make_export_trie_dylib(const uint8_t *trie,
                                              size_t trie_size) {
    static const char install_name[] = "/usr/lib/libTrieFixture.dylib";
    uint32_t id_size = dylib_string_command_size(
            sizeof(ld_dylib_command_t), install_name);
    uint32_t command_size = id_size + sizeof(ld_linkedit_data_command_t);
    uint32_t trie_offset =
            (uint32_t) sizeof(ld_mach_header_64_t) + command_size;
    assert(trie_size <= UINT32_MAX - trie_offset);
    size_t size = trie_offset + trie_size;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    header.ncmds = 2U;
    header.sizeofcmds = command_size;
    memcpy(bytes, &header, sizeof(header));

    size_t offset = sizeof(header);
    ld_dylib_command_t id = {0};
    id.cmd = LD_LC_ID_DYLIB;
    id.cmdsize = id_size;
    id.name_offset = sizeof(id);
    memcpy(bytes + offset, &id, sizeof(id));
    memcpy(bytes + offset + sizeof(id), install_name, sizeof(install_name));
    offset += id_size;

    ld_linkedit_data_command_t exports = {0};
    exports.cmd = LD_LC_DYLD_EXPORTS_TRIE;
    exports.cmdsize = sizeof(exports);
    exports.dataoff = trie_offset;
    exports.datasize = (uint32_t) trie_size;
    memcpy(bytes + offset, &exports, sizeof(exports));
    memcpy(bytes + trie_offset, trie, trie_size);
    return (dylib_fixture_t) {.bytes = bytes, .size = size};
}

static dylib_fixture_t make_reexport_dylib(const char *install_name,
                                           const char *dependency,
                                           const char *rpath) {
    uint32_t id_size = dylib_string_command_size(
            sizeof(ld_dylib_command_t), install_name);
    uint32_t dependency_size = dependency
                                       ? dylib_string_command_size(
                                                 sizeof(ld_dylib_command_t),
                                                 dependency)
                                       : 0U;
    uint32_t rpath_size = rpath ? dylib_string_command_size(
                                          sizeof(ld_rpath_command_t), rpath)
                                : 0U;
    uint32_t command_size = id_size + dependency_size + rpath_size;
    size_t size = sizeof(ld_mach_header_64_t) + command_size;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    header.ncmds = 1U + (dependency ? 1U : 0U) + (rpath ? 1U : 0U);
    header.sizeofcmds = command_size;
    memcpy(bytes, &header, sizeof(header));

    size_t offset = sizeof(header);
    ld_dylib_command_t id = {0};
    id.cmd = LD_LC_ID_DYLIB;
    id.cmdsize = id_size;
    id.name_offset = sizeof(id);
    memcpy(bytes + offset, &id, sizeof(id));
    memcpy(bytes + offset + sizeof(id), install_name,
           strlen(install_name) + 1U);
    offset += id_size;

    if (dependency) {
        ld_dylib_command_t reexport = {0};
        reexport.cmd = LD_LC_REEXPORT_DYLIB;
        reexport.cmdsize = dependency_size;
        reexport.name_offset = sizeof(reexport);
        memcpy(bytes + offset, &reexport, sizeof(reexport));
        memcpy(bytes + offset + sizeof(reexport), dependency,
               strlen(dependency) + 1U);
        offset += dependency_size;
    }
    if (rpath) {
        ld_rpath_command_t command = {0};
        command.cmd = LD_LC_RPATH;
        command.cmdsize = rpath_size;
        command.path_offset = sizeof(command);
        memcpy(bytes + offset, &command, sizeof(command));
        memcpy(bytes + offset + sizeof(command), rpath, strlen(rpath) + 1U);
    }
    return (dylib_fixture_t) {.bytes = bytes, .size = size};
}

static ld_symbol_t *add_unresolved_symbol(ld_context_t *ctx,
                                          const char *name) {
    ld_symbol_t *symbol = calloc(1, sizeof(*symbol));
    assert(symbol != NULL);
    symbol->name = strdup(name);
    assert(symbol->name != NULL);
    symbol->kind = LD_SYMBOL_UNDEFINED;
    HASH_ADD_KEYPTR(hh, ctx->symbols, symbol->name, strlen(symbol->name),
                    symbol);
    return symbol;
}

static void remove_test_symbol(ld_context_t *ctx, ld_symbol_t *symbol) {
    HASH_DEL(ctx->symbols, symbol);
    free(symbol->name);
    free(symbol);
}

static void resolve_fixture_dependency(const char *parent_path,
                                       const char *child_path,
                                       const char *dependency,
                                       const char *rpath,
                                       const char *output_path,
                                       const char *option_rpath,
                                       const char *library_path) {
    dylib_fixture_t parent = make_reexport_dylib(
            "/usr/lib/libParentFixture.dylib", dependency, rpath);
    dylib_fixture_t child = make_dylib_with_symbols(true);
    test_ld_write_named_fixture(parent_path, parent.bytes, parent.size);
    test_ld_write_named_fixture(child_path, child.bytes, child.size);

    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    if (option_rpath) assert(ld_add_rpath(&options, option_rpath) == LD_OK);
    if (library_path) {
        assert(ld_add_library_path(&options, library_path) == LD_OK);
    }
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, parent_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    if (rpath) {
        assert(ctx.dylibs.items[0].rpath_count == 1U);
        assert(strcmp(ctx.dylibs.items[0].rpaths[0], rpath) == 0);
    }
    ld_symbol_t *unresolved = add_unresolved_symbol(&ctx, "_public");
    assert(ld_resolve_reexport_libraries(&ctx) == LD_OK);
    assert(ctx.dylibs.count == 2U);
    assert(test_ld_dylib_has_symbol(ctx.dylibs.items[0].exports,
                                    ctx.dylibs.items[0].export_count,
                                    "_public"));
    remove_test_symbol(&ctx, unresolved);
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(child_path) == 0);
    assert(unlink(parent_path) == 0);
    free(child.bytes);
    free(parent.bytes);
}

static dylib_fixture_t make_dylib_with_symbols(bool with_trie) {
    static const char install_name[] = "/usr/lib/libFixture.dylib";
    static const char strings[] = "\0_private\0_public\0_hidden\0_fallback\0";
    static const uint8_t export_trie[] = {
            0x00,
            0x01,
            '_',
            'p',
            'u',
            'b',
            'l',
            'i',
            'c',
            0x00,
            0x0b,
            0x02,
            0x00,
            0x00,
            0x00,
    };
    uint32_t id_size = (uint32_t) ((sizeof(ld_dylib_command_t) +
                                    sizeof(install_name) + 7U) &
                                   ~7U);
    uint32_t command_count = with_trie ? 4U : 3U;
    uint32_t command_size = id_size + sizeof(ld_symtab_command_t) +
                            sizeof(ld_dysymtab_command_t) +
                            (with_trie ? sizeof(ld_linkedit_data_command_t) : 0U);
    uint32_t symoff = (uint32_t) sizeof(ld_mach_header_64_t) + command_size;
    uint32_t nsyms = 2U;
    uint32_t stroff = symoff + nsyms * sizeof(ld_nlist_64_t);
    uint32_t trieoff = stroff + (uint32_t) sizeof(strings);
    size_t size = trieoff + (with_trie ? sizeof(export_trie) : 0U);
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    header.ncmds = command_count;
    header.sizeofcmds = command_size;
    memcpy(bytes, &header, sizeof(header));

    size_t offset = sizeof(header);
    ld_dylib_command_t id = {0};
    id.cmd = LD_LC_ID_DYLIB;
    id.cmdsize = id_size;
    id.name_offset = sizeof(id);
    memcpy(bytes + offset, &id, sizeof(id));
    memcpy(bytes + offset + sizeof(id), install_name, sizeof(install_name));
    offset += id_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = symoff;
    symtab.nsyms = nsyms;
    symtab.stroff = stroff;
    symtab.strsize = sizeof(strings);
    memcpy(bytes + offset, &symtab, sizeof(symtab));
    offset += sizeof(symtab);

    ld_dysymtab_command_t dysym = {0};
    dysym.cmd = LD_LC_DYSYMTAB;
    dysym.cmdsize = sizeof(dysym);
    dysym.iextdefsym = 0U;
    dysym.nextdefsym = nsyms;
    memcpy(bytes + offset, &dysym, sizeof(dysym));
    offset += sizeof(dysym);

    if (with_trie) {
        ld_linkedit_data_command_t exports = {0};
        exports.cmd = LD_LC_DYLD_EXPORTS_TRIE;
        exports.cmdsize = sizeof(exports);
        exports.dataoff = trieoff;
        exports.datasize = sizeof(export_trie);
        memcpy(bytes + offset, &exports, sizeof(exports));
    }

    ld_nlist_64_t first = {0};
    first.n_strx = with_trie ? 1U : 18U;
    first.n_type = LD_N_SECT | LD_N_EXT |
                   (with_trie ? 0U : LD_N_PEXT);
    first.n_sect = 1U;
    memcpy(bytes + symoff, &first, sizeof(first));
    ld_nlist_64_t second = {0};
    second.n_strx = with_trie ? 10U : 26U;
    second.n_type = LD_N_SECT | LD_N_EXT;
    second.n_sect = 1U;
    memcpy(bytes + symoff + sizeof(first), &second, sizeof(second));
    memcpy(bytes + stroff, strings, sizeof(strings));
    if (with_trie) memcpy(bytes + trieoff, export_trie, sizeof(export_trie));
    return (dylib_fixture_t) {.bytes = bytes, .size = size};
}

static void test_export_trie_is_authoritative(void) {
    dylib_fixture_t fixture = make_dylib_with_symbols(true);
    char path[] = "/tmp/nature-ld-dylib-trie-XXXXXX";
    test_ld_write_fixture(path, fixture.bytes, fixture.size);
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_public"));
    assert(!test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                     "_private"));
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(path);
    free(fixture.bytes);
}

static void test_renamed_reexport_uses_imported_name(void) {
    static const uint8_t renamed_trie[] =
            "\x00\x01_alias\x00\x0a"
            "\x0b\x08\x01_renamed\x00\x00";
    dylib_fixture_t fixture = make_export_trie_dylib(
            renamed_trie, sizeof(renamed_trie) - 1U);
    char path[] = "/tmp/nature-ld-dylib-renamed-reexport-XXXXXX";
    test_ld_write_fixture(path, fixture.bytes, fixture.size);

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_renamed"));
    assert(!test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                     "_alias"));
    assert(dylib->symbol_count == 1U);
    assert(strcmp(dylib->symbols[0].name, "_renamed") == 0);
    assert(dylib->symbols[0].reexport);

    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(path) == 0);
    free(fixture.bytes);
}

static void test_reexport_without_imported_name_uses_prefix(void) {
    static const uint8_t prefix_trie[] =
            "\x00\x01_alias\x00\x0a"
            "\x03\x08\x01\x00\x00";
    dylib_fixture_t fixture = make_export_trie_dylib(
            prefix_trie, sizeof(prefix_trie) - 1U);
    char path[] = "/tmp/nature-ld-dylib-prefix-reexport-XXXXXX";
    test_ld_write_fixture(path, fixture.bytes, fixture.size);

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_alias"));
    assert(dylib->symbol_count == 1U);
    assert(strcmp(dylib->symbols[0].name, "_alias") == 0);

    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(path) == 0);
    free(fixture.bytes);
}

static void test_export_trie_allows_shared_child(void) {
    static const uint8_t shared_child_trie[] =
            "\x00\x02_a\x00\x0a_b\x00\x0a"
            "\x02\x00\x00\x00";
    dylib_fixture_t fixture = make_export_trie_dylib(
            shared_child_trie, sizeof(shared_child_trie) - 1U);
    char path[] = "/tmp/nature-ld-dylib-shared-trie-node-XXXXXX";
    test_ld_write_fixture(path, fixture.bytes, fixture.size);

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_a"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_b"));
    assert(dylib->symbol_count == 2U);

    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(path) == 0);
    free(fixture.bytes);
}

static void test_export_trie_rejects_cycle_and_truncation(void) {
    static const uint8_t cycle_trie[] =
            "\x00\x01_loop\x00\x00";
    dylib_fixture_t cycle = make_export_trie_dylib(
            cycle_trie, sizeof(cycle_trie) - 1U);
    char cycle_path[] = "/tmp/nature-ld-dylib-trie-cycle-XXXXXX";
    test_ld_write_fixture(cycle_path, cycle.bytes, cycle.size);
    test_ld_expect_invalid_input(cycle_path, LD_INVALID_INPUT,
                                 "export trie cycle");
    assert(unlink(cycle_path) == 0);
    free(cycle.bytes);

    static const uint8_t truncated_trie[] = {0x00};
    dylib_fixture_t truncated = make_export_trie_dylib(
            truncated_trie, sizeof(truncated_trie));
    char truncated_path[] = "/tmp/nature-ld-dylib-trie-truncated-XXXXXX";
    test_ld_write_fixture(truncated_path, truncated.bytes, truncated.size);
    test_ld_expect_invalid_input(truncated_path, LD_INVALID_INPUT,
                                 "truncated export trie node");
    assert(unlink(truncated_path) == 0);
    free(truncated.bytes);
}

static void test_dysymtab_fallback_filters_private_externals(void) {
    dylib_fixture_t fixture = make_dylib_with_symbols(false);
    char path[] = "/tmp/nature-ld-dylib-fallback-XXXXXX";
    test_ld_write_fixture(path, fixture.bytes, fixture.size);
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                    "_fallback"));
    assert(!test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                                     "_hidden"));
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(path);
    free(fixture.bytes);
}

static void test_missing_dylib_id_is_malformed(void) {
    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = LD_CPU_SUBTYPE_ARM64_ALL;
    header.filetype = LD_MH_DYLIB;
    char path[] = "/tmp/nature-ld-dylib-no-id-XXXXXX";
    test_ld_write_fixture(path, &header, sizeof(header));
    test_ld_expect_invalid_input(path, LD_INVALID_INPUT,
                                 "missing LC_ID_DYLIB");
    unlink(path);
}

static void test_rpath_flag_parsing(void) {
    ld_options_t options;
    ld_options_init(&options);
    assert(ld_parse_flags(&options, "-rpath '/tmp/runtime path'") == LD_OK);
    assert(options.rpaths.count == 1U);
    assert(strcmp(options.rpaths.items[0], "/tmp/runtime path") == 0);
    assert(ld_add_rpath(&options, "/tmp/runtime path") == LD_OK);
    assert(options.rpaths.count == 1U);

    /* Zig and ld64 spell this option as two arguments.  A joined form is not
       silently reinterpreted as a path. */
    test_ld_diagnostic_capture_t capture = {0};
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_parse_flags(&options, "-rpath/tmp/joined") == LD_UNSUPPORTED);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "-rpath/tmp/joined") != NULL);
    ld_options_deinit(&options);
}

static void test_output_writes_rpath_load_command(void) {
    static const char strings[] = "\0_main\0";
    size_t segment_size =
            sizeof(ld_segment_command_64_t) + sizeof(ld_section_64_t);
    size_t commands_size = segment_size + sizeof(ld_symtab_command_t);
    size_t text_offset = sizeof(ld_mach_header_64_t) + commands_size;
    size_t symbol_offset = text_offset + sizeof(uint32_t);
    size_t strings_offset = symbol_offset + sizeof(ld_nlist_64_t);
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
    size_t offset = sizeof(header);
    memcpy(object + offset, &segment, sizeof(segment));

    ld_section_64_t section = {0};
    memcpy(section.sectname, "__text", sizeof("__text") - 1U);
    memcpy(section.segname, "__TEXT", sizeof("__TEXT") - 1U);
    section.size = sizeof(uint32_t);
    section.offset = (uint32_t) text_offset;
    section.align = 2U;
    section.flags = LD_S_ATTR_PURE_INSTRUCTIONS |
                    LD_S_ATTR_SOME_INSTRUCTIONS;
    memcpy(object + offset + sizeof(segment), &section, sizeof(section));

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) symbol_offset;
    symtab.nsyms = 1U;
    symtab.stroff = (uint32_t) strings_offset;
    symtab.strsize = sizeof(strings);
    memcpy(object + offset + segment_size, &symtab, sizeof(symtab));
    uint32_t ret = 0xd65f03c0U;
    memcpy(object + text_offset, &ret, sizeof(ret));
    ld_nlist_64_t main_symbol = {
            .n_strx = 1U,
            .n_type = LD_N_SECT | LD_N_EXT,
            .n_sect = 1U,
    };
    memcpy(object + symbol_offset, &main_symbol, sizeof(main_symbol));
    memcpy(object + strings_offset, strings, sizeof(strings));

    char object_path[] = "/tmp/nature-ld-rpath-object-XXXXXX";
    test_ld_write_fixture(object_path, object, object_size);
    char output_path[] = "/tmp/nature-ld-rpath-output-XXXXXX";
    int output_fd = mkstemp(output_path);
    assert(output_fd >= 0);
    assert(close(output_fd) == 0);
    assert(unlink(output_path) == 0);

    static const char output_rpath[] = "@executable_path/Frameworks";
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output_path;
    options.adhoc_codesign = false;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_add_rpath(&options, output_rpath) == LD_OK);
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
    ld_mach_header_64_t output_header;
    memcpy(&output_header, image, sizeof(output_header));
    assert(output_header.magic == LD_MH_MAGIC_64);
    size_t command_offset = sizeof(output_header);
    size_t rpath_count = 0;
    for (uint32_t i = 0; i < output_header.ncmds; i++) {
        assert(command_offset <= (size_t) st.st_size &&
               sizeof(ld_load_command_t) <=
                       (size_t) st.st_size - command_offset);
        ld_load_command_t command;
        memcpy(&command, image + command_offset, sizeof(command));
        assert(command.cmdsize >= sizeof(command) &&
               command.cmdsize <= (size_t) st.st_size - command_offset);
        if (command.cmd == LD_LC_RPATH) {
            assert(command.cmdsize >= sizeof(ld_rpath_command_t));
            ld_rpath_command_t rpath;
            memcpy(&rpath, image + command_offset, sizeof(rpath));
            assert(rpath.path_offset >= sizeof(rpath) &&
                   rpath.path_offset < rpath.cmdsize);
            const char *path =
                    (const char *) image + command_offset + rpath.path_offset;
            size_t available = rpath.cmdsize - rpath.path_offset;
            assert(memchr(path, '\0', available) != NULL);
            assert(strcmp(path, output_rpath) == 0);
            rpath_count++;
        }
        command_offset += command.cmdsize;
    }
    assert(rpath_count == 1U);

    free(image);
    free(object);
    assert(unlink(output_path) == 0);
    assert(unlink(object_path) == 0);
}

static void test_binary_dylib_rpath_resolution(void) {
    char root[] = "/tmp/nature-ld-binary-rpath-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char search[PATH_MAX], parent[PATH_MAX], child[PATH_MAX], output[PATH_MAX];
    assert(snprintf(search, sizeof(search), "%s/reexports", root) > 0);
    assert(mkdir(search, 0700) == 0);
    assert(snprintf(parent, sizeof(parent), "%s/Parent.dylib", root) > 0);
    assert(snprintf(child, sizeof(child), "%s/libChild.dylib", search) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    resolve_fixture_dependency(parent, child, "@rpath/libChild.dylib",
                               "@loader_path/reexports", output, NULL, NULL);
    assert(rmdir(search) == 0);
    assert(rmdir(root) == 0);
}

static void test_binary_dylib_loader_path_resolution(void) {
    char root[] = "/tmp/nature-ld-binary-loader-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char parent[PATH_MAX], child[PATH_MAX], output[PATH_MAX];
    assert(snprintf(parent, sizeof(parent), "%s/Parent.dylib", root) > 0);
    assert(snprintf(child, sizeof(child), "%s/libChild.dylib", root) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    resolve_fixture_dependency(parent, child,
                               "@loader_path/libChild.dylib", NULL, output,
                               NULL, NULL);
    assert(rmdir(root) == 0);
}

static void test_binary_dylib_executable_path_resolution(void) {
    char root[] = "/tmp/nature-ld-binary-executable-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char libraries[PATH_MAX], executable[PATH_MAX];
    char parent[PATH_MAX], child[PATH_MAX], output[PATH_MAX];
    assert(snprintf(libraries, sizeof(libraries), "%s/libraries", root) > 0);
    assert(snprintf(executable, sizeof(executable), "%s/bin", root) > 0);
    assert(mkdir(libraries, 0700) == 0);
    assert(mkdir(executable, 0700) == 0);
    assert(snprintf(parent, sizeof(parent), "%s/Parent.dylib", libraries) > 0);
    assert(snprintf(child, sizeof(child), "%s/libChild.dylib", executable) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", executable) > 0);
    resolve_fixture_dependency(parent, child,
                               "@executable_path/libChild.dylib", NULL,
                               output, NULL, NULL);
    assert(rmdir(executable) == 0);
    assert(rmdir(libraries) == 0);
    assert(rmdir(root) == 0);
}

static void test_linker_rpath_resolution(void) {
    char root[] = "/tmp/nature-ld-option-rpath-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char search[PATH_MAX], parent[PATH_MAX], child[PATH_MAX], output[PATH_MAX];
    assert(snprintf(search, sizeof(search), "%s/search", root) > 0);
    assert(mkdir(search, 0700) == 0);
    assert(snprintf(parent, sizeof(parent), "%s/Parent.dylib", root) > 0);
    assert(snprintf(child, sizeof(child), "%s/libChild.dylib", search) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    resolve_fixture_dependency(parent, child, "@rpath/libChild.dylib", NULL,
                               output, search, NULL);
    assert(rmdir(search) == 0);
    assert(rmdir(root) == 0);
}

static void test_sdk_library_path_resolves_nested_install_name(void) {
    char root[] = "/tmp/nature-ld-sdk-library-path-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char usr[PATH_MAX], library[PATH_MAX], system[PATH_MAX];
    char parent[PATH_MAX], child[PATH_MAX], output[PATH_MAX];
    assert(snprintf(usr, sizeof(usr), "%s/usr", root) > 0);
    assert(snprintf(library, sizeof(library), "%s/lib", usr) > 0);
    assert(snprintf(system, sizeof(system), "%s/system", library) > 0);
    assert(mkdir(usr, 0700) == 0);
    assert(mkdir(library, 0700) == 0);
    assert(mkdir(system, 0700) == 0);
    assert(snprintf(parent, sizeof(parent), "%s/Parent.dylib", root) > 0);
    assert(snprintf(child, sizeof(child), "%s/libChild.dylib", system) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    resolve_fixture_dependency(
            parent, child, "/usr/lib/system/libChild.dylib", NULL, output,
            NULL, library);
    assert(rmdir(system) == 0);
    assert(rmdir(library) == 0);
    assert(rmdir(usr) == 0);
    assert(rmdir(root) == 0);
}

static void test_malformed_binary_dylib_rpaths(void) {
    static const char install_name[] = "/usr/lib/libBadRpath.dylib";
    static const char rpath[] = "@loader_path/dependencies";
    uint32_t id_size = dylib_string_command_size(
            sizeof(ld_dylib_command_t), install_name);
    size_t command_offset = sizeof(ld_mach_header_64_t) + id_size;

    dylib_fixture_t invalid_offset =
            make_reexport_dylib(install_name, NULL, rpath);
    ld_rpath_command_t *command =
            (ld_rpath_command_t *) (invalid_offset.bytes + command_offset);
    command->path_offset = command->cmdsize;
    char invalid_offset_path[] = "/tmp/nature-ld-rpath-offset-XXXXXX";
    test_ld_write_fixture(invalid_offset_path, invalid_offset.bytes,
                          invalid_offset.size);
    test_ld_expect_invalid_input(invalid_offset_path, LD_INVALID_INPUT,
                                 "LC_RPATH string offset");
    assert(unlink(invalid_offset_path) == 0);
    free(invalid_offset.bytes);

    dylib_fixture_t unterminated =
            make_reexport_dylib(install_name, NULL, rpath);
    command = (ld_rpath_command_t *) (unterminated.bytes + command_offset);
    memset(unterminated.bytes + command_offset + command->path_offset, 'x',
           command->cmdsize - command->path_offset);
    char unterminated_path[] = "/tmp/nature-ld-rpath-string-XXXXXX";
    test_ld_write_fixture(unterminated_path, unterminated.bytes,
                          unterminated.size);
    test_ld_expect_invalid_input(unterminated_path, LD_INVALID_INPUT,
                                 "unterminated or empty LC_RPATH");
    assert(unlink(unterminated_path) == 0);
    free(unterminated.bytes);

    dylib_fixture_t truncated =
            make_reexport_dylib(install_name, NULL, rpath);
    command = (ld_rpath_command_t *) (truncated.bytes + command_offset);
    command->cmdsize = sizeof(ld_load_command_t);
    ld_mach_header_64_t *header = (ld_mach_header_64_t *) truncated.bytes;
    header->sizeofcmds = id_size + command->cmdsize;
    char truncated_path[] = "/tmp/nature-ld-rpath-command-XXXXXX";
    test_ld_write_fixture(truncated_path, truncated.bytes, truncated.size);
    test_ld_expect_invalid_input(truncated_path, LD_INVALID_INPUT,
                                 "invalid LC_RPATH");
    assert(unlink(truncated_path) == 0);
    free(truncated.bytes);
}

static void test_missing_reexport_is_lazy_and_diagnostic(void) {
    char root[] = "/tmp/nature-ld-missing-reexport-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char parent_path[PATH_MAX], output[PATH_MAX];
    assert(snprintf(parent_path, sizeof(parent_path), "%s/Parent.dylib", root) >
           0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    dylib_fixture_t parent = make_reexport_dylib(
            "/usr/lib/libMissingParent.dylib",
            "@rpath/libMissingChild.dylib", "@loader_path/not-present");
    test_ld_write_named_fixture(parent_path, parent.bytes, parent.size);

    test_ld_diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output;
    options.diagnostic = test_ld_capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, parent_path) == LD_OK);
    assert(ld_resolve_reexport_libraries(&ctx) == LD_OK);
    assert(!ctx.dylibs.items[0].reexports_scanned);

    ld_symbol_t *weak = add_unresolved_symbol(&ctx, "_optional_symbol");
    weak->weak_ref = true;
    assert(ld_resolve_reexport_libraries(&ctx) == LD_OK);
    assert(!ctx.dylibs.items[0].reexports_scanned);
    remove_test_symbol(&ctx, weak);

    ld_symbol_t *unresolved = add_unresolved_symbol(&ctx, "_missing_symbol");
    assert(ld_resolve_reexport_libraries(&ctx) == LD_IO_ERROR);
    assert(capture.count == 1U);
    assert(strstr(capture.message, "@rpath/libMissingChild.dylib") != NULL);
    assert(strstr(capture.message, parent_path) != NULL);
    assert(strstr(capture.message, "checked paths") != NULL);
    assert(strstr(capture.message, "not-present/libMissingChild.tbd") != NULL);

    remove_test_symbol(&ctx, unresolved);
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(parent_path) == 0);
    assert(rmdir(root) == 0);
    free(parent.bytes);
}

static void test_reexport_cycle_is_bounded(void) {
    char root[] = "/tmp/nature-ld-reexport-cycle-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char a_path[PATH_MAX], b_path[PATH_MAX], output[PATH_MAX];
    assert(snprintf(a_path, sizeof(a_path), "%s/A.dylib", root) > 0);
    assert(snprintf(b_path, sizeof(b_path), "%s/B.dylib", root) > 0);
    assert(snprintf(output, sizeof(output), "%s/main", root) > 0);
    dylib_fixture_t a = make_reexport_dylib(
            "@loader_path/A.dylib", "@loader_path/B.dylib", NULL);
    dylib_fixture_t b = make_reexport_dylib(
            "@loader_path/B.dylib", "@loader_path/A.dylib", NULL);
    test_ld_write_named_fixture(a_path, a.bytes, a.size);
    test_ld_write_named_fixture(b_path, b.bytes, b.size);

    ld_options_t options;
    ld_options_init(&options);
    options.output_path = output;
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, a_path) == LD_OK);
    ld_symbol_t *unresolved = add_unresolved_symbol(&ctx, "_not_in_cycle");
    assert(ld_resolve_reexport_libraries(&ctx) == LD_OK);
    assert(ctx.dylibs.count == 2U);
    assert(ctx.files.count == 2U);
    assert(ctx.dylibs.items[0].reexports_scanned);
    assert(ctx.dylibs.items[1].reexports_scanned);

    remove_test_symbol(&ctx, unresolved);
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    assert(unlink(b_path) == 0);
    assert(unlink(a_path) == 0);
    assert(rmdir(root) == 0);
    free(b.bytes);
    free(a.bytes);
}

void test_ld_macho_binary_dylib(void) {
    test_export_trie_is_authoritative();
    test_renamed_reexport_uses_imported_name();
    test_reexport_without_imported_name_uses_prefix();
    test_export_trie_allows_shared_child();
    test_export_trie_rejects_cycle_and_truncation();
    test_dysymtab_fallback_filters_private_externals();
    test_missing_dylib_id_is_malformed();
    test_rpath_flag_parsing();
    test_output_writes_rpath_load_command();
    test_binary_dylib_rpath_resolution();
    test_binary_dylib_loader_path_resolution();
    test_binary_dylib_executable_path_resolution();
    test_linker_rpath_resolution();
    test_sdk_library_path_resolves_nested_install_name();
    test_malformed_binary_dylib_rpaths();
    test_missing_reexport_is_lazy_and_diagnostic();
    test_reexport_cycle_is_bounded();
}
