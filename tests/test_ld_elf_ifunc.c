#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld.h"
#include "src/ld/ld_elf_ifunc.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) |
           ((uint32_t) bytes[3] << 24U);
}

static uint64_t read_u64(const uint8_t *bytes) {
    return (uint64_t) read_u32(bytes) |
           ((uint64_t) read_u32(bytes + 4U) << 32U);
}

static void test_x86_64_pltgot(void) {
    uint8_t entry[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE];
    memset(entry, 0xa5, sizeof(entry));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_AMD64, entry, UINT64_C(0x400100),
                   UINT64_C(0x401000)) == LD_ELF_IFUNC_OK);
    static const uint8_t prefix[] = {
            0xf3U,
            0x0fU,
            0x1eU,
            0xfaU,
            0xffU,
            0x25U,
    };
    assert(memcmp(entry, prefix, sizeof(prefix)) == 0);
    assert(read_u32(entry + 6U) == UINT32_C(0x00000ef6));
    for (size_t i = 10U; i < sizeof(entry); i++) assert(entry[i] == 0xccU);

    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_AMD64, entry, UINT64_C(0x401000),
                   UINT64_C(0x400000)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry + 6U) == UINT32_C(0xffffeff6));

    memset(entry, 0x5a, sizeof(entry));
    uint8_t expected[sizeof(entry)];
    memcpy(expected, entry, sizeof(expected));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_AMD64, entry, 0U,
                   (uint64_t) INT32_MAX + 11U) == LD_ELF_IFUNC_RANGE);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_AMD64, entry, UINT64_MAX - 9U,
                   0U) == LD_ELF_IFUNC_RANGE);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
}

static void test_aarch64_pltgot(void) {
    uint8_t entry[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE];
    memset(entry, 0xa5, sizeof(entry));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, entry, UINT64_C(0x401000),
                   UINT64_C(0x402018)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0xb0000010));
    assert(read_u32(entry + 4U) == UINT32_C(0xf9400e11));
    assert(read_u32(entry + 8U) == UINT32_C(0xd61f0220));
    assert(read_u32(entry + 12U) == UINT32_C(0xd503201f));

    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, entry, UINT64_C(0x402000),
                   UINT64_C(0x401008)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0xf0fffff0));
    assert(read_u32(entry + 4U) == UINT32_C(0xf9400611));

    memset(entry, 0x5a, sizeof(entry));
    uint8_t expected[sizeof(entry)];
    memcpy(expected, entry, sizeof(expected));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, entry, UINT64_C(0x401002),
                   UINT64_C(0x402018)) == LD_ELF_IFUNC_UNALIGNED);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, entry, UINT64_C(0x401000),
                   UINT64_C(0x402014)) == LD_ELF_IFUNC_UNALIGNED);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, entry, 0U,
                   UINT64_C(0x100000000)) == LD_ELF_IFUNC_RANGE);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
}

static void test_riscv64_pltgot(void) {
    uint8_t entry[LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE];
    memset(entry, 0xa5, sizeof(entry));

    /* Exact sequence emitted by GNU ld 2.42 for a static RISC-V IFUNC. */
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x10270),
                   UINT64_C(0x82000)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0x00072e17));
    assert(read_u32(entry + 4U) == UINT32_C(0xd90e3e03));
    assert(read_u32(entry + 8U) == UINT32_C(0x000e0367));
    assert(read_u32(entry + 12U) == UINT32_C(0x00000013));

    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x401000),
                   UINT64_C(0x402018)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0x00001e17));
    assert(read_u32(entry + 4U) == UINT32_C(0x018e3e03));

    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x402000),
                   UINT64_C(0x401008)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0xfffffe17));
    assert(read_u32(entry + 4U) == UINT32_C(0x008e3e03));

    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, 0U,
                   UINT64_C(0x7ffff7f8)) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0x7ffffe17));
    assert(read_u32(entry + 4U) == UINT32_C(0x7f8e3e03));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x80000000),
                   0U) == LD_ELF_IFUNC_OK);
    assert(read_u32(entry) == UINT32_C(0x80000e17));
    assert(read_u32(entry + 4U) == UINT32_C(0x000e3e03));

    memset(entry, 0x5a, sizeof(entry));
    uint8_t expected[sizeof(entry)];
    memcpy(expected, entry, sizeof(expected));
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x401002),
                   UINT64_C(0x402018)) == LD_ELF_IFUNC_UNALIGNED);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x401000),
                   UINT64_C(0x402014)) == LD_ELF_IFUNC_UNALIGNED);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, 0U,
                   UINT64_C(0x7ffff800)) == LD_ELF_IFUNC_RANGE);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_RISCV64, entry, UINT64_C(0x80000004),
                   0U) == LD_ELF_IFUNC_RANGE);
    assert(memcmp(entry, expected, sizeof(entry)) == 0);
}

static void test_irelative(void) {
    const struct {
        ld_arch_t arch;
        uint32_t type;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_R_X86_64_IRELATIVE},
            {LD_ARCH_ARM64, LD_ELF_R_AARCH64_IRELATIVE},
            {LD_ARCH_RISCV64, LD_ELF_R_RISCV_IRELATIVE},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t rela[LD_ELF_IFUNC_RELA_SIZE];
        memset(rela, 0xa5, sizeof(rela));
        assert(ld_elf_ifunc_encode_irelative(
                       cases[i].arch, rela, UINT64_C(0x401020),
                       UINT64_C(0x400880)) == LD_ELF_IFUNC_OK);
        assert(read_u64(rela) == UINT64_C(0x401020));
        assert(read_u64(rela + 8U) == cases[i].type);
        assert(read_u64(rela + 16U) == UINT64_C(0x400880));

        assert(ld_elf_ifunc_encode_irelative(
                       cases[i].arch, rela, UINT64_C(0x401020),
                       -INT64_C(7)) == LD_ELF_IFUNC_OK);
        assert(read_u64(rela + 16U) == UINT64_MAX - 6U);
    }

    uint8_t unchanged[LD_ELF_IFUNC_RELA_SIZE];
    memset(unchanged, 0x5a, sizeof(unchanged));
    uint8_t expected[sizeof(unchanged)];
    memcpy(expected, unchanged, sizeof(expected));
    assert(ld_elf_ifunc_encode_irelative(
                   (ld_arch_t) 99, unchanged, 1U, 2U) ==
           LD_ELF_IFUNC_UNSUPPORTED_ARCH);
    assert(memcmp(unchanged, expected, sizeof(unchanged)) == 0);
    assert(ld_elf_ifunc_encode_irelative(
                   LD_ARCH_ARM64, NULL, 1U, 2U) ==
           LD_ELF_IFUNC_INVALID_ARGUMENT);
    assert(ld_elf_ifunc_encode_pltgot(
                   LD_ARCH_ARM64, NULL, 1U, 2U) ==
           LD_ELF_IFUNC_INVALID_ARGUMENT);
}

static uint8_t *make_ifunc_link_object(ld_arch_t arch,
                                       int64_t direct_addend,
                                       size_t *result_size) {
    const uint16_t machine = arch == LD_ARCH_AMD64
                                     ? LD_ELF_EM_X86_64
                                     : (arch == LD_ARCH_ARM64
                                                ? LD_ELF_EM_AARCH64
                                                : LD_ELF_EM_RISCV);
    const uint32_t call_type = arch == LD_ARCH_AMD64
                                       ? LD_ELF_R_X86_64_PLT32
                                       : (arch == LD_ARCH_ARM64
                                                  ? LD_ELF_R_AARCH64_CALL26
                                                  : LD_ELF_R_RISCV_CALL_PLT);
    const uint32_t absolute_type = arch == LD_ARCH_AMD64
                                           ? LD_ELF_R_X86_64_64
                                           : (arch == LD_ARCH_ARM64
                                                      ? LD_ELF_R_AARCH64_ABS64
                                                      : LD_ELF_R_RISCV_64);
    enum {
        text_index = 1,
        data_index = 2,
        rela_text_index = 3,
        rela_data_index = 4,
        symtab_index = 5,
        strtab_index = 6,
        shstrtab_index = 7,
        section_count = 8,
        symbol_count = 6,
    };
    char symbol_names[96] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");
    const uint32_t ifunc_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "ifunc_target");
    const uint32_t rela_start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "__rela_iplt_start");
    const uint32_t rela_end_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "__rela_iplt_end");

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".data");
    const uint32_t rela_text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.text");
    const uint32_t rela_data_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.data");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t text_offset = LD_ELF64_EHDR_SIZE;
    const size_t text_size = 16U;
    const size_t data_offset = test_elf_align(text_offset + text_size, 8U);
    const size_t data_size = 24U;
    const size_t rela_text_offset =
            test_elf_align(data_offset + data_size, 8U);
    const size_t rela_data_offset =
            rela_text_offset + LD_ELF64_RELA_SIZE;
    const size_t symbols_offset = test_elf_align(
            rela_data_offset + 3U * LD_ELF64_RELA_SIZE, 8U);
    const size_t symbol_names_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_names_offset =
            symbol_names_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            section_names_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
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
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, shstrtab_index);

    if (arch == LD_ARCH_AMD64) {
        bytes[text_offset] = 0xe8U;
        bytes[text_offset + 5U] = 0xc3U;
        bytes[text_offset + 8U] = 0xc3U;
    } else if (arch == LD_ARCH_ARM64) {
        test_elf_write_u32(bytes + text_offset, UINT32_C(0x94000000));
        test_elf_write_u32(bytes + text_offset + 4U,
                           UINT32_C(0xd65f03c0));
        test_elf_write_u32(bytes + text_offset + 8U,
                           UINT32_C(0xd65f03c0));
    } else {
        test_elf_write_u32(bytes + text_offset, UINT32_C(0x00000097));
        test_elf_write_u32(bytes + text_offset + 4U,
                           UINT32_C(0x000080e7));
        test_elf_write_u32(bytes + text_offset + 8U,
                           UINT32_C(0x00008067));
    }

    uint8_t *rela_text = bytes + rela_text_offset;
    test_elf_write_u64(rela_text, arch == LD_ARCH_AMD64 ? 1U : 0U);
    test_elf_write_u64(rela_text + 8U,
                       LD_ELF_RELA_INFO(3U, call_type));
    test_elf_write_u64(rela_text + 16U,
                       arch == LD_ARCH_AMD64 ? UINT64_MAX - 3U : 0U);
    uint8_t *rela_data = bytes + rela_data_offset;
    const uint32_t data_symbols[] = {3U, 4U, 5U};
    for (size_t i = 0; i < 3U; i++) {
        uint8_t *rela = rela_data + i * LD_ELF64_RELA_SIZE;
        test_elf_write_u64(rela, i * 8U);
        test_elf_write_u64(rela + 8U,
                           LD_ELF_RELA_INFO(data_symbols[i],
                                            absolute_type));
        test_elf_write_u64(rela + 16U,
                           i == 0U ? (uint64_t) direct_addend : 0U);
    }

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *text_symbol = symbols + LD_ELF64_SYM_SIZE;
    text_symbol[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(text_symbol + 6U, text_index);
    uint8_t *start = text_symbol + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, text_index);
    test_elf_write_u64(start + 16U, 8U);
    uint8_t *ifunc = start + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(ifunc, ifunc_name);
    ifunc[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL,
                               LD_ELF_STT_GNU_IFUNC);
    test_elf_write_u16(ifunc + 6U, text_index);
    test_elf_write_u64(ifunc + 8U, 8U);
    test_elf_write_u64(ifunc + 16U, 4U);
    uint8_t *rela_start = ifunc + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(rela_start, rela_start_name);
    rela_start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    uint8_t *rela_end = rela_start + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(rela_end, rela_end_name);
    rela_end[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);

    memcpy(bytes + symbol_names_offset, symbol_names, symbol_names_size);
    memcpy(bytes + section_names_offset, section_names, section_names_size);
    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + text_index * LD_ELF64_SHDR_SIZE,
                           text_name, LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + data_index * LD_ELF64_SHDR_SIZE,
                           data_name, LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE,
                           data_offset, data_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + rela_text_index * LD_ELF64_SHDR_SIZE,
            rela_text_name, LD_ELF_SHT_RELA, 0U, rela_text_offset,
            LD_ELF64_RELA_SIZE, symtab_index, text_index, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + rela_data_index * LD_ELF64_SHDR_SIZE,
            rela_data_name, LD_ELF_SHT_RELA, 0U, rela_data_offset,
            3U * LD_ELF64_RELA_SIZE, symtab_index, data_index, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + strtab_index * LD_ELF64_SHDR_SIZE,
                           strtab_name, LD_ELF_SHT_STRTAB, 0U,
                           symbol_names_offset, symbol_names_size, 0U, 0U,
                           1U, 0U);
    test_elf_write_section(sections + shstrtab_index * LD_ELF64_SHDR_SIZE,
                           shstrtab_name, LD_ELF_SHT_STRTAB, 0U,
                           section_names_offset, section_names_size, 0U, 0U,
                           1U, 0U);
    *result_size = size;
    return bytes;
}

static const uint8_t *find_output_symbol(const uint8_t *image,
                                         size_t image_size,
                                         const char *name) {
    const uint8_t *symtab =
            test_elf_find_output_section(image, image_size, ".symtab");
    assert(symtab != NULL);
    uint64_t section_table_offset = test_elf_read_u64(image + 40U);
    uint32_t string_index = test_elf_read_u32(symtab + 40U);
    uint16_t section_count = test_elf_read_u16(image + 60U);
    assert(string_index < section_count);
    const uint8_t *strings_header =
            image + section_table_offset +
            (size_t) string_index * LD_ELF64_SHDR_SIZE;
    uint64_t symbols_offset = test_elf_read_u64(symtab + 24U);
    uint64_t symbols_size = test_elf_read_u64(symtab + 32U);
    uint64_t strings_offset = test_elf_read_u64(strings_header + 24U);
    uint64_t strings_size = test_elf_read_u64(strings_header + 32U);
    assert(symbols_offset <= image_size &&
           symbols_size <= image_size - symbols_offset);
    assert(strings_offset <= image_size &&
           strings_size <= image_size - strings_offset);
    const char *strings = (const char *) image + strings_offset;
    for (uint64_t offset = 0U; offset < symbols_size;
         offset += LD_ELF64_SYM_SIZE) {
        const uint8_t *symbol = image + symbols_offset + offset;
        uint32_t name_offset = test_elf_read_u32(symbol);
        assert(name_offset < strings_size);
        assert(memchr(strings + name_offset, '\0',
                      (size_t) strings_size - name_offset) != NULL);
        if (strcmp(strings + name_offset, name) == 0) return symbol;
    }
    return NULL;
}

static void test_ifunc_link(ld_arch_t arch, bool pie) {
    size_t object_size;
    uint8_t *object = make_ifunc_link_object(arch, 0, &object_size);
    char object_path[] = "/tmp/nature-ld-ifunc-link-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    const char *output_path;
    if (arch == LD_ARCH_AMD64) {
        output_path = pie ? "/tmp/nature-ld-ifunc-pie-x86"
                          : "/tmp/nature-ld-ifunc-link-x86";
    } else if (arch == LD_ARCH_ARM64) {
        output_path = pie ? "/tmp/nature-ld-ifunc-pie-arm"
                          : "/tmp/nature-ld-ifunc-link-arm";
    } else {
        output_path = pie ? "/tmp/nature-ld-ifunc-pie-riscv"
                          : "/tmp/nature-ld-ifunc-link-riscv";
    }
    unlink(output_path);
    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = arch;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = pie;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(test_elf_read_u16(image + 16U) ==
           (pie ? LD_ELF_ET_DYN : LD_ELF_ET_EXEC));
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    const uint8_t *got =
            test_elf_find_output_section(image, image_size, ".got");
    const uint8_t *pltgot =
            test_elf_find_output_section(image, image_size, ".plt.got");
    const uint8_t *rela =
            test_elf_find_output_section(image, image_size, ".rela.dyn");
    assert(text && data && got && pltgot && rela);
    if (pie) {
        size_t match_count = 0U;
        assert(test_elf_find_program_header(
                       image, image_size, LD_ELF_PT_INTERP,
                       &match_count) == NULL);
        assert(match_count == 0U);
        const uint8_t *dynamic_program = test_elf_find_program_header(
                image, image_size, LD_ELF_PT_DYNAMIC, &match_count);
        assert(dynamic_program != NULL && match_count == 1U);
        const uint8_t *relro_program = test_elf_find_program_header(
                image, image_size, LD_ELF_PT_GNU_RELRO, &match_count);
        assert(relro_program != NULL && match_count == 1U);
        const uint8_t *dynamic = test_elf_find_output_section(
                image, image_size, ".dynamic");
        assert(dynamic != NULL);
        assert(test_elf_read_u64(dynamic_program + 8U) ==
               test_elf_read_u64(dynamic + 24U));
        assert(test_elf_read_u64(dynamic_program + 16U) ==
               test_elf_read_u64(dynamic + 16U));
        assert(test_elf_read_u64(dynamic_program + 32U) ==
               test_elf_read_u64(dynamic + 32U));
        uint64_t dynamic_address = test_elf_read_u64(dynamic + 16U);
        uint64_t dynamic_size = test_elf_read_u64(dynamic + 32U);
        uint64_t relro_address = test_elf_read_u64(relro_program + 16U);
        uint64_t relro_size = test_elf_read_u64(relro_program + 40U);
        assert(dynamic_address >= relro_address);
        assert(dynamic_address - relro_address <= relro_size);
        assert(dynamic_size <=
               relro_size - (dynamic_address - relro_address));

        uint64_t dynamic_offset = test_elf_read_u64(dynamic + 24U);
        bool terminated = false;
        for (uint64_t offset = 0U; offset < dynamic_size;
             offset += LD_ELF64_DYN_SIZE) {
            assert(offset <= dynamic_size &&
                   LD_ELF64_DYN_SIZE <= dynamic_size - offset);
            assert(dynamic_offset <= image_size &&
                   offset <= image_size - dynamic_offset &&
                   LD_ELF64_DYN_SIZE <=
                           image_size - dynamic_offset - offset);
            uint64_t tag = test_elf_read_u64(
                    image + dynamic_offset + offset);
            assert(tag != LD_ELF_DT_NEEDED);
            if (tag == LD_ELF_DT_NULL) {
                terminated = true;
                break;
            }
        }
        assert(terminated);
    }
    assert(test_elf_read_u64(got + 32U) == 8U);
    assert(test_elf_read_u64(pltgot + 32U) ==
           LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE);
    const size_t relative_count = pie ? 2U : 0U;
    const size_t relocation_count = relative_count + 2U;
    assert(test_elf_read_u64(rela + 32U) ==
           relocation_count * LD_ELF_IFUNC_RELA_SIZE);
    assert(test_elf_read_u64(rela + 56U) == LD_ELF_IFUNC_RELA_SIZE);

    const uint8_t *ifunc = find_output_symbol(image, image_size,
                                              "ifunc_target");
    const uint8_t *pltgot_symbol = find_output_symbol(
            image, image_size, "ifunc_target$pltgot");
    assert(ifunc && pltgot_symbol);
    assert((ifunc[4] & 0x0fU) == LD_ELF_STT_GNU_IFUNC);
    assert(pltgot_symbol[4] ==
           LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_FUNC));
    const uint64_t resolver_address = test_elf_read_u64(ifunc + 8U);
    const uint64_t pltgot_address = test_elf_read_u64(pltgot + 16U);
    const uint64_t got_address = test_elf_read_u64(got + 16U);
    const uint64_t data_address = test_elf_read_u64(data + 16U);
    const uint64_t rela_address = test_elf_read_u64(rela + 16U);
    assert(test_elf_read_u64(pltgot_symbol + 8U) == pltgot_address);
    assert(test_elf_read_u64(pltgot_symbol + 16U) ==
           LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE);

    const uint64_t text_offset = test_elf_read_u64(text + 24U);
    const uint64_t text_address = test_elf_read_u64(text + 16U);
    uint64_t call_target;
    if (arch == LD_ARCH_AMD64) {
        int32_t displacement =
                (int32_t) test_elf_read_u32(image + text_offset + 1U);
        call_target = (uint64_t) ((int64_t) text_address + 5 + displacement);
    } else if (arch == LD_ARCH_ARM64) {
        uint32_t instruction = test_elf_read_u32(image + text_offset);
        int32_t displacement =
                (int32_t) ((instruction & UINT32_C(0x03ffffff)) << 6U) >> 4U;
        call_target = (uint64_t) ((int64_t) text_address + displacement);
    } else {
        uint32_t auipc = test_elf_read_u32(image + text_offset);
        uint32_t jalr = test_elf_read_u32(image + text_offset + 4U);
        int64_t high = auipc & UINT32_C(0xfffff000);
        int64_t low = jalr >> 20U;
        if ((high & INT64_C(0x80000000)) != 0)
            high -= INT64_C(0x100000000);
        if ((low & INT64_C(0x800)) != 0) low -= INT64_C(0x1000);
        call_target = (uint64_t) ((int64_t) text_address + high + low);
    }
    assert(call_target == pltgot_address);

    const uint64_t data_offset = test_elf_read_u64(data + 24U);
    const uint64_t got_offset = test_elf_read_u64(got + 24U);
    const uint64_t rela_offset = test_elf_read_u64(rela + 24U);
    assert(test_elf_read_u64(image + data_offset) == resolver_address);
    const uint64_t iplt_start =
            pie ? 0U
                : rela_address + relative_count * LD_ELF_IFUNC_RELA_SIZE;
    const uint64_t iplt_end =
            pie ? 0U
                : rela_address + relocation_count * LD_ELF_IFUNC_RELA_SIZE;
    assert(test_elf_read_u64(image + data_offset + 8U) == iplt_start);
    assert(test_elf_read_u64(image + data_offset + 16U) ==
           iplt_end);
    assert(test_elf_read_u64(image + got_offset) == resolver_address);

    const uint32_t irelative_type =
            arch == LD_ARCH_AMD64
                    ? LD_ELF_R_X86_64_IRELATIVE
                    : (arch == LD_ARCH_ARM64
                               ? LD_ELF_R_AARCH64_IRELATIVE
                               : LD_ELF_R_RISCV_IRELATIVE);
    const uint32_t relative_type =
            arch == LD_ARCH_AMD64
                    ? LD_ELF_R_X86_64_RELATIVE
                    : (arch == LD_ARCH_ARM64
                               ? LD_ELF_R_AARCH64_RELATIVE
                               : LD_ELF_R_RISCV_RELATIVE);
    bool found_iplt_start = !pie, found_iplt_end = !pie;
    for (size_t i = 0U; i < relative_count; i++) {
        const uint8_t *entry =
                image + rela_offset + i * LD_ELF_IFUNC_RELA_SIZE;
        uint64_t target = test_elf_read_u64(entry);
        assert(test_elf_read_u64(entry + 8U) == relative_type);
        if (target == data_address + 8U) {
            assert(test_elf_read_u64(entry + 16U) == iplt_start);
            found_iplt_start = true;
        } else if (target == data_address + 16U) {
            assert(test_elf_read_u64(entry + 16U) == iplt_end);
            found_iplt_end = true;
        } else {
            assert(false);
        }
    }
    assert(found_iplt_start && found_iplt_end);

    bool found_got = false, found_data = false;
    for (size_t i = relative_count; i < relocation_count; i++) {
        const uint8_t *entry =
                image + rela_offset + i * LD_ELF_IFUNC_RELA_SIZE;
        uint64_t target = test_elf_read_u64(entry);
        assert(test_elf_read_u64(entry + 8U) == irelative_type);
        assert(test_elf_read_u64(entry + 16U) == resolver_address);
        found_got |= target == got_address;
        found_data |= target == data_address;
    }
    assert(found_got && found_data);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_ifunc_negative_signed_addend(ld_arch_t arch) {
    const int64_t input_addend = -INT64_C(0x100000);
    size_t object_size;
    uint8_t *object = make_ifunc_link_object(
            arch, input_addend, &object_size);
    char object_path[] = "/tmp/nature-ld-ifunc-negative-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    const char *suffix = arch == LD_ARCH_AMD64
                                 ? "x86"
                                 : (arch == LD_ARCH_ARM64 ? "arm"
                                                          : "riscv");
    char output_path[sizeof(object_path) + 16U];
    assert(snprintf(output_path, sizeof(output_path), "%s.%s",
                    object_path, suffix) > 0);
    unlink(output_path);

    diagnostic_capture_t capture = {0};
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = arch;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = true;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = &capture;
    assert(ld_add_input(&options, object_path) == LD_OK);
    assert(ld_link(&options) == LD_OK);
    assert(capture.count == 0U);
    ld_options_deinit(&options);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *data = test_elf_find_output_section(
            image, image_size, ".data");
    const uint8_t *rela = test_elf_find_output_section(
            image, image_size, ".rela.dyn");
    const uint8_t *ifunc = find_output_symbol(
            image, image_size, "ifunc_target");
    assert(data != NULL && rela != NULL && ifunc != NULL);

    uint64_t resolver_address = test_elf_read_u64(ifunc + 8U);
    assert(resolver_address < UINT64_C(0x100000));
    int64_t expected = (int64_t) resolver_address + input_addend;
    assert(expected < 0);
    uint64_t data_address = test_elf_read_u64(data + 16U);
    uint64_t data_offset = test_elf_read_u64(data + 24U);
    uint64_t rela_offset = test_elf_read_u64(rela + 24U);
    uint64_t rela_size = test_elf_read_u64(rela + 32U);
    assert(data_offset <= image_size && 8U <= image_size - data_offset);
    assert(rela_offset <= image_size && rela_size <= image_size - rela_offset);
    assert(test_elf_read_u64(image + data_offset) == (uint64_t) expected);

    const uint32_t irelative_type =
            arch == LD_ARCH_AMD64
                    ? LD_ELF_R_X86_64_IRELATIVE
                    : (arch == LD_ARCH_ARM64
                               ? LD_ELF_R_AARCH64_IRELATIVE
                               : LD_ELF_R_RISCV_IRELATIVE);
    bool found = false;
    for (uint64_t offset = 0U; offset < rela_size;
         offset += LD_ELF_IFUNC_RELA_SIZE) {
        assert(LD_ELF_IFUNC_RELA_SIZE <= rela_size - offset);
        const uint8_t *entry = image + rela_offset + offset;
        if (test_elf_read_u64(entry) != data_address) continue;
        assert(test_elf_read_u64(entry + 8U) == irelative_type);
        assert(test_elf_read_u64(entry + 16U) == (uint64_t) expected);
        found = true;
    }
    assert(found);

    free(image);
    unlink(object_path);
    unlink(output_path);
}

void test_ld_elf_ifunc(void) {
    test_x86_64_pltgot();
    test_aarch64_pltgot();
    test_riscv64_pltgot();
    test_irelative();
    test_ifunc_link(LD_ARCH_AMD64, false);
    test_ifunc_link(LD_ARCH_ARM64, false);
    test_ifunc_link(LD_ARCH_RISCV64, false);
    test_ifunc_link(LD_ARCH_AMD64, true);
    test_ifunc_link(LD_ARCH_ARM64, true);
    test_ifunc_link(LD_ARCH_RISCV64, true);
    test_ifunc_negative_signed_addend(LD_ARCH_AMD64);
    test_ifunc_negative_signed_addend(LD_ARCH_ARM64);
    test_ifunc_negative_signed_addend(LD_ARCH_RISCV64);
}
