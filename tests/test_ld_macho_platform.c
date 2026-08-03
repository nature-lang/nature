#include "test_ld_macho_common.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *make_platform_object(uint32_t command_kind,
                                     uint32_t platform,
                                     int32_t cpu_subtype,
                                     size_t *size_out) {
    size_t platform_size = command_kind == LD_LC_BUILD_VERSION
                                   ? sizeof(ld_build_version_command_t)
                                   : sizeof(ld_version_min_command_t);
    size_t commands_size = platform_size + sizeof(ld_symtab_command_t);
    size_t size = sizeof(ld_mach_header_64_t) + commands_size + 1U;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    ld_mach_header_64_t header = {0};
    header.magic = LD_MH_MAGIC_64;
    header.cputype = LD_CPU_TYPE_ARM64;
    header.cpusubtype = cpu_subtype;
    header.filetype = LD_MH_OBJECT;
    header.ncmds = 2U;
    header.sizeofcmds = (uint32_t) commands_size;
    memcpy(bytes, &header, sizeof(header));

    size_t offset = sizeof(header);
    if (command_kind == LD_LC_BUILD_VERSION) {
        ld_build_version_command_t build = {0};
        build.cmd = LD_LC_BUILD_VERSION;
        build.cmdsize = sizeof(build);
        build.platform = platform;
        build.minos = ld_macos_version(11, 0, 0);
        build.sdk = ld_macos_version(15, 0, 0);
        memcpy(bytes + offset, &build, sizeof(build));
    } else {
        ld_version_min_command_t version = {0};
        version.cmd = command_kind;
        version.cmdsize = sizeof(version);
        version.version = ld_macos_version(11, 0, 0);
        version.sdk = ld_macos_version(15, 0, 0);
        memcpy(bytes + offset, &version, sizeof(version));
    }
    offset += platform_size;

    ld_symtab_command_t symtab = {0};
    symtab.cmd = LD_LC_SYMTAB;
    symtab.cmdsize = sizeof(symtab);
    symtab.symoff = (uint32_t) (sizeof(header) + commands_size);
    symtab.stroff = symtab.symoff;
    symtab.strsize = 1U;
    memcpy(bytes + offset, &symtab, sizeof(symtab));
    *size_out = size;
    return bytes;
}

static void parse_object_success(const uint8_t *bytes, size_t size) {
    char path[] = "/tmp/nature-ld-platform-ok-XXXXXX";
    test_ld_write_fixture(path, bytes, size);
    ld_options_t options;
    ld_options_init(&options);
    ld_context_t ctx = {0};
    ctx.options = &options;
    assert(ld_parse_input_file(&ctx, path) == LD_OK);
    assert(ctx.objects.count == 1U);
    test_ld_parsed_context_deinit(&ctx);
    ld_options_deinit(&options);
    unlink(path);
}

static void expect_object_failure(const uint8_t *bytes, size_t size,
                                  int error, const char *message) {
    char path[] = "/tmp/nature-ld-platform-bad-XXXXXX";
    test_ld_write_fixture(path, bytes, size);
    test_ld_expect_invalid_input(path, error, message);
    unlink(path);
}

static void test_object_platforms(void) {
    size_t size = 0;
    uint8_t *bytes = make_platform_object(LD_LC_BUILD_VERSION,
                                          LD_PLATFORM_MACOS,
                                          LD_CPU_SUBTYPE_ARM64_ALL, &size);
    parse_object_success(bytes, size);
    free(bytes);

    bytes = make_platform_object(LD_LC_VERSION_MIN_MACOSX,
                                 LD_PLATFORM_MACOS,
                                 LD_CPU_SUBTYPE_ARM64_ALL, &size);
    parse_object_success(bytes, size);
    free(bytes);

    bytes = make_platform_object(LD_LC_BUILD_VERSION, LD_PLATFORM_IOS,
                                 LD_CPU_SUBTYPE_ARM64_ALL, &size);
    expect_object_failure(bytes, size, LD_UNSUPPORTED, "targets iOS");
    free(bytes);

    bytes = make_platform_object(LD_LC_BUILD_VERSION,
                                 LD_PLATFORM_MACCATALYST,
                                 LD_CPU_SUBTYPE_ARM64_ALL, &size);
    expect_object_failure(bytes, size, LD_UNSUPPORTED,
                          "targets Mac Catalyst");
    free(bytes);

    bytes = make_platform_object(LD_LC_VERSION_MIN_IPHONEOS,
                                 LD_PLATFORM_IOS,
                                 LD_CPU_SUBTYPE_ARM64_ALL, &size);
    expect_object_failure(bytes, size, LD_UNSUPPORTED, "targets iOS");
    free(bytes);
}

static void test_arm64e_binary_rejection(void) {
    size_t object_size = 0;
    uint8_t *object = make_platform_object(LD_LC_BUILD_VERSION,
                                           LD_PLATFORM_MACOS,
                                           LD_CPU_SUBTYPE_ARM64E,
                                           &object_size);
    expect_object_failure(object, object_size, LD_UNSUPPORTED,
                          "unsupported Mach-O object");

    size_t fat_size = 8U + sizeof(ld_fat_arch_t) + object_size;
    uint8_t *fat = calloc(1, fat_size);
    assert(fat != NULL);
    fat[0] = 0xca;
    fat[1] = 0xfe;
    fat[2] = 0xba;
    fat[3] = 0xbe;
    fat[7] = 1U;
    size_t arch = 8U;
    uint32_t cpu = LD_CPU_TYPE_ARM64;
    fat[arch + 0U] = (uint8_t) (cpu >> 24U);
    fat[arch + 1U] = (uint8_t) (cpu >> 16U);
    fat[arch + 2U] = (uint8_t) (cpu >> 8U);
    fat[arch + 3U] = (uint8_t) cpu;
    fat[arch + 7U] = LD_CPU_SUBTYPE_ARM64E;
    uint32_t slice_offset = (uint32_t) (8U + sizeof(ld_fat_arch_t));
    fat[arch + 8U] = (uint8_t) (slice_offset >> 24U);
    fat[arch + 9U] = (uint8_t) (slice_offset >> 16U);
    fat[arch + 10U] = (uint8_t) (slice_offset >> 8U);
    fat[arch + 11U] = (uint8_t) slice_offset;
    uint32_t slice_size = (uint32_t) object_size;
    fat[arch + 12U] = (uint8_t) (slice_size >> 24U);
    fat[arch + 13U] = (uint8_t) (slice_size >> 16U);
    fat[arch + 14U] = (uint8_t) (slice_size >> 8U);
    fat[arch + 15U] = (uint8_t) slice_size;
    memcpy(fat + slice_offset, object, object_size);
    expect_object_failure(fat, fat_size, LD_UNSUPPORTED,
                          "contains only an arm64e slice");
    free(fat);
    free(object);
}

void test_ld_macho_platform(void) {
    test_object_platforms();
    test_arm64e_binary_rejection();
}
