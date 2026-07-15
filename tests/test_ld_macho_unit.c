#include "test_ld_macho_common.h"

#include "src/ld/sha256.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

void test_ld_macho_unit(void) {
    ld_options_init(NULL);
    ld_options_deinit(NULL);
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
    ld_options_t options;
    ld_options_init(&options);
    assert(!options.objc_load);

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

    assert(ld_parse_flags(&options, "-ObjC") == LD_OK);
    assert(options.objc_load);

    assert(ld_parse_flags(&options, "-L '/tmp/a\\b' -L\"/tmp/c\\q\"") == LD_OK);
    assert(options.library_paths.count == 4);
    assert(strcmp(options.library_paths.items[2], "/tmp/a\\b") == 0);
    assert(strcmp(options.library_paths.items[3], "/tmp/c\\q") == 0);

    test_ld_diagnostic_capture_t capture = {0};
    options.diagnostic = test_ld_capture_diagnostic;
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
    ld_options_deinit(&options);
}
