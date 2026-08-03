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

static void test_elf_arm64_relocations(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_ARM64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_AARCH64;
    object.display_name = (char *) "relocation-test.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_relocation_t relocation = {0};
    ld_elf_reloc_values_t values = {
            .symbol_name = "target",
    };
    uint8_t place[8] = {0};

    relocation.type = LD_ELF_R_AARCH64_ABS64;
    relocation.addend = 0x678;
    values.symbol_address = 0x12345000U;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u64(place) == 0x12345678U);

    relocation.type = LD_ELF_R_AARCH64_ADR_PREL_PG_HI21;
    relocation.addend = 0;
    values.place_address = 0x400000U;
    values.symbol_address = 0x412345U;
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xd0000080U);
    relocation.type = LD_ELF_R_AARCH64_ADD_ABS_LO12_NC;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x910d1400U);

    relocation.type = LD_ELF_R_AARCH64_CALL26;
    values.place_address = 0x400000U;
    values.symbol_address = 0x400100U;
    test_elf_write_u32(place, 0x94000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x94000040U);

    relocation.type = LD_ELF_R_AARCH64_ADR_GOT_PAGE;
    values.place_address = 0x400000U;
    values.got_entry_address = 0x423450U;
    ld_elf_reloc_scan_result_t scan;
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.needs_got && scan.pc_relative && scan.write_width == 4U);
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf0000100U);
    relocation.type = LD_ELF_R_AARCH64_LD64_GOT_LO12_NC;
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.needs_got && !scan.pc_relative && scan.write_width == 4U);
    test_elf_write_u32(place, 0xf9400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf9422800U);

    /*
     * These code-model relocations are present in real AArch64 GCC/Clang
     * ELF64 objects.  The 16/32 suffixes describe relocation field widths,
     * not support for a 32-bit target architecture.
     */
    static const uint32_t code_model_types[] = {
            LD_ELF_R_AARCH64_ABS16,
            LD_ELF_R_AARCH64_PREL16,
            LD_ELF_R_AARCH64_MOVW_UABS_G0,
            LD_ELF_R_AARCH64_MOVW_UABS_G0_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G1,
            LD_ELF_R_AARCH64_MOVW_UABS_G1_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G2,
            LD_ELF_R_AARCH64_MOVW_UABS_G2_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G3,
            LD_ELF_R_AARCH64_LD_PREL_LO19,
            LD_ELF_R_AARCH64_TSTBR14,
            LD_ELF_R_AARCH64_GOT_LD_PREL19,
    };
    static const char *const code_model_names[] = {
            "R_AARCH64_ABS16",
            "R_AARCH64_PREL16",
            "R_AARCH64_MOVW_UABS_G0",
            "R_AARCH64_MOVW_UABS_G0_NC",
            "R_AARCH64_MOVW_UABS_G1",
            "R_AARCH64_MOVW_UABS_G1_NC",
            "R_AARCH64_MOVW_UABS_G2",
            "R_AARCH64_MOVW_UABS_G2_NC",
            "R_AARCH64_MOVW_UABS_G3",
            "R_AARCH64_LD_PREL_LO19",
            "R_AARCH64_TSTBR14",
            "R_AARCH64_GOT_LD_PREL19",
    };
    for (size_t i = 0;
         i < sizeof(code_model_types) / sizeof(code_model_types[0]); i++) {
        relocation.type = code_model_types[i];
        assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64,
                                             relocation.type),
                      code_model_names[i]) == 0);
        assert(ld_elf_relocation_supported(LD_ARCH_ARM64,
                                           relocation.type));
        size_t expected_width =
                (relocation.type == LD_ELF_R_AARCH64_ABS16 ||
                 relocation.type == LD_ELF_R_AARCH64_PREL16)
                        ? 2U
                        : 4U;
        assert(ld_elf_relocation_write_width(LD_ARCH_ARM64,
                                             relocation.type) ==
               expected_width);
        assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                      false, &scan) == LD_OK);
        bool expected_got =
                relocation.type == LD_ELF_R_AARCH64_GOT_LD_PREL19;
        bool expected_pc_relative =
                relocation.type == LD_ELF_R_AARCH64_PREL16 ||
                relocation.type == LD_ELF_R_AARCH64_LD_PREL_LO19 ||
                relocation.type == LD_ELF_R_AARCH64_TSTBR14 ||
                expected_got;
        assert(scan.needs_got == expected_got);
        assert(scan.pc_relative == expected_pc_relative);
        assert(!scan.needs_plt && scan.write_width == expected_width);
        assert(ld_elf_relocation_got_kind(LD_ARCH_ARM64,
                                          relocation.type) ==
               (expected_got ? LD_ELF_RELOC_GOT_ORDINARY
                             : LD_ELF_RELOC_GOT_NONE));
    }

    relocation.addend = 0;
    relocation.type = LD_ELF_R_AARCH64_ABS16;
    values.symbol_address = 0xabcdU;
    place[0] = 0U;
    place[1] = 0U;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 2U, &values) == LD_OK);
    assert(test_elf_read_u16(place) == 0xabcdU);

    relocation.type = LD_ELF_R_AARCH64_PREL16;
    values.place_address = 0x400000U;
    values.symbol_address = 0x400100U;
    place[0] = 0U;
    place[1] = 0U;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 2U, &values) == LD_OK);
    assert(test_elf_read_u16(place) == 0x100U);

    static const uint32_t movw_types[] = {
            LD_ELF_R_AARCH64_MOVW_UABS_G0_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G1_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G2_NC,
            LD_ELF_R_AARCH64_MOVW_UABS_G3,
    };
    static const uint32_t movw_input[] = {
            0xd2800008U,
            0xf2a00008U,
            0xf2c00008U,
            0xf2e00008U,
    };
    static const uint32_t movw_expected[] = {
            0xd29bde08U,
            0xf2b35788U,
            0xf2cacf08U,
            0xf2e24688U,
    };
    values.symbol_address = UINT64_C(0x123456789abcdef0);
    for (size_t i = 0; i < sizeof(movw_types) / sizeof(movw_types[0]); i++) {
        relocation.type = movw_types[i];
        test_elf_write_u32(place, movw_input[i]);
        assert(ld_elf_relocation_apply(&ctx, &object, &section,
                                       &relocation, place, 4U,
                                       &values) == LD_OK);
        assert(test_elf_read_u32(place) == movw_expected[i]);
    }

    static const uint32_t movw_checked_types[] = {
            LD_ELF_R_AARCH64_MOVW_UABS_G0,
            LD_ELF_R_AARCH64_MOVW_UABS_G1,
            LD_ELF_R_AARCH64_MOVW_UABS_G2,
    };
    static const uint64_t movw_checked_values[] = {
            UINT64_C(0xabcd),
            UINT64_C(0x1234abcd),
            UINT64_C(0x56781234abcd),
    };
    static const uint32_t movw_checked_expected[] = {
            0xd29579a8U,
            0xf2a24688U,
            0xf2cacf08U,
    };
    for (size_t i = 0;
         i < sizeof(movw_checked_types) / sizeof(movw_checked_types[0]);
         i++) {
        relocation.type = movw_checked_types[i];
        values.symbol_address = movw_checked_values[i];
        test_elf_write_u32(place, movw_input[i]);
        assert(ld_elf_relocation_apply(&ctx, &object, &section,
                                       &relocation, place, 4U,
                                       &values) == LD_OK);
        assert(test_elf_read_u32(place) == movw_checked_expected[i]);
    }

    values.place_address = 0x400000U;
    values.symbol_address = 0x400100U;
    relocation.type = LD_ELF_R_AARCH64_LD_PREL_LO19;
    test_elf_write_u32(place, 0x58000009U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x58000809U);
    values.symbol_address = values.place_address - 4U;
    test_elf_write_u32(place, 0x58000009U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x58ffffe9U);

    relocation.type = LD_ELF_R_AARCH64_GOT_LD_PREL19;
    values.got_entry_address = 0x400100U;
    test_elf_write_u32(place, 0x58000009U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x58000809U);

    relocation.type = LD_ELF_R_AARCH64_TSTBR14;
    values.symbol_address = 0x400100U;
    test_elf_write_u32(place, 0x36080000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x36080800U);
    values.symbol_address = values.place_address - 4U;
    test_elf_write_u32(place, 0x36080000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x360fffe0U);

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_LD_PREL_LO19;
    values.symbol_address = values.place_address + 2U;
    test_elf_write_u32(place, 0x58000009U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x58000009U);
    assert(capture.count == 1U && strstr(capture.message, "aligned"));

    capture.count = 0U;
    capture.message[0] = '\0';
    values.symbol_address = values.place_address + (UINT64_C(1) << 20U);
    test_elf_write_u32(place, 0x58000009U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x58000009U);
    assert(capture.count == 1U && strstr(capture.message, "out of range"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_TSTBR14;
    values.symbol_address = values.place_address + 32768U;
    test_elf_write_u32(place, 0x36080000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x36080000U);
    assert(capture.count == 1U && strstr(capture.message, "out of range"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_MOVW_UABS_G3;
    values.symbol_address = 0U;
    relocation.addend = -1;
    test_elf_write_u32(place, 0xf2e00008U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xf2e00008U);
    assert(capture.count == 1U && strstr(capture.message, "unsigned 64-bit"));
    capture.count = 0U;
    capture.message[0] = '\0';
    values.symbol_address = UINT64_MAX;
    relocation.addend = 1;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xf2e00008U);
    assert(capture.count == 1U && strstr(capture.message, "unsigned 64-bit"));

    relocation.addend = 0;

    static const uint32_t tls_got_types[] = {
            LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21,
            LD_ELF_R_AARCH64_TLSGD_ADD_LO12_NC,
            LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21,
            LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC,
    };
    static const char *const tls_got_names[] = {
            "R_AARCH64_TLSGD_ADR_PAGE21",
            "R_AARCH64_TLSGD_ADD_LO12_NC",
            "R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21",
            "R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC",
    };
    static const ld_elf_reloc_got_kind_t tls_got_kinds[] = {
            LD_ELF_RELOC_GOT_TLSGD,
            LD_ELF_RELOC_GOT_TLSGD,
            LD_ELF_RELOC_GOT_TP,
            LD_ELF_RELOC_GOT_TP,
    };
    for (size_t i = 0; i < sizeof(tls_got_types) / sizeof(tls_got_types[0]);
         i++) {
        relocation.type = tls_got_types[i];
        assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64,
                                             relocation.type),
                      tls_got_names[i]) == 0);
        assert(ld_elf_relocation_got_kind(LD_ARCH_ARM64,
                                          relocation.type) ==
               tls_got_kinds[i]);
        assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                      false, &scan) == LD_OK);
        assert(scan.needs_got && !scan.needs_plt &&
               scan.write_width == 4U);
        assert(scan.pc_relative ==
               (relocation.type == LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21 ||
                relocation.type ==
                        LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21));
    }

    values.place_address = 0x400000U;
    values.got_entry_address = 0x423450U;
    relocation.type = LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21;
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf0000100U);
    relocation.type = LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC;
    test_elf_write_u32(place, 0xf9400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf9422800U);
    relocation.type = LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21;
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf0000100U);
    relocation.type = LD_ELF_R_AARCH64_TLSGD_ADD_LO12_NC;
    values.got_entry_address = 0x423460U;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x91118000U);

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC;
    values.got_entry_address = 0x423451U;
    test_elf_write_u32(place, 0xf9400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xf9400000U);
    assert(capture.count == 1U && strstr(capture.message, "aligned"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21;
    values.place_address = 0x400000U;
    values.got_entry_address = values.place_address + (UINT64_C(1) << 32U);
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x90000000U);
    assert(capture.count == 1U && strstr(capture.message, "signed 21"));

    values.symbol_address = 0x720345U;
    values.thread_pointer_address = 0x700000U;
    relocation.type = LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12;
    test_elf_write_u32(place, 0x91400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x91408000U);
    relocation.type = LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x910d1400U);

    relocation.type = LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12;
    assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64, relocation.type),
                  "R_AARCH64_TLSLE_ADD_TPREL_LO12") == 0);
    assert(ld_elf_relocation_supported(LD_ARCH_ARM64, relocation.type));
    assert(ld_elf_relocation_write_width(LD_ARCH_ARM64, relocation.type) ==
           4U);
    values.symbol_address = values.thread_pointer_address + 0x345U;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x910d1400U);
    values.symbol_address = values.thread_pointer_address + 0x1000U;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x91000000U);

    static const uint32_t tlsle_ldst_types[][2] = {
            {LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12,
             LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC},
            {LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12,
             LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC},
            {LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12,
             LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC},
            {LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12,
             LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC},
            {LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12,
             LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC},
    };
    static const char *const tlsle_ldst_names[][2] = {
            {"R_AARCH64_TLSLE_LDST8_TPREL_LO12",
             "R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC"},
            {"R_AARCH64_TLSLE_LDST16_TPREL_LO12",
             "R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC"},
            {"R_AARCH64_TLSLE_LDST32_TPREL_LO12",
             "R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC"},
            {"R_AARCH64_TLSLE_LDST64_TPREL_LO12",
             "R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC"},
            {"R_AARCH64_TLSLE_LDST128_TPREL_LO12",
             "R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC"},
    };
    static const uint32_t tlsle_ldst_input[] = {
            0x39400000U,
            0x79400000U,
            0xb9400000U,
            0xf9400000U,
            0x3dc00000U,
    };
    static const uint32_t tlsle_ldst_expected[] = {
            0x394d1400U,
            0x79468c00U,
            0xb9434800U,
            0xf941a400U,
            0x3dc0d400U,
    };
    static const uint16_t tlsle_ldst_offsets[] = {
            0x345U,
            0x346U,
            0x348U,
            0x348U,
            0x350U,
    };
    for (size_t i = 0;
         i < sizeof(tlsle_ldst_input) / sizeof(tlsle_ldst_input[0]); i++) {
        for (size_t variant = 0; variant < 2U; variant++) {
            relocation.type = tlsle_ldst_types[i][variant];
            assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64,
                                                 relocation.type),
                          tlsle_ldst_names[i][variant]) == 0);
            assert(ld_elf_relocation_supported(LD_ARCH_ARM64,
                                               relocation.type));
            assert(ld_elf_relocation_got_kind(LD_ARCH_ARM64,
                                              relocation.type) ==
                   LD_ELF_RELOC_GOT_NONE);
            assert(ld_elf_relocation_write_width(LD_ARCH_ARM64,
                                                 relocation.type) == 4U);
            values.symbol_address = values.thread_pointer_address +
                                    tlsle_ldst_offsets[i];
            test_elf_write_u32(place, tlsle_ldst_input[i]);
            assert(ld_elf_relocation_apply(
                           &ctx, &object, &section, &relocation, place, 4U,
                           &values) == LD_OK);
            assert(test_elf_read_u32(place) == tlsle_ldst_expected[i]);
        }
    }

    relocation.type = LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12;
    values.symbol_address = values.thread_pointer_address + 0x1000U;
    test_elf_write_u32(place, 0xf9400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xf9400000U);
    relocation.type = LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC;
    values.symbol_address = values.thread_pointer_address + 0x1348U;
    test_elf_write_u32(place, 0xf9400000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf941a400U);
    values.symbol_address = values.thread_pointer_address + 0x347U;
    for (size_t variant = 0; variant < 2U; variant++) {
        relocation.type = tlsle_ldst_types[3][variant];
        test_elf_write_u32(place, 0xf9400000U);
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, 4U, &values) ==
               LD_RELOCATION_ERROR);
        assert(test_elf_read_u32(place) == 0xf9400000U);
    }

    static const uint32_t tlsdesc_types[] = {
            LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21,
            LD_ELF_R_AARCH64_TLSDESC_LD64_LO12,
            LD_ELF_R_AARCH64_TLSDESC_ADD_LO12,
            LD_ELF_R_AARCH64_TLSDESC_CALL,
    };
    static const char *const tlsdesc_names[] = {
            "R_AARCH64_TLSDESC_ADR_PAGE21",
            "R_AARCH64_TLSDESC_LD64_LO12",
            "R_AARCH64_TLSDESC_ADD_LO12",
            "R_AARCH64_TLSDESC_CALL",
    };
    for (size_t i = 0; i < sizeof(tlsdesc_types) / sizeof(tlsdesc_types[0]);
         i++) {
        relocation.type = tlsdesc_types[i];
        assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64,
                                             relocation.type),
                      tlsdesc_names[i]) == 0);
        assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                      true, &scan) == LD_OK);
        assert(!scan.needs_got && !scan.needs_plt && !scan.pc_relative);
        assert(scan.write_width == 4U);
    }

    relocation.addend = 0x1111;
    values.symbol_address = 0x71234567U;
    values.thread_pointer_address = 0x70000000U;
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21;
    test_elf_write_u32(place, 0x90000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xd503201fU);
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_LD64_LO12;
    test_elf_write_u32(place, 0xf9400002U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xd503201fU);
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_ADD_LO12;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xd2a02460U);
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_CALL;
    test_elf_write_u32(place, 0xd63f0040U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xf28acf00U);

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.addend = 0;
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21;
    test_elf_write_u32(place, 0xd503201fU);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xd503201fU);
    assert(capture.count == 1U && strstr(capture.message, "ADRP X0"));
    assert(strstr(capture.message, "R_AARCH64_TLSDESC_ADR_PAGE21"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_ADD_LO12;
    values.symbol_address = 0xf0000000U;
    values.thread_pointer_address = 0x70000000U;
    test_elf_write_u32(place, 0x91000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0x91000000U);
    assert(capture.count == 1U && strstr(capture.message, "signed 32-bit"));
    assert(strstr(capture.message, "target"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_TLSDESC_CALL;
    test_elf_write_u32(place, 0xd63f0040U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == 0xd63f0040U);
    assert(capture.count == 1U && strstr(capture.message, "signed 32-bit"));
    assert(strstr(capture.message, "R_AARCH64_TLSDESC_CALL"));

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_AARCH64_CALL26;
    values.place_address = 0x400000U;
    values.symbol_address = values.place_address + (1ULL << 27U);
    test_elf_write_u32(place, 0x94000000U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 4U, &values) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "out of range"));

    capture.count = 0U;
    relocation.type = LD_ELF_R_AARCH64_ABS64;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, 7U, &values) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "exceeds"));

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}
void test_ld_elf_reloc_aarch64(void) {
    test_elf_arm64_relocations();
}
