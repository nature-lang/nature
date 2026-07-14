#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_relro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t size;
    uint64_t alignment;
    uint64_t entry_size;
    uint8_t fill;
    bool relro_fixture_only;
    size_t file_offset;
    uint32_t name_offset;
} test_elf_relro_section_t;

static uint32_t test_elf_relro_entry_word(uint16_t machine) {
    switch (machine) {
        case LD_ELF_EM_X86_64:
            return 0x000000c3U;
        case LD_ELF_EM_AARCH64:
            return 0xd65f03c0U;
        case LD_ELF_EM_RISCV:
            return 0x00008067U;
    }
    assert(false);
    return 0U;
}

static uint32_t test_elf_relro_absolute_relocation(uint16_t machine) {
    switch (machine) {
        case LD_ELF_EM_X86_64:
            return LD_ELF_R_X86_64_64;
        case LD_ELF_EM_AARCH64:
            return LD_ELF_R_AARCH64_ABS64;
        case LD_ELF_EM_RISCV:
            return LD_ELF_R_RISCV_64;
    }
    assert(false);
    return 0U;
}

static uint8_t *make_test_elf_relro_object(uint16_t machine,
                                           bool include_relro,
                                           size_t *result_size) {
    const uint64_t alloc_write = LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE;
    const uint64_t tls = alloc_write | LD_ELF_SHF_TLS;
    test_elf_relro_section_t candidates[] = {
            {".text", LD_ELF_SHT_PROGBITS,
             LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, 4U, 4U, 0U, 0U,
             false, 0U, 0U},
            {".tdata", LD_ELF_SHT_PROGBITS, tls, 8U, 16U, 0U, 0x11U,
             true, 0U, 0U},
            {".tbss", LD_ELF_SHT_NOBITS, tls, 32U, 16U, 0U, 0U,
             true, 0U, 0U},
            {".preinit_array", LD_ELF_SHT_PREINIT_ARRAY, alloc_write, 8U,
             8U, 8U, 0x21U, true, 0U, 0U},
            {".init_array", LD_ELF_SHT_INIT_ARRAY, alloc_write, 8U, 8U,
             8U, 0x31U, true, 0U, 0U},
            {".fini_array", LD_ELF_SHT_FINI_ARRAY, alloc_write, 8U, 8U,
             8U, 0x41U, true, 0U, 0U},
            {".data.rel.ro", LD_ELF_SHT_PROGBITS, alloc_write, 8U, 8U,
             0U, 0x51U, true, 0U, 0U},
            {".bss.rel.ro", LD_ELF_SHT_NOBITS, alloc_write, 16U, 8U, 0U,
             0U, true, 0U, 0U},
            {".got", LD_ELF_SHT_PROGBITS, alloc_write, 8U, 8U, 0U, 0x61U,
             true, 0U, 0U},
            {".data", LD_ELF_SHT_PROGBITS, alloc_write, 8U, 8U, 0U, 0x71U,
             false, 0U, 0U},
            {".bss", LD_ELF_SHT_NOBITS, alloc_write, 24U, 8U, 0U, 0U,
             false, 0U, 0U},
    };
    test_elf_relro_section_t sections[sizeof(candidates) /
                                      sizeof(candidates[0])];
    size_t section_count = 0U;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!include_relro && candidates[i].relro_fixture_only) continue;
        sections[section_count++] = candidates[i];
    }

    char section_names[256] = {0};
    size_t section_names_size = 1U;
    for (size_t i = 0; i < section_count; i++) {
        sections[i].name_offset = test_elf_append_name(
                section_names, sizeof(section_names), &section_names_size,
                sections[i].name);
    }
    uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");
    uint32_t rela_name = 0U;
    if (include_relro) {
        rela_name = test_elf_append_name(
                section_names, sizeof(section_names), &section_names_size,
                ".rela.bss.rel.ro");
    }

    size_t cursor = LD_ELF64_EHDR_SIZE;
    uint16_t bss_rel_ro_index = 0U;
    for (size_t i = 0; i < section_count; i++) {
        cursor = test_elf_align(cursor, (size_t) sections[i].alignment);
        sections[i].file_offset = cursor;
        if (strcmp(sections[i].name, ".bss.rel.ro") == 0) {
            assert(i < UINT16_MAX);
            bss_rel_ro_index = (uint16_t) i + 1U;
        }
        if (sections[i].type != LD_ELF_SHT_NOBITS)
            cursor += (size_t) sections[i].size;
    }
    const size_t rela_offset = test_elf_align(cursor, 8U);
    if (include_relro) cursor = rela_offset + LD_ELF64_RELA_SIZE;
    const size_t symtab_offset = test_elf_align(cursor, 8U);
    const size_t symbol_count = 2U;
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    static const char symbol_names[] = "\0_start";
    const size_t shstrtab_offset = strtab_offset + sizeof(symbol_names);
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    size_t input_section_count = section_count + (include_relro ? 1U : 0U);
    assert(input_section_count <= UINT16_MAX - 4U);
    const uint16_t output_section_count =
            (uint16_t) (input_section_count + 4U);
    const uint16_t rela_index = include_relro
                                        ? (uint16_t) section_count + 1U
                                        : 0U;
    const uint16_t symtab_index = (uint16_t) input_section_count + 1U;
    const uint16_t strtab_index = symtab_index + 1U;
    const uint16_t shstrtab_index = strtab_index + 1U;
    const size_t size = section_table_offset +
                        (size_t) output_section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);

    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, machine);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, output_section_count);
    test_elf_write_u16(bytes + 62U, shstrtab_index);

    for (size_t i = 0; i < section_count; i++) {
        if (sections[i].type == LD_ELF_SHT_NOBITS) continue;
        if (strcmp(sections[i].name, ".text") == 0) {
            test_elf_write_u32(bytes + sections[i].file_offset,
                               test_elf_relro_entry_word(machine));
        } else {
            memset(bytes + sections[i].file_offset, sections[i].fill,
                   (size_t) sections[i].size);
        }
    }
    memcpy(bytes + strtab_offset, symbol_names, sizeof(symbol_names));
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);
    if (include_relro) {
        assert(bss_rel_ro_index != 0U && rela_index != 0U);
        test_elf_write_u64(bytes + rela_offset, 0U);
        test_elf_write_u64(
                bytes + rela_offset + 8U,
                LD_ELF_RELA_INFO(
                        1U, test_elf_relro_absolute_relocation(machine)));
        test_elf_write_u64(bytes + rela_offset + 16U, 0U);
    }

    uint8_t *start = bytes + symtab_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, 1U);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, 1U);
    test_elf_write_u64(start + 16U, 4U);

    uint8_t *section_headers = bytes + section_table_offset;
    for (size_t i = 0; i < section_count; i++) {
        test_elf_write_section(
                section_headers + (i + 1U) * LD_ELF64_SHDR_SIZE,
                sections[i].name_offset, sections[i].type,
                sections[i].flags, sections[i].file_offset,
                sections[i].size, 0U, 0U, sections[i].alignment,
                sections[i].entry_size);
    }
    if (include_relro) {
        test_elf_write_section(
                section_headers +
                        (size_t) rela_index * LD_ELF64_SHDR_SIZE,
                rela_name, LD_ELF_SHT_RELA, 0U, rela_offset,
                LD_ELF64_RELA_SIZE, symtab_index, bss_rel_ro_index, 8U,
                LD_ELF64_RELA_SIZE);
    }
    test_elf_write_section(
            section_headers + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 1U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            section_headers + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, strtab_offset,
            sizeof(symbol_names), 0U, 0U, 1U, 0U);
    test_elf_write_section(
            section_headers + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static const uint8_t *find_test_elf_program(const uint8_t *image,
                                            size_t image_size,
                                            uint32_t type,
                                            size_t *match_count) {
    assert(image_size >= LD_ELF64_EHDR_SIZE);
    uint64_t offset = test_elf_read_u64(image + 32U);
    uint16_t count = test_elf_read_u16(image + 56U);
    assert(offset <= image_size &&
           (uint64_t) count * LD_ELF64_PHDR_SIZE <= image_size - offset);
    const uint8_t *found = NULL;
    size_t matches = 0U;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *program = image + (size_t) offset +
                                 (size_t) i * LD_ELF64_PHDR_SIZE;
        if (test_elf_read_u32(program) != type) continue;
        if (!found) found = program;
        matches++;
    }
    if (match_count) *match_count = matches;
    return found;
}

static int link_test_elf_relro_object(ld_arch_t arch, const char *input_path,
                                      const char *output_path,
                                      diagnostic_capture_t *capture) {
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = arch;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    int result = ld_link(&options);
    ld_options_deinit(&options);
    return result;
}

static void test_relro_classification_and_plan(void) {
    const uint64_t write = LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE;
    const uint64_t tls = write | LD_ELF_SHF_TLS;
    assert(ld_elf_relro_section_is_protected(
            ".tdata", LD_ELF_SHT_PROGBITS, tls, 8U));
    assert(!ld_elf_relro_section_is_protected(
            ".tbss", LD_ELF_SHT_NOBITS, tls, 8U));
    assert(ld_elf_relro_section_is_transparent(
            ".tbss", LD_ELF_SHT_NOBITS, tls, 8U));
    assert(ld_elf_relro_section_is_protected(
            ".init_array", LD_ELF_SHT_INIT_ARRAY, write, 8U));
    assert(ld_elf_relro_section_is_protected(
            ".data.rel.ro.local", LD_ELF_SHT_PROGBITS, write, 8U));
    assert(ld_elf_relro_section_is_protected(
            ".bss.rel.ro.foo", LD_ELF_SHT_PROGBITS, write, 8U));
    assert(ld_elf_relro_section_is_protected(
            ".got", LD_ELF_SHT_PROGBITS, write, 8U));
    assert(!ld_elf_relro_section_is_protected(
            ".got.plt", LD_ELF_SHT_PROGBITS, write, 8U));
    assert(!ld_elf_relro_section_is_protected(
            ".data", LD_ELF_SHT_PROGBITS, write, 8U));

    ld_elf_relro_plan_t plan;
    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_add_section(
                   &plan, ".tdata", LD_ELF_SHT_PROGBITS, tls,
                   0x4000U, 0x404000U, 8U, 8U) == LD_ELF_RELRO_OK);
    assert(ld_elf_relro_add_section(
                   &plan, ".init_array", LD_ELF_SHT_INIT_ARRAY, write,
                   0x4008U, 0x404008U, 8U, 8U) == LD_ELF_RELRO_OK);
    assert(ld_elf_relro_add_section(
                   &plan, ".got", LD_ELF_SHT_PROGBITS, write,
                   0x4020U, 0x404020U, 8U, 8U) == LD_ELF_RELRO_OK);
    uint64_t next_file = 0U;
    uint64_t next_address = 0U;
    assert(ld_elf_relro_finalize(
                   &plan, 0x1000U, 0x4028U, 0x404028U,
                   &next_file, &next_address) == LD_ELF_RELRO_OK);
    assert(plan.present && plan.finalized);
    assert(plan.file_offset == 0x4000U && plan.address == 0x404000U);
    assert(plan.file_size == 0x28U && plan.memory_size == 0x1000U);
    assert(plan.protection_end == 0x405000U);
    assert(next_file == 0x5000U && next_address == 0x405000U);
    assert(ld_elf_relro_finalize(
                   &plan, 0x1000U, next_file, next_address,
                   &next_file, &next_address) == LD_ELF_RELRO_FINALIZED);
    assert(ld_elf_relro_add_section(
                   &plan, ".got", LD_ELF_SHT_PROGBITS, write,
                   0x5000U, 0x405000U, 8U, 8U) ==
           LD_ELF_RELRO_FINALIZED);
}

static void test_relro_plan_failures(void) {
    const uint64_t write = LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE;
    ld_elf_relro_plan_t plan;
    uint64_t next_file;
    uint64_t next_address;

    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_finalize(
                   &plan, 3U, 0U, 0U, &next_file, &next_address) ==
           LD_ELF_RELRO_INVALID_ALIGNMENT);

    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_add_section(
                   &plan, ".data", LD_ELF_SHT_PROGBITS, write,
                   0U, 0U, 8U, 8U) ==
           LD_ELF_RELRO_SECTION_NOT_PROTECTED);
    assert(ld_elf_relro_add_section(
                   &plan, ".got", LD_ELF_SHT_PROGBITS, write,
                   UINT64_MAX - 3U, 0x400000U, 8U, 8U) ==
           LD_ELF_RELRO_LAYOUT_OVERFLOW);

    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_add_section(
                   &plan, ".got", LD_ELF_SHT_PROGBITS, write,
                   0x4008U, 0x404000U, 8U, 8U) == LD_ELF_RELRO_OK);
    assert(ld_elf_relro_finalize(
                   &plan, 0x1000U, 0x4010U, 0x404008U,
                   &next_file, &next_address) ==
           LD_ELF_RELRO_INCONGRUENT_LAYOUT);

    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_add_section(
                   &plan, ".got", LD_ELF_SHT_PROGBITS, write,
                   0x4000U, 0x404000U, 16U, 16U) == LD_ELF_RELRO_OK);
    assert(ld_elf_relro_add_section(
                   &plan, ".data.rel.ro", LD_ELF_SHT_PROGBITS, write,
                   0x4008U, 0x404008U, 8U, 8U) ==
           LD_ELF_RELRO_NON_MONOTONIC_LAYOUT);

    ld_elf_relro_plan_init(&plan);
    assert(ld_elf_relro_finalize(
                   &plan, 0x1000U, 0x123U, 0x456U,
                   &next_file, &next_address) == LD_ELF_RELRO_OK);
    assert(!plan.present && plan.finalized);
    assert(next_file == 0x123U && next_address == 0x456U);
}

static void test_relro_output_for_arch(ld_arch_t arch, uint16_t machine,
                                       const char *tag) {
    size_t object_size;
    uint8_t *object = make_test_elf_relro_object(
            machine, true, &object_size);
    char object_path[] = "/tmp/nature-ld-relro-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    char default_output[128];
    int length = snprintf(default_output, sizeof(default_output),
                          "/tmp/nature-ld-relro-%s", tag);
    assert(length > 0 && (size_t) length < sizeof(default_output));
    const char *retained = arch == LD_ARCH_ARM64
                                   ? getenv("NATURE_TEST_ELF_RELRO_OUTPUT")
                                   : NULL;
    const char *output_path = retained && retained[0]
                                      ? retained
                                      : default_output;
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_relro_object(
                   arch, object_path, output_path, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(test_elf_read_u16(image + 18U) == machine);
    assert(test_elf_read_u16(image + 56U) == 6U);

    size_t relro_count;
    const uint8_t *relro = find_test_elf_program(
            image, image_size, LD_ELF_PT_GNU_RELRO, &relro_count);
    assert(relro != NULL && relro_count == 1U);
    assert(test_elf_read_u32(relro + 4U) == LD_ELF_PF_R);
    assert(test_elf_read_u64(relro + 48U) == 1U);

    const uint8_t *tdata = test_elf_find_output_section(
            image, image_size, ".tdata");
    const uint8_t *tbss = test_elf_find_output_section(
            image, image_size, ".tbss");
    const uint8_t *preinit = test_elf_find_output_section(
            image, image_size, ".preinit_array");
    const uint8_t *init = test_elf_find_output_section(
            image, image_size, ".init_array");
    const uint8_t *fini = test_elf_find_output_section(
            image, image_size, ".fini_array");
    const uint8_t *data_rel_ro = test_elf_find_output_section(
            image, image_size, ".data.rel.ro");
    const uint8_t *bss_rel_ro = test_elf_find_output_section(
            image, image_size, ".bss.rel.ro");
    const uint8_t *got = test_elf_find_output_section(
            image, image_size, ".got");
    const uint8_t *data = test_elf_find_output_section(
            image, image_size, ".data");
    const uint8_t *bss = test_elf_find_output_section(
            image, image_size, ".bss");
    assert(tdata && tbss && preinit && init && fini && data_rel_ro &&
           bss_rel_ro && got && data && bss);

    const uint8_t *protected_sections[] = {
            tdata,
            preinit,
            init,
            fini,
            data_rel_ro,
            bss_rel_ro,
            got,
    };
    uint64_t relro_offset = test_elf_read_u64(relro + 8U);
    uint64_t relro_address = test_elf_read_u64(relro + 16U);
    uint64_t relro_file_size = test_elf_read_u64(relro + 32U);
    uint64_t relro_memory_size = test_elf_read_u64(relro + 40U);
    uint64_t relro_file_end = relro_offset + relro_file_size;
    uint64_t relro_memory_end = relro_address + relro_memory_size;
    uint64_t page_size = arch == LD_ARCH_ARM64 ? 0x10000U : 0x1000U;
    assert(relro_offset == test_elf_read_u64(tdata + 24U));
    assert(relro_address == test_elf_read_u64(tdata + 16U));
    assert(relro_memory_size != 0U &&
           relro_memory_size % page_size == 0U);
    for (size_t i = 0; i < sizeof(protected_sections) /
                                   sizeof(protected_sections[0]);
         i++) {
        const uint8_t *section = protected_sections[i];
        uint64_t offset = test_elf_read_u64(section + 24U);
        uint64_t address = test_elf_read_u64(section + 16U);
        uint64_t size = test_elf_read_u64(section + 32U);
        assert(offset >= relro_offset && offset + size <= relro_file_end);
        assert(address >= relro_address && address + size <= relro_memory_end);
    }
    assert(test_elf_read_u64(got + 24U) +
                   test_elf_read_u64(got + 32U) ==
           relro_file_end);

    uint64_t data_offset = test_elf_read_u64(data + 24U);
    uint64_t data_address = test_elf_read_u64(data + 16U);
    assert(data_offset == relro_offset + relro_memory_size);
    assert(data_address == relro_memory_end);
    assert(data_offset % page_size == 0U &&
           data_address % page_size == 0U);
    assert(test_elf_read_u64(bss + 16U) >= data_address +
                                                   test_elf_read_u64(data + 32U));

    assert(test_elf_read_u32(tbss + 4U) == LD_ELF_SHT_NOBITS);
    assert(test_elf_read_u32(bss_rel_ro + 4U) == LD_ELF_SHT_PROGBITS);
    uint64_t bss_rel_ro_offset = test_elf_read_u64(bss_rel_ro + 24U);
    uint64_t bss_rel_ro_size = test_elf_read_u64(bss_rel_ro + 32U);
    assert(bss_rel_ro_offset <= image_size &&
           bss_rel_ro_size <= image_size - bss_rel_ro_offset);
    assert(bss_rel_ro_size >= sizeof(uint64_t));
    assert(test_elf_read_u64(image + (size_t) bss_rel_ro_offset) ==
           test_elf_read_u64(image + 24U));
    for (uint64_t i = sizeof(uint64_t); i < bss_rel_ro_size; i++)
        assert(image[(size_t) (bss_rel_ro_offset + i)] == 0U);

    size_t tls_count;
    const uint8_t *tls = find_test_elf_program(
            image, image_size, LD_ELF_PT_TLS, &tls_count);
    assert(tls && tls_count == 1U);
    assert(test_elf_read_u64(tls + 8U) ==
           test_elf_read_u64(tdata + 24U));
    assert(test_elf_read_u64(tls + 16U) ==
           test_elf_read_u64(tdata + 16U));
    assert(test_elf_read_u64(tls + 32U) ==
           test_elf_read_u64(tdata + 32U));
    assert(test_elf_read_u64(tls + 40U) >=
           test_elf_read_u64(tdata + 32U) +
                   test_elf_read_u64(tbss + 32U));

    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    size_t rw_load_count = 0U;
    for (uint16_t i = 0; i < program_count; i++) {
        const uint8_t *program = image + (size_t) program_offset +
                                 (size_t) i * LD_ELF64_PHDR_SIZE;
        if (test_elf_read_u32(program) != LD_ELF_PT_LOAD ||
            test_elf_read_u32(program + 4U) !=
                    (LD_ELF_PF_R | LD_ELF_PF_W)) {
            continue;
        }
        rw_load_count++;
        uint64_t offset = test_elf_read_u64(program + 8U);
        uint64_t address = test_elf_read_u64(program + 16U);
        uint64_t file_size = test_elf_read_u64(program + 32U);
        uint64_t memory_size = test_elf_read_u64(program + 40U);
        uint64_t alignment = test_elf_read_u64(program + 48U);
        assert(offset == relro_offset && address == relro_address);
        assert(offset % alignment == address % alignment);
        assert(data_offset + test_elf_read_u64(data + 32U) <=
               offset + file_size);
        assert(test_elf_read_u64(bss + 16U) +
                       test_elf_read_u64(bss + 32U) <=
               address + memory_size);
    }
    assert(rw_load_count == 1U);

    free(image);
    unlink(object_path);
    if (!retained || !retained[0]) unlink(output_path);
}

static void test_output_without_relro_has_no_header(void) {
    size_t object_size;
    uint8_t *object = make_test_elf_relro_object(
            LD_ELF_EM_AARCH64, false, &object_size);
    char object_path[] = "/tmp/nature-ld-no-relro-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    static const char output_path[] = "/tmp/nature-ld-no-relro-output";
    diagnostic_capture_t capture = {0};
    assert(link_test_elf_relro_object(
                   LD_ARCH_ARM64, object_path, output_path, &capture) ==
           LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    size_t count;
    assert(find_test_elf_program(
                   image, image_size, LD_ELF_PT_GNU_RELRO, &count) == NULL);
    assert(count == 0U);
    assert(test_elf_read_u16(image + 56U) == 4U);
    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_relro(void) {
    test_relro_classification_and_plan();
    test_relro_plan_failures();
    test_relro_output_for_arch(LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
                               "aarch64");
    test_relro_output_for_arch(LD_ARCH_AMD64, LD_ELF_EM_X86_64,
                               "x86-64");
    test_relro_output_for_arch(LD_ARCH_RISCV64, LD_ELF_EM_RISCV,
                               "riscv64");
    test_output_without_relro_has_no_header();
}
