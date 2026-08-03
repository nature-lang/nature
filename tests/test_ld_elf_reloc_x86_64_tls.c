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

static void test_elf_amd64_tls_relaxations(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;

    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_symbol_t symbols[2] = {0};
    symbols[0].index = 0U;
    symbols[0].name = "tls_value";
    symbols[0].binding = LD_ELF_STB_GLOBAL;
    symbols[0].type = LD_ELF_STT_TLS;
    symbols[0].entry.st_shndx = 1U;
    symbols[1].index = 1U;
    symbols[1].name = "__tls_get_addr";
    symbols[1].binding = LD_ELF_STB_GLOBAL;
    symbols[1].type = LD_ELF_STT_FUNC;
    symbols[1].entry.st_shndx = LD_ELF_SHN_UNDEF;

    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_X86_64;
    object.display_name = (char *) "amd64-tls-relax-test.o";
    object.symbols = symbols;
    object.symbol_count = 2U;

    ld_elf_section_t section = {0};
    section.name = ".text";
    object.sections = &section;
    object.section_count = 1U;
    ld_elf_reloc_values_t values = {
            .symbol_address = 0x403000U,
            .thread_pointer_address = 0x403010U,
            .tls_block_address = 0x402ff0U,
            .symbol_name = "tls_value",
    };
    ld_elf_reloc_scan_result_t scan = {0};

    static const uint8_t gd_input[17] = {
            0x50U,
            0x66U,
            0x48U,
            0x8dU,
            0x3dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x66U,
            0x66U,
            0x48U,
            0xe8U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    static const uint8_t gd_expected[17] = {
            0x50U,
            0x64U,
            0x48U,
            0x8bU,
            0x04U,
            0x25U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x48U,
            0x81U,
            0xc0U,
            0xf0U,
            0xffU,
            0xffU,
            0xffU,
    };
    ld_elf_relocation_t gd_relocations[2] = {
            {
                    .offset = 5U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_TLSGD,
                    .addend = -4,
                    .relocation_section_index = 7U,
            },
            {
                    .offset = 13U,
                    .symbol_index = 1U,
                    .type = LD_ELF_R_X86_64_PLT32,
                    .addend = -4,
                    .relocation_section_index = 7U,
            },
    };
    section.data = gd_input;
    section.data_size = sizeof(gd_input);
    section.header.sh_size = sizeof(gd_input);
    section.relocations = gd_relocations;
    section.relocation_count = 2U;
    assert(strcmp(ld_elf_relocation_name(LD_ARCH_AMD64,
                                         LD_ELF_R_X86_64_TLSGD),
                  "R_X86_64_TLSGD") == 0);
    assert(ld_elf_relocation_supported(LD_ARCH_AMD64,
                                       LD_ELF_R_X86_64_TLSGD));
    assert(ld_elf_relocation_scan(&ctx, &object, &section,
                                  &gd_relocations[0], false, &scan) == LD_OK);
    assert(scan.pc_relative && !scan.needs_got && !scan.needs_plt &&
           scan.write_width == 4U);
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    assert(gd_relocations[0].x86_tls_pair_index == 1U);
    assert(!gd_relocations[0].x86_tls_pair_follower);
    assert(gd_relocations[1].x86_tls_pair_follower);
    uint8_t gd_output[sizeof(gd_input)];
    memcpy(gd_output, gd_input, sizeof(gd_output));
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &gd_relocations[0],
                   &gd_relocations[1], gd_output, sizeof(gd_output), 5U,
                   &values) == LD_OK);
    assert(memcmp(gd_output, gd_expected, sizeof(gd_output)) == 0);

    static const uint8_t gd_indirect_input[17] = {
            0x50U,
            0x66U,
            0x48U,
            0x8dU,
            0x3dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0x66U,
            0x48U,
            0xffU,
            0x15U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    ld_elf_relocation_t gd_indirect_relocations[2] = {
            gd_relocations[0],
            gd_relocations[1],
    };
    gd_indirect_relocations[1].type = LD_ELF_R_X86_64_GOTPCRELX;
    section.data = gd_indirect_input;
    section.data_size = sizeof(gd_indirect_input);
    section.header.sh_size = sizeof(gd_indirect_input);
    section.relocations = gd_indirect_relocations;
    section.relocation_count = 2U;
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    memcpy(gd_output, gd_indirect_input, sizeof(gd_output));
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &gd_indirect_relocations[0],
                   &gd_indirect_relocations[1], gd_output,
                   sizeof(gd_output), 5U, &values) == LD_OK);
    assert(memcmp(gd_output, gd_expected, sizeof(gd_output)) == 0);

    /* The resolver relocation is found by section, offset and identity, not
     * by assuming the relocation array is ordered or adjacent. */
    static const uint8_t ld_input[13] = {
            0x50U,
            0x48U,
            0x8dU,
            0x3dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0xe8U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    static const uint8_t ld_expected[13] = {
            0x50U,
            0x31U,
            0xc0U,
            0x64U,
            0x48U,
            0x8bU,
            0x00U,
            0x48U,
            0x2dU,
            0x20U,
            0x00U,
            0x00U,
            0x00U,
    };
    ld_elf_relocation_t ld_relocations[3] = {
            {
                    .offset = 4U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_TLSLD,
                    .addend = -4,
                    .relocation_section_index = 9U,
            },
            {
                    .offset = 0U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_NONE,
                    .relocation_section_index = 9U,
            },
            {
                    .offset = 9U,
                    .symbol_index = 1U,
                    .type = LD_ELF_R_X86_64_PC32,
                    .addend = -4,
                    .relocation_section_index = 9U,
            },
    };
    section.data = ld_input;
    section.data_size = sizeof(ld_input);
    section.header.sh_size = sizeof(ld_input);
    section.relocations = ld_relocations;
    section.relocation_count = 3U;
    assert(strcmp(ld_elf_relocation_name(LD_ARCH_AMD64,
                                         LD_ELF_R_X86_64_TLSLD),
                  "R_X86_64_TLSLD") == 0);
    assert(ld_elf_relocation_scan(&ctx, &object, &section,
                                  &ld_relocations[0], false, &scan) == LD_OK);
    assert(scan.pc_relative && !scan.needs_got && scan.write_width == 4U);
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    assert(ld_relocations[0].x86_tls_pair_index == 2U);
    assert(ld_relocations[2].x86_tls_pair_follower);
    uint8_t ld_output[sizeof(ld_input)];
    memcpy(ld_output, ld_input, sizeof(ld_output));
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &ld_relocations[0],
                   &ld_relocations[2], ld_output, sizeof(ld_output), 4U,
                   &values) == LD_OK);
    assert(memcmp(ld_output, ld_expected, sizeof(ld_output)) == 0);

    static const uint8_t ld_indirect_input[14] = {
            0x50U,
            0x48U,
            0x8dU,
            0x3dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0xffU,
            0x15U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    static const uint8_t ld_indirect_expected[14] = {
            0x50U,
            0x31U,
            0xc0U,
            0x64U,
            0x48U,
            0x8bU,
            0x00U,
            0x48U,
            0x2dU,
            0x20U,
            0x00U,
            0x00U,
            0x00U,
            0x90U,
    };
    ld_elf_relocation_t ld_indirect_relocations[2] = {
            {
                    .offset = 4U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_TLSLD,
                    .addend = -4,
                    .relocation_section_index = 10U,
            },
            {
                    .offset = 10U,
                    .symbol_index = 1U,
                    .type = LD_ELF_R_X86_64_GOTPCRELX,
                    .addend = -4,
                    .relocation_section_index = 10U,
            },
    };
    section.data = ld_indirect_input;
    section.data_size = sizeof(ld_indirect_input);
    section.header.sh_size = sizeof(ld_indirect_input);
    section.relocations = ld_indirect_relocations;
    section.relocation_count = 2U;
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    uint8_t ld_indirect_output[sizeof(ld_indirect_input)];
    memcpy(ld_indirect_output, ld_indirect_input,
           sizeof(ld_indirect_output));
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &ld_indirect_relocations[0],
                   &ld_indirect_relocations[1], ld_indirect_output,
                   sizeof(ld_indirect_output), 4U, &values) == LD_OK);
    assert(memcmp(ld_indirect_output, ld_indirect_expected,
                  sizeof(ld_indirect_output)) == 0);

    static const uint8_t tlsdesc_input[10] = {
            0x50U,
            0x48U,
            0x8dU,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0xffU,
            0x10U,
    };
    static const uint8_t tlsdesc_expected[10] = {
            0x50U,
            0x48U,
            0xc7U,
            0xc0U,
            0xf0U,
            0xffU,
            0xffU,
            0xffU,
            0x66U,
            0x90U,
    };
    ld_elf_relocation_t tlsdesc_relocations[2] = {
            {
                    .offset = 4U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_GOTPC32_TLSDESC,
                    .addend = -4,
                    .relocation_section_index = 12U,
            },
            {
                    .offset = 8U,
                    .symbol_index = 0U,
                    .type = LD_ELF_R_X86_64_TLSDESC_CALL,
                    .addend = 0,
                    .relocation_section_index = 12U,
            },
    };
    section.data = tlsdesc_input;
    section.data_size = sizeof(tlsdesc_input);
    section.header.sh_size = sizeof(tlsdesc_input);
    section.relocations = tlsdesc_relocations;
    section.relocation_count = 2U;
    assert(strcmp(ld_elf_relocation_name(
                          LD_ARCH_AMD64,
                          LD_ELF_R_X86_64_GOTPC32_TLSDESC),
                  "R_X86_64_GOTPC32_TLSDESC") == 0);
    assert(strcmp(ld_elf_relocation_name(LD_ARCH_AMD64,
                                         LD_ELF_R_X86_64_TLSDESC_CALL),
                  "R_X86_64_TLSDESC_CALL") == 0);
    assert(!ld_elf_relocation_supported(LD_ARCH_AMD64,
                                        LD_ELF_R_X86_64_TLSDESC));
    assert(ld_elf_relocation_scan(&ctx, &object, &section,
                                  &tlsdesc_relocations[0], false,
                                  &scan) == LD_OK);
    assert(scan.pc_relative && !scan.needs_got && scan.write_width == 4U);
    assert(ld_elf_relocation_scan(&ctx, &object, &section,
                                  &tlsdesc_relocations[1], false,
                                  &scan) == LD_OK);
    assert(!scan.pc_relative && !scan.needs_got && scan.write_width == 0U);
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    assert(tlsdesc_relocations[0].x86_tls_pair_index == 1U);
    assert(tlsdesc_relocations[1].x86_tls_pair_follower);
    uint8_t tlsdesc_output[sizeof(tlsdesc_input)];
    memcpy(tlsdesc_output, tlsdesc_input, sizeof(tlsdesc_output));
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &tlsdesc_relocations[0],
                   &tlsdesc_relocations[1], tlsdesc_output,
                   sizeof(tlsdesc_output), 4U, &values) == LD_OK);
    assert(memcmp(tlsdesc_output, tlsdesc_expected,
                  sizeof(tlsdesc_output)) == 0);

    ld_elf_relocation_t tlsdesc_wrong_symbol[2] = {
            tlsdesc_relocations[0],
            tlsdesc_relocations[1],
    };
    tlsdesc_wrong_symbol[1].symbol_index = 1U;
    section.relocations = tlsdesc_wrong_symbol;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "same STT_TLS"));

    ld_elf_relocation_t orphan_tlsdesc_call = tlsdesc_relocations[1];
    section.relocations = &orphan_tlsdesc_call;
    section.relocation_count = 1U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "orphan"));

    ld_elf_relocation_t missing_tlsdesc_call = tlsdesc_relocations[0];
    section.relocations = &missing_tlsdesc_call;
    section.relocation_count = 1U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U &&
           strstr(capture.message, "missing paired R_X86_64_TLSDESC_CALL"));

    ld_elf_relocation_t invalid_module_base = {
            .offset = 0U,
            .symbol_index = 0U,
            .type = LD_ELF_R_X86_64_PC32,
            .addend = -4,
            .relocation_section_index = 13U,
    };
    static const uint8_t module_base_data[4] = {0};
    symbols[0].name = "_TLS_MODULE_BASE_";
    symbols[0].entry.st_shndx = LD_ELF_SHN_UNDEF;
    section.data = module_base_data;
    section.data_size = sizeof(module_base_data);
    section.header.sh_size = sizeof(module_base_data);
    section.relocations = &invalid_module_base;
    section.relocation_count = 1U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U &&
           strstr(capture.message, "complete TLSDESC pair"));
    symbols[0].name = "tls_value";
    symbols[0].entry.st_shndx = 1U;

    /* Zig relaxes the MOV form in place. REX.R becomes REX.B so extended
     * destination registers retain their identity. */
    static const uint8_t gottpoff_input[7] = {
            0x4cU,
            0x8bU,
            0x0dU,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    static const uint8_t gottpoff_expected[7] = {
            0x49U,
            0xc7U,
            0xc1U,
            0xf0U,
            0xffU,
            0xffU,
            0xffU,
    };
    ld_elf_relocation_t gottpoff = {
            .offset = 3U,
            .symbol_index = 0U,
            .type = LD_ELF_R_X86_64_GOTTPOFF,
            .addend = -4,
            .relocation_section_index = 11U,
    };
    section.data = gottpoff_input;
    section.data_size = sizeof(gottpoff_input);
    section.header.sh_size = sizeof(gottpoff_input);
    section.relocations = &gottpoff;
    section.relocation_count = 1U;
    assert(strcmp(ld_elf_relocation_name(LD_ARCH_AMD64,
                                         LD_ELF_R_X86_64_GOTTPOFF),
                  "R_X86_64_GOTTPOFF") == 0);
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &gottpoff,
                                  false, &scan) == LD_OK);
    assert(scan.pc_relative && scan.needs_got && scan.write_width == 4U);
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    assert(gottpoff.x86_gottpoff_relax);
    uint8_t gottpoff_output[sizeof(gottpoff_input)];
    memcpy(gottpoff_output, gottpoff_input, sizeof(gottpoff_output));
    assert(ld_elf_relocation_apply_x86_gottpoff(
                   &ctx, &object, &section, &gottpoff, gottpoff_output,
                   sizeof(gottpoff_output), 3U, &values) == LD_OK);
    assert(memcmp(gottpoff_output, gottpoff_expected,
                  sizeof(gottpoff_output)) == 0);

    /* The ADD form is deliberately not rewritten. Its relocation remains a
     * normal G + A - P expression against a dedicated GOTTP entry. */
    static const uint8_t gottpoff_add_input[7] = {
            0x48U,
            0x03U,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
    };
    section.data = gottpoff_add_input;
    section.data_size = sizeof(gottpoff_add_input);
    section.header.sh_size = sizeof(gottpoff_add_input);
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    assert(!gottpoff.x86_gottpoff_relax);
    uint8_t gottpoff_add_output[sizeof(gottpoff_add_input)];
    memcpy(gottpoff_add_output, gottpoff_add_input,
           sizeof(gottpoff_add_output));
    values.place_address = 0x401003U;
    values.got_entry_address = 0x403000U;
    assert(ld_elf_relocation_apply(
                   &ctx, &object, &section, &gottpoff,
                   gottpoff_add_output + 3U, 4U, &values) == LD_OK);
    assert(test_elf_read_u32(gottpoff_add_output + 3U) == 0x1ff9U);

    /* Overflow and malformed input diagnostics are failure-atomic. */
    section.data = gd_input;
    section.data_size = sizeof(gd_input);
    section.header.sh_size = sizeof(gd_input);
    section.relocations = gd_relocations;
    section.relocation_count = 2U;
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_OK);
    memcpy(gd_output, gd_input, sizeof(gd_output));
    uint8_t before[sizeof(gd_output)];
    memcpy(before, gd_output, sizeof(before));
    capture.count = 0U;
    capture.message[0] = '\0';
    values.symbol_address = 0x100000000ULL;
    values.thread_pointer_address = 0U;
    assert(ld_elf_relocation_apply_x86_tls_pair(
                   &ctx, &object, &section, &gd_relocations[0],
                   &gd_relocations[1], gd_output, sizeof(gd_output), 5U,
                   &values) == LD_RELOCATION_ERROR);
    assert(memcmp(gd_output, before, sizeof(gd_output)) == 0);
    assert(capture.count == 1U && strstr(capture.message, "signed 32-bit"));
    assert(strstr(capture.message, "tls_value"));

    ld_elf_relocation_t missing_helper = gd_relocations[0];
    section.relocations = &missing_helper;
    section.relocation_count = 1U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U &&
           strstr(capture.message, "missing __tls_get_addr"));

    ld_elf_relocation_t wrong_helper[2] = {
            gd_relocations[0],
            gd_relocations[1],
    };
    symbols[1].name = "not_the_tls_resolver";
    section.relocations = wrong_helper;
    section.relocation_count = 2U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "__tls_get_addr"));
    symbols[1].name = "__tls_get_addr";

    uint8_t malformed_gd[sizeof(gd_input)];
    memcpy(malformed_gd, gd_input, sizeof(malformed_gd));
    malformed_gd[1] = 0x90U;
    section.data = malformed_gd;
    section.data_size = sizeof(malformed_gd);
    section.header.sh_size = sizeof(malformed_gd);
    section.relocations = gd_relocations;
    section.relocation_count = 2U;
    capture.count = 0U;
    capture.message[0] = '\0';
    assert(ld_elf_relocation_prepare_x86_tls_sequences(&ctx, &object) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "TLSGD"));

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

void test_ld_elf_reloc_x86_64_tls(void) {
    test_elf_amd64_tls_relaxations();
}
