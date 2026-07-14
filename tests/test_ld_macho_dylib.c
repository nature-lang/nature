#include "test_ld_macho_common.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    test_ld_write_fixture(v4_path, tbd_v4, sizeof(tbd_v4) - 1U);

    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, v4_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    const ld_dylib_input_t *dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_NSApplication"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_METACLASS_$_NSApplication"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_NSQuotedClass"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_EHTYPE_$_NSException"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_IVAR_$_NSApplication._delegate"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_AddedForCompatibility"));
    assert(!test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                             "_HiddenCompatibility"));
    assert(!test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                             "_OBJC_CLASS_$_NSWrongArchitecture"));
    assert(test_ld_dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_CLASS_$_NSWeakClass"));
    assert(test_ld_dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_METACLASS_$_NSWeakClass"));
    assert(test_ld_dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                            "_OBJC_IVAR_$_NSWeakClass._value"));
    test_ld_parsed_context_deinit(&ctx);
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
    test_ld_write_fixture(v3_path, tbd_v3, sizeof(tbd_v3) - 1U);
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, v3_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    dylib = &ctx.dylibs.items[0];
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_CLASS_$_LegacyClass"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_METACLASS_$_LegacyClass"));
    assert(test_ld_dylib_has_symbol(dylib->exports, dylib->export_count,
                            "_OBJC_EHTYPE_$_LegacyException"));
    assert(!test_ld_dylib_has_symbol(dylib->weak_exports, dylib->weak_export_count,
                             "_OBJC_IVAR_$_WrongArchitecture._value"));
    test_ld_parsed_context_deinit(&ctx);
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
    test_ld_write_fixture(path, bytes, file_size);
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
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(path);

    /* MH_NO_REEXPORTED_DYLIBS makes LC_REEXPORT_DYLIB metadata inert.  Keep
       the command in the fixture to ensure the parser checks the header flag
       instead of blindly recording every reexport command. */
    ((ld_mach_header_64_t *) bytes)->flags = LD_MH_NO_REEXPORTED_DYLIBS;
    char no_reexport_path[] = "/tmp/nature-ld-no-reexport-dylib-XXXXXX";
    test_ld_write_fixture(no_reexport_path, bytes, file_size);
    ld_options_init(&options);
    memset(&ctx, 0, sizeof(ctx));
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, no_reexport_path) == LD_OK);
    assert(ctx.dylibs.count == 1U);
    assert(ctx.dylibs.items[0].reexport_count == 0U);
    test_ld_parsed_context_deinit(&ctx);
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
    test_ld_write_named_fixture(parent_path, parent_tbd, sizeof(parent_tbd) - 1U);
    test_ld_write_named_fixture(child_path, child_tbd, sizeof(child_tbd) - 1U);

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
    assert(test_ld_dylib_has_symbol(ctx.dylibs.items[0].exports, ctx.dylibs.items[0].export_count, "_child"));
    HASH_DEL(ctx.symbols, &unresolved);
    free(unresolved.name);

    test_ld_parsed_context_deinit(&ctx);
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
    assert(test_ld_dylib_has_symbol(appkit->exports, appkit->export_count,
                            "_OBJC_CLASS_$_NSApplication"));
    assert(test_ld_dylib_has_symbol(appkit->exports, appkit->export_count,
                            "_OBJC_METACLASS_$_NSApplication"));
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
}

void test_ld_macho_dylib(void) {
    test_tbd_objc_exports();
    test_binary_dylib_reexport();
    test_framework_path_reexport();
    test_sdk_appkit_objc_exports();
}
