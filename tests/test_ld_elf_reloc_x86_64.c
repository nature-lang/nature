#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_cie.h"
#include "src/ld/ld_elf_eh_frame.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_reloc.h"
#include "src/ld/ld_elf_symtab.h"
#include "src/ld/ld_elf_thunk.h"
#include "src/ld/ld_internal.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_elf_amd64_weak_dynamic_link(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_weak_dynamic_object(&object_size);
    char object_path[] = "/tmp/nature-ld-weak-dynamic-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-weak-dynamic-output";
    unlink(output_path);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(image_size >= LD_ELF64_EHDR_SIZE + LD_ELF64_PHDR_SIZE);
    uint64_t entry = test_elf_read_u64(image + 24U);
    assert(entry >= 0x400000U && entry - 0x400000U <= image_size - 4U);
    size_t entry_offset = (size_t) (entry - 0x400000U);
    /* S + A - P = 0 - 4 - entry for the weak undefined _DYNAMIC. */
    uint32_t expected = (uint32_t) (0U - 4U - entry);
    assert(test_elf_read_u32(image + entry_offset) == expected);
    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_amd64_relocations(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;

    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_X86_64;
    object.display_name = (char *) "amd64-relocation-test.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_relocation_t relocation = {0};
    ld_elf_reloc_scan_result_t scan = {0};
    ld_elf_reloc_values_t values = {0};
    uint8_t place[16] = {0};

    /* A weak undefined _DYNAMIC resolves to the linker-defined zero symbol.
     * The -4 addend is emitted by crt objects and must remain a valid signed
     * PC32 expression instead of tripping an unsigned-underflow check. */
    relocation.type = LD_ELF_R_X86_64_PC32;
    relocation.addend = -4;
    values.place_address = 0x400009U;
    values.symbol_address = 0U;
    values.symbol_name = "_DYNAMIC";
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.pc_relative && !scan.needs_got && !scan.needs_plt);
    assert(scan.write_width == 4U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xffbffff3U);
    assert(capture.count == 0U);

    /*
     * The context-free formula scan conservatively reports a GOT need. The
     * backend's instruction/target-aware allocation pass can now suppress it
     * and is covered by test_ld_elf_relax.c.
     */
    relocation.type = LD_ELF_R_X86_64_REX_GOTPCRELX;
    relocation.addend = -4;
    values.place_address = 0x400008U;
    values.got_entry_address = 0x403000U;
    values.symbol_name = "got_target";
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.needs_got && scan.pc_relative && !scan.needs_plt);
    assert(scan.write_width == 4U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x2ff4U);

    /* Local-exec TLS relocations use S + A - TP. */
    relocation.type = LD_ELF_R_X86_64_TPOFF32;
    relocation.addend = 0;
    values.symbol_address = 0x403000U;
    values.thread_pointer_address = 0x404000U;
    values.symbol_name = "tls_local";
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(!scan.needs_got && !scan.pc_relative && scan.write_width == 4U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xfffff000U);

    relocation.type = LD_ELF_R_X86_64_TPOFF64;
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(!scan.needs_got && !scan.pc_relative && scan.write_width == 8U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u64(place) == 0xfffffffffffff000ULL);

    /* Initial-exec style DTP offsets use S + A - the TLS block address. */
    relocation.type = LD_ELF_R_X86_64_DTPOFF32;
    relocation.addend = -0x20;
    values.symbol_address = 0x403120U;
    values.tls_block_address = 0x403000U;
    values.symbol_name = "tls_dtp";
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(!scan.needs_got && !scan.pc_relative && scan.write_width == 4U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x100U);

    relocation.type = LD_ELF_R_X86_64_DTPOFF64;
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(!scan.needs_got && !scan.pc_relative && scan.write_width == 8U);
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u64(place) == 0x100U);

    assert(capture.count == 0U);
    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

static void test_elf_amd64_code_model_relocations(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;

    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_X86_64;
    object.display_name = (char *) "amd64-code-model-relocation-test.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_relocation_t relocation = {
            .offset = 0x28U,
    };
    ld_elf_reloc_scan_result_t scan = {0};
    uint8_t place[16];

    /*
     * These are all ELF64 x86-64 relocations.  Numeric suffixes such as 8,
     * 16, and 32 describe the relocation field width; they do not imply a
     * 32-bit target architecture.
     */
    static const struct {
        uint32_t type;
        const char *name;
        size_t width;
        bool needs_got;
        bool needs_got_base;
        bool pc_relative;
    } types[] = {
            {LD_ELF_R_X86_64_GOT32, "R_X86_64_GOT32", 4U, true, true,
             false},
            {LD_ELF_R_X86_64_16, "R_X86_64_16", 2U, false, false, false},
            {LD_ELF_R_X86_64_PC16, "R_X86_64_PC16", 2U, false, false,
             true},
            {LD_ELF_R_X86_64_8, "R_X86_64_8", 1U, false, false, false},
            {LD_ELF_R_X86_64_PC8, "R_X86_64_PC8", 1U, false, false, true},
            {LD_ELF_R_X86_64_PC64, "R_X86_64_PC64", 8U, false, false,
             true},
            {LD_ELF_R_X86_64_GOTOFF64, "R_X86_64_GOTOFF64", 8U, false,
             true, false},
            {LD_ELF_R_X86_64_GOTPC32, "R_X86_64_GOTPC32", 4U, false, true,
             true},
            {LD_ELF_R_X86_64_GOT64, "R_X86_64_GOT64", 8U, true, true,
             false},
            {LD_ELF_R_X86_64_GOTPCREL64, "R_X86_64_GOTPCREL64", 8U, true,
             false, true},
            {LD_ELF_R_X86_64_GOTPC64, "R_X86_64_GOTPC64", 8U, false, true,
             true},
            {LD_ELF_R_X86_64_PLTOFF64, "R_X86_64_PLTOFF64", 8U, false,
             true, false},
            {LD_ELF_R_X86_64_SIZE32, "R_X86_64_SIZE32", 4U, false, false,
             false},
            {LD_ELF_R_X86_64_SIZE64, "R_X86_64_SIZE64", 8U, false, false,
             false},
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        relocation.type = types[i].type;
        assert(strcmp(ld_elf_relocation_name(LD_ARCH_AMD64,
                                             relocation.type),
                      types[i].name) == 0);
        assert(ld_elf_relocation_supported(LD_ARCH_AMD64,
                                           relocation.type));
        assert(ld_elf_relocation_write_width(LD_ARCH_AMD64,
                                             relocation.type) ==
               types[i].width);
        assert(ld_elf_relocation_needs_got(LD_ARCH_AMD64,
                                           relocation.type) ==
               types[i].needs_got);
        assert(ld_elf_relocation_needs_got_base(LD_ARCH_AMD64,
                                                relocation.type) ==
               types[i].needs_got_base);
        assert(ld_elf_relocation_got_kind(LD_ARCH_AMD64,
                                          relocation.type) ==
               (types[i].needs_got ? LD_ELF_RELOC_GOT_ORDINARY
                                   : LD_ELF_RELOC_GOT_NONE));
        assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                      false, &scan) == LD_OK);
        assert(scan.write_width == types[i].width);
        assert(scan.needs_got == types[i].needs_got);
        assert(scan.needs_got_base == types[i].needs_got_base);
        assert(scan.pc_relative == types[i].pc_relative);
        assert(!scan.needs_plt);
    }

    typedef struct {
        uint32_t type;
        int64_t addend;
        uint64_t place_address;
        uint64_t symbol_address;
        uint64_t got_entry_address;
        uint64_t got_base_address;
        uint64_t symbol_size;
        size_t width;
        uint64_t expected;
    } apply_case_t;

    /*
     * Use distinct operands for every formula:
     *
     *   S   symbol_address       P   place_address
     *   GE  got_entry_address    GOT got_base_address
     *   Z   symbol_size          A   relocation addend
     */
    static const apply_case_t apply_cases[] = {
            {LD_ELF_R_X86_64_GOT32, -0x20, 0U, 0U, 0x603078U, 0x600000U,
             0U, 4U, 0x3058U},
            {LD_ELF_R_X86_64_16, -0x20, 0U, 0x1234U, 0U, 0U, 0U, 2U,
             0x1214U},
            {LD_ELF_R_X86_64_PC16, -0x20, 0x401020U, 0x401100U, 0U, 0U,
             0U, 2U, 0xc0U},
            {LD_ELF_R_X86_64_8, -0x20, 0U, 0x70U, 0U, 0U, 0U, 1U, 0x50U},
            {LD_ELF_R_X86_64_PC8, -0x10, 0x401020U, 0x401000U, 0U, 0U, 0U,
             1U, 0xd0U},
            {LD_ELF_R_X86_64_PC64, -0x20,
             UINT64_C(0x123456789abc0000),
             UINT64_C(0x123456789abc1000), 0U, 0U, 0U, 8U, 0xfe0U},
            {LD_ELF_R_X86_64_GOTOFF64, -0x20, 0U, 0x700123U, 0U,
             0x600000U, 0U, 8U, 0x100103U},
            {LD_ELF_R_X86_64_GOTPC32, -0x20, 0x401020U, 0U, 0U,
             0x600000U, 0U, 4U, 0x1fefc0U},
            {LD_ELF_R_X86_64_GOT64, -0x20, 0U, 0U, 0x603078U, 0x600000U,
             0U, 8U, 0x3058U},
            {LD_ELF_R_X86_64_GOTPCREL64, -0x20, 0x401020U, 0U, 0x603078U,
             0U, 0U, 8U, 0x202038U},
            {LD_ELF_R_X86_64_GOTPC64, -0x20, 0x401020U, 0U, 0U,
             0x600000U, 0U, 8U, 0x1fefc0U},
            {LD_ELF_R_X86_64_PLTOFF64, -0x20, 0U, 0x700123U, 0U,
             0x600000U, 0U, 8U, 0x100103U},
            {LD_ELF_R_X86_64_SIZE32, -0x20, 0U, 0U, 0U, 0U, 0x1234U, 4U,
             0x1214U},
            {LD_ELF_R_X86_64_SIZE64, -0x20, 0U, 0U, 0U, 0U,
             UINT64_C(0x123456789abcdef0), 8U,
             UINT64_C(0x123456789abcded0)},
    };

    for (size_t i = 0;
         i < sizeof(apply_cases) / sizeof(apply_cases[0]); i++) {
        const apply_case_t *test = &apply_cases[i];
        relocation.type = test->type;
        relocation.addend = test->addend;
        ld_elf_reloc_values_t values = {
                .place_address = test->place_address,
                .symbol_address = test->symbol_address,
                .got_entry_address = test->got_entry_address,
                .got_base_address = test->got_base_address,
                .symbol_size = test->symbol_size,
                .symbol_name = "code_model_target",
        };
        memset(place, 0xa5, sizeof(place));
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, sizeof(place), &values) ==
               LD_OK);
        switch (test->width) {
            case 1U:
                assert(place[0] == (uint8_t) test->expected);
                break;
            case 2U:
                assert(test_elf_read_u16(place) ==
                       (uint16_t) test->expected);
                break;
            case 4U:
                assert(test_elf_read_u32(place) ==
                       (uint32_t) test->expected);
                break;
            default:
                assert(test->width == 8U);
                assert(test_elf_read_u64(place) == test->expected);
                break;
        }
        for (size_t j = test->width; j < sizeof(place); j++) {
            assert(place[j] == 0xa5U);
        }
    }

    /* Both endpoints of every checked result width are accepted. */
    static const apply_case_t boundary_cases[] = {
            {LD_ELF_R_X86_64_GOT32, 0, 0U, 0U, INT32_MAX, 0U, 0U, 4U,
             INT32_MAX},
            {LD_ELF_R_X86_64_GOT32, 0, 0U, 0U, 0U,
             UINT64_C(0x80000000), 0U, 4U, UINT64_C(0x80000000)},
            {LD_ELF_R_X86_64_16, 0, 0U, UINT16_MAX, 0U, 0U, 0U, 2U,
             UINT16_MAX},
            {LD_ELF_R_X86_64_PC16, 0, 0U, INT16_MAX, 0U, 0U, 0U, 2U,
             INT16_MAX},
            {LD_ELF_R_X86_64_PC16, 0, UINT64_C(0x8000), 0U, 0U, 0U, 0U,
             2U, UINT64_C(0x8000)},
            {LD_ELF_R_X86_64_8, 0, 0U, INT8_MAX, 0U, 0U, 0U, 1U,
             INT8_MAX},
            {LD_ELF_R_X86_64_8, INT8_MIN, 0U, 0U, 0U, 0U, 0U, 1U,
             UINT8_C(0x80)},
            {LD_ELF_R_X86_64_PC8, 0, 0U, INT8_MAX, 0U, 0U, 0U, 1U,
             INT8_MAX},
            {LD_ELF_R_X86_64_PC8, 0, UINT64_C(0x80), 0U, 0U, 0U, 0U, 1U,
             UINT8_C(0x80)},
            {LD_ELF_R_X86_64_PC64, 0, 0U, INT64_MAX, 0U, 0U, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_PC64, 0, UINT64_C(0x8000000000000000), 0U,
             0U, 0U, 0U, 8U, UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_GOTOFF64, 0, 0U, INT64_MAX, 0U, 0U, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_GOTOFF64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 8U,
             UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_GOTPC32, 0, 0U, 0U, 0U, INT32_MAX, 0U, 4U,
             INT32_MAX},
            {LD_ELF_R_X86_64_GOTPC32, 0, UINT64_C(0x80000000), 0U, 0U, 0U,
             0U, 4U, UINT64_C(0x80000000)},
            {LD_ELF_R_X86_64_GOT64, 0, 0U, 0U, INT64_MAX, 0U, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_GOT64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 8U,
             UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_GOTPCREL64, 0, 0U, 0U, INT64_MAX, 0U, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_GOTPCREL64, 0,
             UINT64_C(0x8000000000000000), 0U, 0U, 0U, 0U, 8U,
             UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_GOTPC64, 0, 0U, 0U, 0U, INT64_MAX, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_GOTPC64, 0,
             UINT64_C(0x8000000000000000), 0U, 0U, 0U, 0U, 8U,
             UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_PLTOFF64, 0, 0U, INT64_MAX, 0U, 0U, 0U, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_PLTOFF64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 8U,
             UINT64_C(0x8000000000000000)},
            {LD_ELF_R_X86_64_SIZE32, 0, 0U, 0U, 0U, 0U, INT32_MAX, 4U,
             INT32_MAX},
            {LD_ELF_R_X86_64_SIZE32, INT32_MIN, 0U, 0U, 0U, 0U, 0U, 4U,
             UINT64_C(0x80000000)},
            {LD_ELF_R_X86_64_SIZE64, 0, 0U, 0U, 0U, 0U, INT64_MAX, 8U,
             INT64_MAX},
            {LD_ELF_R_X86_64_SIZE64, INT64_MIN, 0U, 0U, 0U, 0U, 0U, 8U,
             UINT64_C(0x8000000000000000)},
    };

    for (size_t i = 0;
         i < sizeof(boundary_cases) / sizeof(boundary_cases[0]); i++) {
        const apply_case_t *test = &boundary_cases[i];
        relocation.type = test->type;
        relocation.addend = test->addend;
        ld_elf_reloc_values_t values = {
                .place_address = test->place_address,
                .symbol_address = test->symbol_address,
                .got_entry_address = test->got_entry_address,
                .got_base_address = test->got_base_address,
                .symbol_size = test->symbol_size,
                .symbol_name = "boundary_target",
        };
        memset(place, 0, sizeof(place));
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, sizeof(place), &values) ==
               LD_OK);
        switch (test->width) {
            case 1U:
                assert(place[0] == (uint8_t) test->expected);
                break;
            case 2U:
                assert(test_elf_read_u16(place) ==
                       (uint16_t) test->expected);
                break;
            case 4U:
                assert(test_elf_read_u32(place) ==
                       (uint32_t) test->expected);
                break;
            default:
                assert(test->width == 8U);
                assert(test_elf_read_u64(place) == test->expected);
                break;
        }
    }

    /* One value past either endpoint is rejected without modifying output. */
    static const apply_case_t overflow_cases[] = {
            {LD_ELF_R_X86_64_GOT32, 0, 0U, 0U,
             UINT64_C(0x80000000), 0U, 0U, 4U, 0U},
            {LD_ELF_R_X86_64_GOT32, 0, 0U, 0U, 0U,
             UINT64_C(0x80000001), 0U, 4U, 0U},
            {LD_ELF_R_X86_64_16, 0, 0U, UINT64_C(0x10000), 0U, 0U, 0U, 2U,
             0U},
            {LD_ELF_R_X86_64_16, -1, 0U, 0U, 0U, 0U, 0U, 2U, 0U},
            {LD_ELF_R_X86_64_PC16, 0, 0U, UINT64_C(0x8000), 0U, 0U, 0U,
             2U, 0U},
            {LD_ELF_R_X86_64_PC16, 0, UINT64_C(0x8001), 0U, 0U, 0U, 0U,
             2U, 0U},
            {LD_ELF_R_X86_64_8, 0, 0U, UINT64_C(0x80), 0U, 0U, 0U, 1U,
             0U},
            {LD_ELF_R_X86_64_8, -129, 0U, 0U, 0U, 0U, 0U, 1U, 0U},
            {LD_ELF_R_X86_64_PC8, 0, 0U, UINT64_C(0x80), 0U, 0U, 0U, 1U,
             0U},
            {LD_ELF_R_X86_64_PC8, 0, UINT64_C(0x81), 0U, 0U, 0U, 0U, 1U,
             0U},
            {LD_ELF_R_X86_64_PC64, 0, 0U,
             UINT64_C(0x8000000000000000), 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_PC64, 0,
             UINT64_C(0x8000000000000001), 0U, 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTOFF64, 0, 0U,
             UINT64_C(0x8000000000000000), 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTOFF64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000001), 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTPC32, 0, 0U, 0U, 0U,
             UINT64_C(0x80000000), 0U, 4U, 0U},
            {LD_ELF_R_X86_64_GOTPC32, 0, UINT64_C(0x80000001), 0U, 0U, 0U,
             0U, 4U, 0U},
            {LD_ELF_R_X86_64_GOT64, 0, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOT64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000001), 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTPCREL64, 0, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTPCREL64, 0,
             UINT64_C(0x8000000000000001), 0U, 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTPC64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000000), 0U, 8U, 0U},
            {LD_ELF_R_X86_64_GOTPC64, 0,
             UINT64_C(0x8000000000000001), 0U, 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_PLTOFF64, 0, 0U,
             UINT64_C(0x8000000000000000), 0U, 0U, 0U, 8U, 0U},
            {LD_ELF_R_X86_64_PLTOFF64, 0, 0U, 0U, 0U,
             UINT64_C(0x8000000000000001), 0U, 8U, 0U},
            {LD_ELF_R_X86_64_SIZE32, 0, 0U, 0U, 0U, 0U,
             UINT64_C(0x80000000), 4U, 0U},
            {LD_ELF_R_X86_64_SIZE32, INT64_C(-2147483649), 0U, 0U, 0U, 0U,
             0U, 4U, 0U},
            {LD_ELF_R_X86_64_SIZE64, 0, 0U, 0U, 0U, 0U,
             UINT64_C(0x8000000000000000), 8U, 0U},
    };

    for (size_t i = 0;
         i < sizeof(overflow_cases) / sizeof(overflow_cases[0]); i++) {
        const apply_case_t *test = &overflow_cases[i];
        relocation.type = test->type;
        relocation.addend = test->addend;
        ld_elf_reloc_values_t values = {
                .place_address = test->place_address,
                .symbol_address = test->symbol_address,
                .got_entry_address = test->got_entry_address,
                .got_base_address = test->got_base_address,
                .symbol_size = test->symbol_size,
                .symbol_name = "overflow_target",
        };
        memset(place, 0xa5, sizeof(place));
        uint8_t before[sizeof(place)];
        memcpy(before, place, sizeof(before));
        capture.count = 0U;
        capture.message[0] = '\0';
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, sizeof(place), &values) ==
               LD_RELOCATION_ERROR);
        assert(memcmp(place, before, sizeof(place)) == 0);
        assert(capture.count == 1U);
        assert(strstr(capture.message,
                      ld_elf_relocation_name(LD_ARCH_AMD64,
                                             relocation.type)) != NULL);
        assert(strstr(capture.message, "overflow_target") != NULL);
    }

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

void test_ld_elf_reloc_x86_64(void) {
    test_elf_amd64_weak_dynamic_link();
    test_elf_amd64_relocations();
    test_elf_amd64_code_model_relocations();
}
