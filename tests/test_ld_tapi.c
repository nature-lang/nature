#include "src/ld/ld_tapi.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const ld_tapi_symbol_t *find_symbol(const ld_tapi_stub_t *stub,
                                           const char *name) {
    for (size_t i = 0; i < stub->symbol_count; i++) {
        if (strcmp(stub->symbols[i].name, name) == 0) {
            return &stub->symbols[i];
        }
    }
    return NULL;
}

static bool has_reexport(const ld_tapi_stub_t *stub, const char *name) {
    for (size_t i = 0; i < stub->reexport_count; i++) {
        if (strcmp(stub->reexports[i], name) == 0) return true;
    }
    return false;
}

static void test_tapi_v4_structured_documents(void) {
    static const char source[] =
            "--- !tapi-tbd\n"
            "install-name: '/usr/lib/libStructured.dylib' # retained metadata\n"
            "compatibility-version: 2.3\n"
            "exports:\n"
            "  - symbols:\n"
            "      - '_hash#symbol' # the hash inside quotes is data\n"
            "      - _legacy_macosx\n"
            "    weak-symbols: [ _weak ]\n"
            "    absolute-symbols:\n"
            "      - _absolute\n"
            "    thread-local-symbols: [ _thread_local ]\n"
            "    objc-classes: [ StructuredClass ]\n"
            "    objc-ivars:\n"
            "      - StructuredClass.value\n"
            "    objc-eh-types: [ StructuredException ]\n"
            "    targets:\n"
            "      - arm64-macosx\n"
            "  - symbols: [ _wrong_simulator ]\n"
            "    targets: [ arm64-macos-simulator, not-arm64-macosx ]\n"
            "targets: [ arm64-macos, x86_64-macos ]\n"
            "current-version: 7.8.9\n"
            "tbd-version: 4\n"
            "reexported-libraries:\n"
            "  - libraries:\n"
            "      - '/usr/lib/libChild.dylib'\n"
            "    targets: [ arm64-macos ]\n"
            "  - libraries: [ '/usr/lib/libWrong.dylib' ]\n"
            "    targets: [ arm64e-maccatalyst ]\n"
            "--- !tapi-tbd\n"
            "targets:\n"
            "  - arm64e-macos\n"
            "install-name: '/usr/lib/libStructuredChild.dylib'\n"
            "tbd-version: 4\n"
            "reexports:\n"
            "  - weak-symbols:\n"
            "      - _child_weak_reexport\n"
            "    symbols: [ _child_reexport ]\n"
            "    targets: [ arm64e-macos ]\n"
            "...\n";

    ld_tapi_stub_t stub;
    ld_tapi_error_t error;
    assert(ld_tapi_parse((const uint8_t *) source, sizeof(source) - 1U,
                         &stub, &error) == LD_OK);
    assert(strcmp(stub.install_name, "/usr/lib/libStructured.dylib") == 0);
    assert(stub.current_version == ld_macos_version(7, 8, 9));
    assert(stub.compatibility_version == ld_macos_version(2, 3, 0));

    const ld_tapi_symbol_t *symbol = find_symbol(&stub, "_hash#symbol");
    assert(symbol && symbol->kind == LD_TAPI_SYMBOL_REGULAR && !symbol->weak);
    assert(find_symbol(&stub, "_legacy_macosx") != NULL);
    assert(find_symbol(&stub, "_wrong_simulator") == NULL);
    symbol = find_symbol(&stub, "_weak");
    assert(symbol && symbol->weak && symbol->kind == LD_TAPI_SYMBOL_REGULAR);
    symbol = find_symbol(&stub, "_absolute");
    assert(symbol && symbol->kind == LD_TAPI_SYMBOL_ABSOLUTE && !symbol->weak);
    symbol = find_symbol(&stub, "_thread_local");
    assert(symbol && symbol->kind == LD_TAPI_SYMBOL_TLV && !symbol->weak);
    assert(find_symbol(&stub, "_OBJC_CLASS_$_StructuredClass") != NULL);
    assert(find_symbol(&stub, "_OBJC_METACLASS_$_StructuredClass") != NULL);
    assert(find_symbol(&stub, "_OBJC_IVAR_$_StructuredClass.value") != NULL);
    assert(find_symbol(&stub, "_OBJC_EHTYPE_$_StructuredException") != NULL);
    symbol = find_symbol(&stub, "_child_reexport");
    assert(symbol && symbol->reexport && !symbol->weak);
    symbol = find_symbol(&stub, "_child_weak_reexport");
    assert(symbol && symbol->reexport && symbol->weak);
    assert(has_reexport(&stub, "/usr/lib/libChild.dylib"));
    assert(!has_reexport(&stub, "/usr/lib/libWrong.dylib"));
    ld_tapi_stub_deinit(&stub);
}

static void test_tapi_v3_zippered_and_reexports(void) {
    static const char source[] =
            "--- !tapi-tbd-v3\n"
            "platform: zippered\n"
            "install-name: '/usr/lib/libLegacy.dylib'\n"
            "exports:\n"
            "  - symbols:\n"
            "      - _legacy\n"
            "    re-exports:\n"
            "      - '/usr/lib/libLegacyChild.dylib'\n"
            "    objc-classes: [ LegacyClass ]\n"
            "    archs:\n"
            "      - arm64\n"
            "  - symbols: [ _wrong_arch ]\n"
            "    re-exports: [ '/usr/lib/libWrongArch.dylib' ]\n"
            "    archs: [ x86_64 ]\n"
            "archs: [ arm64, x86_64 ]\n"
            "--- !tapi-tbd-v3\n"
            "archs: [ arm64 ]\n"
            "install-name: '/usr/lib/libLegacyMacOS.dylib'\n"
            "exports:\n"
            "  - archs: [ arm64 ]\n"
            "    symbols: [ _legacy_macos ]\n"
            "platform: macos\n"
            "...\n";

    ld_tapi_stub_t stub;
    ld_tapi_error_t error;
    assert(ld_tapi_parse((const uint8_t *) source, sizeof(source) - 1U,
                         &stub, &error) == LD_OK);
    assert(strcmp(stub.install_name, "/usr/lib/libLegacy.dylib") == 0);
    assert(find_symbol(&stub, "_legacy") != NULL);
    assert(find_symbol(&stub, "_legacy_macos") != NULL);
    assert(find_symbol(&stub, "_wrong_arch") == NULL);
    assert(find_symbol(&stub, "_OBJC_CLASS_$_LegacyClass") != NULL);
    assert(find_symbol(&stub, "_OBJC_METACLASS_$_LegacyClass") != NULL);
    assert(has_reexport(&stub, "/usr/lib/libLegacyChild.dylib"));
    assert(!has_reexport(&stub, "/usr/lib/libWrongArch.dylib"));
    ld_tapi_stub_deinit(&stub);
}

static void test_tapi_reports_malformed_yaml(void) {
    static const char source[] =
            "--- !tapi-tbd\n"
            "tbd-version: 4\n"
            "targets: [ 'arm64e-macos ]\n"
            "install-name: '/usr/lib/libMalformed.dylib'\n"
            "...\n";
    ld_tapi_stub_t stub;
    ld_tapi_error_t error;
    assert(ld_tapi_parse((const uint8_t *) source, sizeof(source) - 1U,
                         &stub, &error) == LD_INVALID_INPUT);
    assert(error.line != 0U);
    assert(error.message[0] != '\0');
}

void test_ld_tapi(void) {
    test_tapi_v4_structured_documents();
    test_tapi_v3_zippered_and_reexports();
    test_tapi_reports_malformed_yaml();
}

#ifdef TEST_LD_TAPI_STANDALONE
int main(void) {
    test_ld_tapi();
    return 0;
}
#endif
