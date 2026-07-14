#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_internal.h"
#include "src/ld/ld_elf_reloc.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_aarch64_gotpage_lo15(void) {
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
    object.display_name = (char *) "aarch64-gotpage.o";
    ld_elf_section_t section = {0};
    section.name = ".text";
    ld_elf_relocation_t relocation = {
            .type = LD_ELF_R_AARCH64_LD64_GOTPAGE_LO15,
    };
    ld_elf_reloc_values_t values = {
            .got_entry_address = UINT64_C(0x423450),
            .got_base_address = UINT64_C(0x4208a0),
            .symbol_name = "target",
    };
    uint8_t place[4];

    assert(strcmp(ld_elf_relocation_name(LD_ARCH_ARM64, relocation.type),
                  "R_AARCH64_LD64_GOTPAGE_LO15") == 0);
    assert(ld_elf_relocation_supported(LD_ARCH_ARM64, relocation.type));
    assert(ld_elf_relocation_write_width(LD_ARCH_ARM64,
                                         relocation.type) == 4U);
    assert(ld_elf_relocation_got_kind(LD_ARCH_ARM64, relocation.type) ==
           LD_ELF_RELOC_GOT_ORDINARY);

    ld_elf_reloc_scan_result_t scan;
    assert(ld_elf_relocation_scan(&ctx, &object, &section, &relocation,
                                  false, &scan) == LD_OK);
    assert(scan.needs_got && !scan.needs_plt && !scan.pc_relative);
    assert(scan.write_width == 4U);

    test_elf_write_u32(place, UINT32_C(0xf9400000));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == UINT32_C(0xf95a2800));

    relocation.addend = 8;
    test_elf_write_u32(place, UINT32_C(0xf9400000));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == UINT32_C(0xf95a2c00));

    relocation.addend = 0;
    values.got_entry_address = UINT64_C(0x427ff8);
    test_elf_write_u32(place, UINT32_C(0xf9400000));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) == LD_OK);
    assert(test_elf_read_u32(place) == UINT32_C(0xf97ffc00));

    capture.count = 0U;
    capture.message[0] = '\0';
    values.got_entry_address = UINT64_C(0x428000);
    test_elf_write_u32(place, UINT32_C(0xf9400000));
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == UINT32_C(0xf9400000));
    assert(capture.count == 1U);
    assert(strstr(capture.message, "unsigned 15-bit") != NULL);
    assert(strstr(capture.message, "R_AARCH64_LD64_GOTPAGE_LO15") != NULL);

    capture.count = 0U;
    capture.message[0] = '\0';
    values.got_entry_address = UINT64_C(0x41fff8);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == UINT32_C(0xf9400000));
    assert(capture.count == 1U);
    assert(strstr(capture.message, "unsigned 15-bit") != NULL);

    capture.count = 0U;
    capture.message[0] = '\0';
    values.got_entry_address = UINT64_C(0x423451);
    assert(ld_elf_relocation_apply(&ctx, &object, &section, &relocation,
                                   place, sizeof(place), &values) ==
           LD_RELOCATION_ERROR);
    assert(test_elf_read_u32(place) == UINT32_C(0xf9400000));
    assert(capture.count == 1U);
    assert(strstr(capture.message, "aligned to 8") != NULL);

    ld_elf_context_deinit(&ctx);
    ld_options_deinit(&options);
}

void test_ld_elf_aarch64_got(void) {
    test_aarch64_gotpage_lo15();
}
