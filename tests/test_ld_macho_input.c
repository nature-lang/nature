#include "test_ld_macho_common.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_ld_macho_input(void) {
    static const uint8_t truncated_macho[] = {0xcf, 0xfa, 0xed, 0xfe};
    char macho_path[] = "/tmp/nature-ld-macho-XXXXXX";
    test_ld_write_fixture(macho_path, truncated_macho, sizeof(truncated_macho));
    test_ld_expect_invalid_input(macho_path, LD_INVALID_INPUT, "truncated");
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
    test_ld_write_fixture(aligned_object_path, aligned_object, aligned_object_size);
    free(aligned_object);
    test_ld_expect_invalid_input(aligned_object_path, LD_UNSUPPORTED,
                         "alignment exponent 21");
    unlink(aligned_object_path);

    uint8_t malformed_archive[68];
    memset(malformed_archive, ' ', sizeof(malformed_archive));
    memcpy(malformed_archive, "!<arch>\n", 8);
    memcpy(malformed_archive + 8 + 48, "abc       ", 10);
    memcpy(malformed_archive + 8 + 58, "`\n", 2);
    char archive_path[] = "/tmp/nature-ld-archive-XXXXXX";
    test_ld_write_fixture(archive_path, malformed_archive, sizeof(malformed_archive));
    test_ld_expect_invalid_input(archive_path, LD_INVALID_INPUT, "member size");
    unlink(archive_path);

    uint8_t missing_archive_padding[8 + 60 + 1];
    memset(missing_archive_padding, ' ', sizeof(missing_archive_padding));
    memcpy(missing_archive_padding, "!<arch>\n", 8);
    missing_archive_padding[8] = '/';
    missing_archive_padding[8 + 48] = '1';
    memcpy(missing_archive_padding + 8 + 58, "`\n", 2);
    missing_archive_padding[8 + 60] = 0;
    char missing_padding_path[] = "/tmp/nature-ld-archive-padding-XXXXXX";
    test_ld_write_fixture(missing_padding_path, missing_archive_padding, sizeof(missing_archive_padding));
    test_ld_expect_invalid_input(missing_padding_path, LD_INVALID_INPUT, "alignment padding");
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
    test_ld_write_fixture(long_archive_path, long_archive, 8 + 60 + long_payload_size);
    test_ld_expect_invalid_input(long_archive_path, LD_INVALID_INPUT, long_member);
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
    test_ld_write_fixture(fat_path, wrong_arch_fat, sizeof(wrong_arch_fat));
    test_ld_expect_invalid_input(fat_path, LD_UNSUPPORTED, "no arm64 slice");
    unlink(fat_path);
}
