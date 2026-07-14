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

static void test_elf_riscv_relocations(void) {
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_RISCV64;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;

    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, &options);
    ld_elf_object_t object = {0};
    object.header.e_machine = LD_ELF_EM_RISCV;
    object.display_name = (char *) "riscv-relocation-test.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_relocation_t relocation = {0};
    ld_elf_reloc_scan_result_t scan = {0};
    ld_elf_reloc_values_t values = {
            .place_address = 0x1000U,
            .symbol_address = 0x2234U,
            .symbol_name = "target",
    };
    uint8_t place[16] = {0};

    relocation.type = LD_ELF_R_RISCV_CALL;
    test_elf_write_u32(place, 0x00000097U);
    test_elf_write_u32(place + 4U, 0x000080e7U);
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.pc_relative && scan.write_width == 8U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x00001097U);
    assert(test_elf_read_u32(place + 4U) == 0x234080e7U);

    /* Nature uses the same relocation for its AUIPC+ADDI address materializer. */
    values.symbol_address = 0x2235U;
    test_elf_write_u32(place, 0x00000f97U);
    test_elf_write_u32(place + 4U, 0x000f8f93U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x00001f97U);
    assert(test_elf_read_u32(place + 4U) == 0x235f8f93U);

    relocation.type = LD_ELF_R_RISCV_PCREL_LO12_I;
    values.place_address = 0x1004U;
    values.symbol_address = 0x1000U;
    values.paired_hi.present = true;
    values.paired_hi.type = LD_ELF_R_RISCV_PCREL_HI20;
    values.paired_hi.place_address = 0x1000U;
    values.paired_hi.symbol_address = 0x2234U;
    values.paired_hi.symbol_name = "data";
    test_elf_write_u32(place, 0x00050513U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0x23450513U);

    static const uint32_t riscv_tls_got_types[] = {
            LD_ELF_R_RISCV_TLS_GOT_HI20,
            LD_ELF_R_RISCV_TLS_GD_HI20,
    };
    static const char *const riscv_tls_got_names[] = {
            "R_RISCV_TLS_GOT_HI20",
            "R_RISCV_TLS_GD_HI20",
    };
    static const ld_elf_reloc_got_kind_t riscv_tls_got_kinds[] = {
            LD_ELF_RELOC_GOT_TP,
            LD_ELF_RELOC_GOT_TLSGD,
    };
    for (size_t i = 0;
         i < sizeof(riscv_tls_got_types) /
                     sizeof(riscv_tls_got_types[0]);
         i++) {
        relocation.type = riscv_tls_got_types[i];
        relocation.addend = 0;
        values.place_address = 0x1000U;
        values.got_entry_address = 0x2234U;
        assert(strcmp(ld_elf_relocation_name(LD_ARCH_RISCV64,
                                             relocation.type),
                      riscv_tls_got_names[i]) == 0);
        assert(ld_elf_relocation_got_kind(LD_ARCH_RISCV64,
                                          relocation.type) ==
               riscv_tls_got_kinds[i]);
        assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                      false, &scan) == LD_OK);
        assert(scan.needs_got && scan.pc_relative &&
               scan.write_width == 4U);
        test_elf_write_u32(place, 0x00000017U);
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, sizeof(place), &values) ==
               LD_OK);
        assert(test_elf_read_u32(place) == 0x00001017U);

        relocation.type = LD_ELF_R_RISCV_PCREL_LO12_I;
        values.place_address = 0x1004U;
        values.symbol_address = 0x1000U;
        values.paired_hi.present = true;
        values.paired_hi.type = riscv_tls_got_types[i];
        values.paired_hi.place_address = 0x1000U;
        values.paired_hi.symbol_address = 0x5000U;
        values.paired_hi.got_entry_address = 0x2234U;
        values.paired_hi.addend = 0;
        values.paired_hi.symbol_name = "tls_target";
        test_elf_write_u32(place, 0x00050513U);
        assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                       place, sizeof(place), &values) ==
               LD_OK);
        assert(test_elf_read_u32(place) == 0x23450513U);
    }

    relocation.type = LD_ELF_R_RISCV_RVC_BRANCH;
    values.place_address = 0x1000U;
    values.symbol_address = 0x1008U;
    place[0] = 0x01U;
    place[1] = 0xc1U;
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u16(place) == 0xc501U);

    relocation.type = LD_ELF_R_RISCV_TPREL_LO12_I;
    values.symbol_address = 0x2ff0U;
    values.thread_pointer_address = 0x3000U;
    test_elf_write_u32(place, 0x00050513U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == 0xff050513U);

    relocation.type = LD_ELF_R_RISCV_TLS_DTPREL64;
    relocation.addend = 0;
    values.symbol_address = 0x5008U;
    values.tls_block_address = 0x5000U;
    memset(place, 0, sizeof(place));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u64(place) == UINT64_C(0xfffffffffffff808));

    relocation.type = LD_ELF_R_RISCV_ALIGN;
    relocation.addend = 4;
    values.place_address = 0x1004U;
    test_elf_write_u32(place, 0x00000013U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);

    capture.count = 0U;
    capture.message[0] = '\0';
    relocation.type = LD_ELF_R_RISCV_PCREL_LO12_I;
    relocation.addend = 0;
    memset(&values.paired_hi, 0, sizeof(values.paired_hi));
    test_elf_write_u32(place, 0x00050513U);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) ==
           LD_RELOCATION_ERROR);
    assert(capture.count == 1U && strstr(capture.message, "missing paired"));

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

void test_ld_elf_reloc_riscv64(void) {
    test_elf_riscv_relocations();
}
