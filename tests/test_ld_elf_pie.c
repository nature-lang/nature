#include "test_ld_elf_common.h"

#include "src/ld/elf_format.h"
#include "src/ld/ld_elf_reloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    TEST_PIE_TEXT = 1,
    TEST_PIE_DATA,
    TEST_PIE_RELA_DATA,
    TEST_PIE_SYMTAB,
    TEST_PIE_STRTAB,
    TEST_PIE_SHSTRTAB,
    TEST_PIE_SECTION_COUNT,
};

enum {
    TEST_PIE_NULL_SYMBOL = 0,
    TEST_PIE_DATA_SECTION_SYMBOL,
    TEST_PIE_START_SYMBOL,
    TEST_PIE_TARGET_SYMBOL,
    TEST_PIE_DYNAMIC_SYMBOL,
    TEST_PIE_EHDR_SYMBOL,
    TEST_PIE_RELA_IPLT_START_SYMBOL,
    TEST_PIE_RELA_IPLT_END_SYMBOL,
    TEST_PIE_SYMBOL_COUNT,
};

typedef struct {
    bool found;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} test_pie_output_symbol_t;

static void test_pie_expect_policy(ld_arch_t arch,
                                   const uint32_t *accepted,
                                   size_t accepted_count,
                                   const uint32_t *rejected,
                                   size_t rejected_count) {
    for (size_t i = 0U; i < accepted_count; i++) {
        assert(ld_elf_relocation_supported(arch, accepted[i]));
        assert(ld_elf_relocation_supported_in_static_pie(arch,
                                                         accepted[i]));
    }
    for (size_t i = 0U; i < rejected_count; i++) {
        assert(ld_elf_relocation_supported(arch, rejected[i]));
        assert(!ld_elf_relocation_supported_in_static_pie(arch,
                                                          rejected[i]));
    }
}

static void test_pie_zig_relocation_policy(void) {
    static const uint32_t x86_64_accepted[] = {
            LD_ELF_R_X86_64_NONE,
            LD_ELF_R_X86_64_64,
            LD_ELF_R_X86_64_PC32,
            LD_ELF_R_X86_64_GOT32,
            LD_ELF_R_X86_64_PLT32,
            LD_ELF_R_X86_64_GOTPCREL,
            LD_ELF_R_X86_64_32,
            LD_ELF_R_X86_64_32S,
            LD_ELF_R_X86_64_DTPOFF64,
            LD_ELF_R_X86_64_TPOFF64,
            LD_ELF_R_X86_64_TLSGD,
            LD_ELF_R_X86_64_TLSLD,
            LD_ELF_R_X86_64_DTPOFF32,
            LD_ELF_R_X86_64_GOTTPOFF,
            LD_ELF_R_X86_64_TPOFF32,
            LD_ELF_R_X86_64_GOTOFF64,
            LD_ELF_R_X86_64_GOTPC32,
            LD_ELF_R_X86_64_GOTPCREL64,
            LD_ELF_R_X86_64_GOTPC64,
            LD_ELF_R_X86_64_PLTOFF64,
            LD_ELF_R_X86_64_SIZE32,
            LD_ELF_R_X86_64_SIZE64,
            LD_ELF_R_X86_64_GOTPC32_TLSDESC,
            LD_ELF_R_X86_64_TLSDESC_CALL,
            LD_ELF_R_X86_64_GOTPCRELX,
            LD_ELF_R_X86_64_REX_GOTPCRELX,
    };
    static const uint32_t x86_64_rejected[] = {
            LD_ELF_R_X86_64_16,
            LD_ELF_R_X86_64_PC16,
            LD_ELF_R_X86_64_8,
            LD_ELF_R_X86_64_PC8,
            LD_ELF_R_X86_64_PC64,
            LD_ELF_R_X86_64_GOT64,
    };
    test_pie_expect_policy(
            LD_ARCH_AMD64, x86_64_accepted,
            sizeof(x86_64_accepted) / sizeof(x86_64_accepted[0]),
            x86_64_rejected,
            sizeof(x86_64_rejected) / sizeof(x86_64_rejected[0]));

    static const uint32_t aarch64_accepted[] = {
            LD_ELF_R_AARCH64_NONE,
            LD_ELF_R_AARCH64_ABS64,
            LD_ELF_R_AARCH64_PREL64,
            LD_ELF_R_AARCH64_PREL32,
            LD_ELF_R_AARCH64_ADR_PREL_LO21,
            LD_ELF_R_AARCH64_ADR_PREL_PG_HI21,
            LD_ELF_R_AARCH64_ADD_ABS_LO12_NC,
            LD_ELF_R_AARCH64_JUMP26,
            LD_ELF_R_AARCH64_CALL26,
            LD_ELF_R_AARCH64_LDST8_ABS_LO12_NC,
            LD_ELF_R_AARCH64_LDST16_ABS_LO12_NC,
            LD_ELF_R_AARCH64_LDST32_ABS_LO12_NC,
            LD_ELF_R_AARCH64_LDST64_ABS_LO12_NC,
            LD_ELF_R_AARCH64_LDST128_ABS_LO12_NC,
            LD_ELF_R_AARCH64_ADR_GOT_PAGE,
            LD_ELF_R_AARCH64_LD64_GOT_LO12_NC,
            LD_ELF_R_AARCH64_LD64_GOTPAGE_LO15,
            LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21,
            LD_ELF_R_AARCH64_TLSGD_ADD_LO12_NC,
            LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21,
            LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12,
            LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21,
            LD_ELF_R_AARCH64_TLSDESC_LD64_LO12,
            LD_ELF_R_AARCH64_TLSDESC_ADD_LO12,
            LD_ELF_R_AARCH64_TLSDESC_CALL,
    };
    static const uint32_t aarch64_rejected[] = {
            LD_ELF_R_AARCH64_ABS32,
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
            LD_ELF_R_AARCH64_CONDBR19,
            LD_ELF_R_AARCH64_GOT_LD_PREL19,
            LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC,
            LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12,
            LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC,
    };
    test_pie_expect_policy(
            LD_ARCH_ARM64, aarch64_accepted,
            sizeof(aarch64_accepted) / sizeof(aarch64_accepted[0]),
            aarch64_rejected,
            sizeof(aarch64_rejected) / sizeof(aarch64_rejected[0]));

    static const uint32_t riscv64_accepted[] = {
            LD_ELF_R_RISCV_NONE,
            LD_ELF_R_RISCV_32,
            LD_ELF_R_RISCV_64,
            LD_ELF_R_RISCV_CALL_PLT,
            LD_ELF_R_RISCV_GOT_HI20,
            LD_ELF_R_RISCV_PCREL_HI20,
            LD_ELF_R_RISCV_PCREL_LO12_I,
            LD_ELF_R_RISCV_PCREL_LO12_S,
            LD_ELF_R_RISCV_HI20,
            LD_ELF_R_RISCV_LO12_I,
            LD_ELF_R_RISCV_LO12_S,
            LD_ELF_R_RISCV_TPREL_HI20,
            LD_ELF_R_RISCV_TPREL_LO12_I,
            LD_ELF_R_RISCV_TPREL_LO12_S,
            LD_ELF_R_RISCV_TPREL_ADD,
            LD_ELF_R_RISCV_ADD32,
            LD_ELF_R_RISCV_SUB32,
            LD_ELF_R_RISCV_SET_ULEB128,
            LD_ELF_R_RISCV_SUB_ULEB128,
    };
    static const uint32_t riscv64_rejected[] = {
            LD_ELF_R_RISCV_RELATIVE,
            LD_ELF_R_RISCV_TLS_DTPREL32,
            LD_ELF_R_RISCV_TLS_DTPREL64,
            LD_ELF_R_RISCV_TLS_TPREL32,
            LD_ELF_R_RISCV_TLS_TPREL64,
            LD_ELF_R_RISCV_BRANCH,
            LD_ELF_R_RISCV_JAL,
            LD_ELF_R_RISCV_CALL,
            LD_ELF_R_RISCV_TLS_GOT_HI20,
            LD_ELF_R_RISCV_TLS_GD_HI20,
            LD_ELF_R_RISCV_ADD8,
            LD_ELF_R_RISCV_ADD16,
            LD_ELF_R_RISCV_ADD64,
            LD_ELF_R_RISCV_SUB8,
            LD_ELF_R_RISCV_SUB16,
            LD_ELF_R_RISCV_SUB64,
            LD_ELF_R_RISCV_ALIGN,
            LD_ELF_R_RISCV_RVC_BRANCH,
            LD_ELF_R_RISCV_RVC_JUMP,
            LD_ELF_R_RISCV_RELAX,
            LD_ELF_R_RISCV_SUB6,
            LD_ELF_R_RISCV_SET6,
            LD_ELF_R_RISCV_SET8,
            LD_ELF_R_RISCV_SET16,
            LD_ELF_R_RISCV_SET32,
            LD_ELF_R_RISCV_32_PCREL,
    };
    test_pie_expect_policy(
            LD_ARCH_RISCV64, riscv64_accepted,
            sizeof(riscv64_accepted) / sizeof(riscv64_accepted[0]),
            riscv64_rejected,
            sizeof(riscv64_rejected) / sizeof(riscv64_rejected[0]));

    assert(!ld_elf_relocation_supported_in_static_pie(
            (ld_arch_t) 0, LD_ELF_R_X86_64_NONE));
    assert(!ld_elf_relocation_supported_in_static_pie(
            LD_ARCH_AMD64, UINT32_MAX));
}

static uint8_t *test_pie_make_relocation_object(
        uint16_t machine, uint32_t relocation_type, bool target_absolute,
        uint32_t relocation_word, size_t *result_size) {
    assert(result_size != NULL);

    char symbol_names[128] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");
    const uint32_t target_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "target");
    const uint32_t dynamic_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_DYNAMIC");
    const uint32_t ehdr_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "__ehdr_start");
    const uint32_t rela_iplt_start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "__rela_iplt_start");
    const uint32_t rela_iplt_end_name = test_elf_append_name(
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
    const size_t text_size = 8U;
    const size_t data_offset = test_elf_align(text_offset + text_size, 8U);
    const size_t data_size = 32U;
    const size_t rela_offset = test_elf_align(data_offset + data_size, 8U);
    const size_t symtab_offset =
            test_elf_align(rela_offset + LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + TEST_PIE_SYMBOL_COUNT * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        TEST_PIE_SECTION_COUNT * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
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
    test_elf_write_u16(bytes + 60U, TEST_PIE_SECTION_COUNT);
    test_elf_write_u16(bytes + 62U, TEST_PIE_SHSTRTAB);

    bytes[text_offset] = 0xc3U; /* ret */
    test_elf_write_u32(bytes + data_offset, relocation_word);
    test_elf_write_u64(bytes + data_offset + 16U,
                       UINT64_C(0x1122334455667788));
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *relocation = bytes + rela_offset;
    test_elf_write_u64(relocation, 0U);
    test_elf_write_u64(
            relocation + 8U,
            LD_ELF_RELA_INFO(TEST_PIE_TARGET_SYMBOL, relocation_type));
    test_elf_write_u64(relocation + 16U, 7U);

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *data_section =
            symbols + TEST_PIE_DATA_SECTION_SYMBOL * LD_ELF64_SYM_SIZE;
    data_section[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(data_section + 6U, TEST_PIE_DATA);

    uint8_t *start =
            symbols + TEST_PIE_START_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, TEST_PIE_TEXT);
    test_elf_write_u64(start + 16U, 1U);

    uint8_t *target =
            symbols + TEST_PIE_TARGET_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(target, target_name);
    target[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_OBJECT);
    test_elf_write_u16(target + 6U,
                       target_absolute ? LD_ELF_SHN_ABS : TEST_PIE_DATA);
    test_elf_write_u64(target + 8U, target_absolute ? 0x1234U : 16U);
    test_elf_write_u64(target + 16U, 8U);

    uint8_t *dynamic =
            symbols + TEST_PIE_DYNAMIC_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(dynamic, dynamic_name);
    dynamic[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(dynamic + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *ehdr = symbols + TEST_PIE_EHDR_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(ehdr, ehdr_name);
    ehdr[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(ehdr + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *rela_iplt_start =
            symbols + TEST_PIE_RELA_IPLT_START_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(rela_iplt_start, rela_iplt_start_name);
    rela_iplt_start[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(rela_iplt_start + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *rela_iplt_end =
            symbols + TEST_PIE_RELA_IPLT_END_SYMBOL * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(rela_iplt_end, rela_iplt_end_name);
    rela_iplt_end[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(rela_iplt_end + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + TEST_PIE_TEXT * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            text_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_PIE_DATA * LD_ELF64_SHDR_SIZE, data_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, data_offset, data_size,
            0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + TEST_PIE_RELA_DATA * LD_ELF64_SHDR_SIZE,
            rela_data_name, LD_ELF_SHT_RELA, 0U, rela_offset,
            LD_ELF64_RELA_SIZE, TEST_PIE_SYMTAB, TEST_PIE_DATA, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + TEST_PIE_SYMTAB * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            TEST_PIE_SYMBOL_COUNT * LD_ELF64_SYM_SIZE, TEST_PIE_STRTAB,
            TEST_PIE_START_SYMBOL, 8U, LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + TEST_PIE_STRTAB * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, symbol_names_size, 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + TEST_PIE_SHSTRTAB * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static void test_pie_mutate_target_and_addend(
        uint8_t *object, size_t object_size, uint8_t binding,
        uint16_t section_index, uint64_t value, int64_t addend) {
    assert(object != NULL && object_size >= LD_ELF64_EHDR_SIZE);
    uint64_t section_table_offset = test_elf_read_u64(object + 40U);
    uint16_t section_count = test_elf_read_u16(object + 60U);
    assert(section_count == TEST_PIE_SECTION_COUNT);
    assert(section_table_offset <= object_size &&
           (uint64_t) section_count * LD_ELF64_SHDR_SIZE <=
                   object_size - section_table_offset);

    uint8_t *sections = object + (size_t) section_table_offset;
    uint8_t *symtab = sections + TEST_PIE_SYMTAB * LD_ELF64_SHDR_SIZE;
    uint64_t symtab_offset = test_elf_read_u64(symtab + 24U);
    uint64_t symtab_size = test_elf_read_u64(symtab + 32U);
    assert(symtab_offset <= object_size &&
           symtab_size <= object_size - symtab_offset);
    assert((uint64_t) (TEST_PIE_TARGET_SYMBOL + 1U) * LD_ELF64_SYM_SIZE <=
           symtab_size);
    uint8_t *target = object + (size_t) symtab_offset +
                      TEST_PIE_TARGET_SYMBOL * LD_ELF64_SYM_SIZE;
    target[4] = LD_ELF_SYM_INFO(binding, LD_ELF_STT_OBJECT);
    test_elf_write_u16(target + 6U, section_index);
    test_elf_write_u64(target + 8U, value);

    uint8_t *rela = sections + TEST_PIE_RELA_DATA * LD_ELF64_SHDR_SIZE;
    uint64_t rela_offset = test_elf_read_u64(rela + 24U);
    uint64_t rela_size = test_elf_read_u64(rela + 32U);
    assert(rela_offset <= object_size &&
           rela_size <= object_size - rela_offset &&
           rela_size >= LD_ELF64_RELA_SIZE);
    test_elf_write_u64(object + (size_t) rela_offset + 16U,
                       (uint64_t) addend);
}

static uint8_t *test_pie_make_x86_64_gottpoff_object(size_t *result_size) {
    enum {
        text_index = 1,
        tdata_index,
        rela_text_index,
        symtab_index,
        strtab_index,
        shstrtab_index,
        section_count,
    };
    enum {
        null_symbol = 0,
        text_section_symbol,
        start_symbol,
        tls_symbol,
        symbol_count,
    };
    assert(result_size != NULL);

    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "_start");
    const uint32_t tls_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "tls_value");

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t tdata_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".tdata");
    const uint32_t rela_text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.text");
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
    const size_t text_size = 8U;
    const size_t tdata_offset = test_elf_align(text_offset + text_size, 8U);
    const size_t tdata_size = 8U;
    const size_t rela_offset = test_elf_align(tdata_offset + tdata_size, 8U);
    const size_t symtab_offset =
            test_elf_align(rela_offset + LD_ELF64_RELA_SIZE, 8U);
    const size_t strtab_offset =
            symtab_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t shstrtab_offset = strtab_offset + symbol_names_size;
    const size_t section_table_offset = test_elf_align(
            shstrtab_offset + section_names_size, 8U);
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1U, size);
    assert(bytes != NULL);

    bytes[0] = LD_ELF_MAGIC_0;
    bytes[1] = LD_ELF_MAGIC_1;
    bytes[2] = LD_ELF_MAGIC_2;
    bytes[3] = LD_ELF_MAGIC_3;
    bytes[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    bytes[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    bytes[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    test_elf_write_u16(bytes + 16U, LD_ELF_ET_REL);
    test_elf_write_u16(bytes + 18U, LD_ELF_EM_X86_64);
    test_elf_write_u32(bytes + 20U, LD_ELF_VERSION_CURRENT);
    test_elf_write_u64(bytes + 40U, section_table_offset);
    test_elf_write_u16(bytes + 52U, LD_ELF64_EHDR_SIZE);
    test_elf_write_u16(bytes + 58U, LD_ELF64_SHDR_SIZE);
    test_elf_write_u16(bytes + 60U, section_count);
    test_elf_write_u16(bytes + 62U, shstrtab_index);

    static const uint8_t add_gottpoff[] = {
            0x48U,
            0x03U,
            0x05U,
            0x00U,
            0x00U,
            0x00U,
            0x00U,
            0xc3U,
    };
    memcpy(bytes + text_offset, add_gottpoff, sizeof(add_gottpoff));
    test_elf_write_u64(bytes + tdata_offset,
                       UINT64_C(0x8877665544332211));
    memcpy(bytes + strtab_offset, symbol_names, symbol_names_size);
    memcpy(bytes + shstrtab_offset, section_names, section_names_size);

    uint8_t *relocation = bytes + rela_offset;
    test_elf_write_u64(relocation, 3U);
    test_elf_write_u64(
            relocation + 8U,
            LD_ELF_RELA_INFO(tls_symbol, LD_ELF_R_X86_64_GOTTPOFF));
    test_elf_write_u64(relocation + 16U, UINT64_MAX - 3U); /* -4 */

    uint8_t *symbols = bytes + symtab_offset;
    uint8_t *text_section =
            symbols + text_section_symbol * LD_ELF64_SYM_SIZE;
    text_section[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(text_section + 6U, text_index);

    uint8_t *start = symbols + start_symbol * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, text_index);
    test_elf_write_u64(start + 16U, text_size);

    uint8_t *tls = symbols + tls_symbol * LD_ELF64_SYM_SIZE;
    test_elf_write_u32(tls, tls_name);
    tls[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_TLS);
    test_elf_write_u16(tls + 6U, tdata_index);
    test_elf_write_u64(tls + 16U, tdata_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset,
            text_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + tdata_index * LD_ELF64_SHDR_SIZE, tdata_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE | LD_ELF_SHF_TLS,
            tdata_offset, tdata_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + rela_text_index * LD_ELF64_SHDR_SIZE,
            rela_text_name, LD_ELF_SHT_RELA, 0U, rela_offset,
            LD_ELF64_RELA_SIZE, symtab_index, text_index, 8U,
            LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + symtab_index * LD_ELF64_SHDR_SIZE, symtab_name,
            LD_ELF_SHT_SYMTAB, 0U, symtab_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, start_symbol,
            8U, LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + strtab_index * LD_ELF64_SHDR_SIZE, strtab_name,
            LD_ELF_SHT_STRTAB, 0U, strtab_offset, symbol_names_size, 0U,
            0U, 1U, 0U);
    test_elf_write_section(
            sections + shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, shstrtab_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static int test_pie_link_arch(const char *input_path,
                              const char *output_path, bool pie,
                              ld_arch_t arch,
                              diagnostic_capture_t *capture) {
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = arch;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = pie;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    assert(ld_add_input(&options, input_path) == LD_OK);
    int status = ld_link(&options);
    ld_options_deinit(&options);
    return status;
}

static int test_pie_link(const char *input_path, const char *output_path,
                         bool pie, diagnostic_capture_t *capture) {
    return test_pie_link_arch(input_path, output_path, pie, LD_ARCH_AMD64,
                              capture);
}

static const uint8_t *test_pie_find_program_header(const uint8_t *image,
                                                   size_t image_size,
                                                   uint32_t type,
                                                   size_t *match_count) {
    assert(image != NULL && image_size >= LD_ELF64_EHDR_SIZE);
    uint64_t table_offset = test_elf_read_u64(image + 32U);
    uint16_t entry_size = test_elf_read_u16(image + 54U);
    uint16_t entry_count = test_elf_read_u16(image + 56U);
    assert(entry_size == LD_ELF64_PHDR_SIZE && table_offset <= image_size);
    assert((uint64_t) entry_count * entry_size <= image_size - table_offset);
    const uint8_t *result = NULL;
    size_t count = 0U;
    for (uint16_t i = 0U; i < entry_count; i++) {
        const uint8_t *program = image + (size_t) table_offset +
                                 (size_t) i * entry_size;
        if (test_elf_read_u32(program) != type) continue;
        if (!result) result = program;
        count++;
    }
    if (match_count) *match_count = count;
    return result;
}

static uint16_t test_pie_section_index(const uint8_t *image,
                                       size_t image_size,
                                       const uint8_t *section) {
    assert(image != NULL && section != NULL &&
           image_size >= LD_ELF64_EHDR_SIZE);
    uint64_t table_offset = test_elf_read_u64(image + 40U);
    uint16_t entry_size = test_elf_read_u16(image + 58U);
    uint16_t entry_count = test_elf_read_u16(image + 60U);
    assert(entry_size == LD_ELF64_SHDR_SIZE && table_offset <= image_size);
    assert((uint64_t) entry_count * entry_size <= image_size - table_offset);
    const uint8_t *table = image + (size_t) table_offset;
    assert(section >= table &&
           section < table + (size_t) entry_count * entry_size);
    size_t offset = (size_t) (section - table);
    assert(offset % entry_size == 0U);
    return (uint16_t) (offset / entry_size);
}

static const uint8_t *test_pie_section_contents(const uint8_t *image,
                                                size_t image_size,
                                                const uint8_t *section) {
    assert(image != NULL && section != NULL);
    uint64_t offset = test_elf_read_u64(section + 24U);
    uint64_t size = test_elf_read_u64(section + 32U);
    assert(offset <= image_size && size <= image_size - offset);
    return image + (size_t) offset;
}

static test_pie_output_symbol_t test_pie_find_output_symbol(
        const uint8_t *image, size_t image_size, const char *name) {
    test_pie_output_symbol_t result = {0};
    const uint8_t *symtab =
            test_elf_find_output_section(image, image_size, ".symtab");
    const uint8_t *strtab =
            test_elf_find_output_section(image, image_size, ".strtab");
    assert(symtab != NULL && strtab != NULL);

    uint64_t symbols_offset = test_elf_read_u64(symtab + 24U);
    uint64_t symbols_size = test_elf_read_u64(symtab + 32U);
    uint64_t symbol_size = test_elf_read_u64(symtab + 56U);
    uint64_t strings_offset = test_elf_read_u64(strtab + 24U);
    uint64_t strings_size = test_elf_read_u64(strtab + 32U);
    assert(symbol_size == LD_ELF64_SYM_SIZE &&
           symbols_offset <= image_size &&
           symbols_size <= image_size - symbols_offset &&
           symbols_size % symbol_size == 0U &&
           strings_offset <= image_size &&
           strings_size <= image_size - strings_offset);

    const char *strings = (const char *) image + (size_t) strings_offset;
    for (uint64_t offset = 0U; offset < symbols_size;
         offset += symbol_size) {
        const uint8_t *symbol =
                image + (size_t) symbols_offset + (size_t) offset;
        uint32_t name_offset = test_elf_read_u32(symbol);
        assert(name_offset < strings_size);
        assert(memchr(strings + name_offset, '\0',
                      (size_t) strings_size - name_offset) != NULL);
        if (strcmp(strings + name_offset, name) != 0) continue;
        result.found = true;
        result.info = symbol[4];
        result.other = symbol[5];
        result.section_index = test_elf_read_u16(symbol + 6U);
        result.value = test_elf_read_u64(symbol + 8U);
        result.size = test_elf_read_u64(symbol + 16U);
        break;
    }
    return result;
}

static void test_pie_expect_section_header_links(
        const uint8_t *image, size_t image_size, const uint8_t *dynstr,
        const uint8_t *dynsym, const uint8_t *hash,
        const uint8_t *gnu_hash, const uint8_t *rela,
        const uint8_t *dynamic) {
    const uint16_t dynstr_index =
            test_pie_section_index(image, image_size, dynstr);
    const uint16_t dynsym_index =
            test_pie_section_index(image, image_size, dynsym);

    assert(test_elf_read_u32(dynstr + 4U) == LD_ELF_SHT_STRTAB);
    assert(test_elf_read_u32(dynstr + 40U) == 0U);
    assert(test_elf_read_u32(dynstr + 44U) == 0U);

    assert(test_elf_read_u32(dynsym + 4U) == LD_ELF_SHT_DYNSYM);
    assert(test_elf_read_u32(dynsym + 40U) == dynstr_index);
    assert(test_elf_read_u32(dynsym + 44U) == 1U);
    assert(test_elf_read_u64(dynsym + 56U) == LD_ELF64_SYM_SIZE);

    assert(test_elf_read_u32(hash + 4U) == LD_ELF_SHT_HASH);
    assert(test_elf_read_u32(hash + 40U) == dynsym_index);
    assert(test_elf_read_u32(hash + 44U) == 0U);
    assert(test_elf_read_u64(hash + 56U) == 4U);

    assert(test_elf_read_u32(gnu_hash + 4U) == LD_ELF_SHT_GNU_HASH);
    assert(test_elf_read_u32(gnu_hash + 40U) == dynsym_index);
    assert(test_elf_read_u32(gnu_hash + 44U) == 0U);

    assert(test_elf_read_u32(rela + 4U) == LD_ELF_SHT_RELA);
    assert(test_elf_read_u32(rela + 40U) == dynsym_index);
    assert(test_elf_read_u32(rela + 44U) == 0U);
    assert(test_elf_read_u64(rela + 56U) == LD_ELF64_RELA_SIZE);

    assert(test_elf_read_u32(dynamic + 4U) == LD_ELF_SHT_DYNAMIC);
    assert(test_elf_read_u32(dynamic + 40U) == dynstr_index);
    assert(test_elf_read_u32(dynamic + 44U) == 0U);
    assert(test_elf_read_u64(dynamic + 56U) == LD_ELF64_DYN_SIZE);
}

static void test_pie_expect_dynamic_tags(const uint8_t *image,
                                         size_t image_size,
                                         const uint8_t *dynamic) {
    const uint8_t *contents =
            test_pie_section_contents(image, image_size, dynamic);
    uint64_t size = test_elf_read_u64(dynamic + 32U);
    assert(size >= LD_ELF64_DYN_SIZE && size % LD_ELF64_DYN_SIZE == 0U);
    bool terminated = false;
    bool saw_flags_1 = false;
    for (uint64_t offset = 0U; offset < size;
         offset += LD_ELF64_DYN_SIZE) {
        uint64_t tag = test_elf_read_u64(contents + (size_t) offset);
        uint64_t value =
                test_elf_read_u64(contents + (size_t) offset + 8U);
        assert(tag != 1U); /* DT_NEEDED */
        assert(tag != LD_ELF_DT_FLAGS); /* no static TLS in this fixture */
        if (tag == LD_ELF_DT_FLAGS_1) {
            assert(value == LD_ELF_DF_1_PIE);
            saw_flags_1 = true;
        }
        if (tag != LD_ELF_DT_NULL) continue;
        assert(offset + LD_ELF64_DYN_SIZE == size);
        terminated = true;
        break;
    }
    assert(terminated && saw_flags_1);
}

static bool test_pie_find_dynamic_tag(const uint8_t *image,
                                      size_t image_size,
                                      const uint8_t *dynamic, uint64_t tag,
                                      uint64_t *value) {
    const uint8_t *contents =
            test_pie_section_contents(image, image_size, dynamic);
    uint64_t size = test_elf_read_u64(dynamic + 32U);
    assert(size % LD_ELF64_DYN_SIZE == 0U);
    for (uint64_t offset = 0U; offset < size;
         offset += LD_ELF64_DYN_SIZE) {
        uint64_t current = test_elf_read_u64(contents + (size_t) offset);
        if (current == tag) {
            if (value) {
                *value = test_elf_read_u64(contents + (size_t) offset + 8U);
            }
            return true;
        }
        if (current == LD_ELF_DT_NULL) break;
    }
    return false;
}

static void test_pie_mode_and_backend_metadata(void) {
    size_t object_size;
    uint8_t *object = test_pie_make_relocation_object(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_64, false, 0U,
            &object_size);
    char object_path[] = "/tmp/nature-ld-pie-object-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    char exec_output[sizeof(object_path) + 8U];
    char pie_output[sizeof(object_path) + 8U];
    assert(snprintf(exec_output, sizeof(exec_output), "%s.exec",
                    object_path) > 0);
    assert(snprintf(pie_output, sizeof(pie_output), "%s.pie",
                    object_path) > 0);
    unlink(exec_output);
    unlink(pie_output);

    diagnostic_capture_t capture = {0};
    assert(test_pie_link(object_path, exec_output, false, &capture) ==
           LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(exec_output, &image_size, NULL);
    assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_EXEC);
    assert(test_elf_read_u16(image + 18U) == LD_ELF_EM_X86_64);
    size_t match_count;
    const uint8_t *load = test_pie_find_program_header(
            image, image_size, LD_ELF_PT_LOAD, &match_count);
    assert(load != NULL && match_count == 2U);
    assert(test_elf_read_u64(load + 8U) == 0U);
    assert(test_elf_read_u64(load + 16U) == UINT64_C(0x400000));
    assert(test_pie_find_program_header(
                   image, image_size, LD_ELF_PT_DYNAMIC, &match_count) ==
           NULL);
    assert(match_count == 0U);
    assert(test_pie_find_program_header(
                   image, image_size, LD_ELF_PT_INTERP, &match_count) ==
           NULL);
    assert(match_count == 0U);
    assert(test_elf_find_output_section(image, image_size, ".dynamic") ==
           NULL);
    assert(test_elf_find_output_section(image, image_size, ".rela.dyn") ==
           NULL);

    const uint8_t *exec_data =
            test_elf_find_output_section(image, image_size, ".data");
    assert(exec_data != NULL);
    test_pie_output_symbol_t exec_target =
            test_pie_find_output_symbol(image, image_size, "target");
    test_pie_output_symbol_t exec_start =
            test_pie_find_output_symbol(image, image_size, "_start");
    test_pie_output_symbol_t exec_ehdr =
            test_pie_find_output_symbol(image, image_size, "__ehdr_start");
    test_pie_output_symbol_t exec_dynamic =
            test_pie_find_output_symbol(image, image_size, "_DYNAMIC");
    assert(exec_target.found && exec_start.found && exec_ehdr.found &&
           exec_dynamic.found);
    assert(exec_target.value == test_elf_read_u64(exec_data + 16U) + 16U);
    assert(test_elf_read_u64(test_pie_section_contents(
                   image, image_size, exec_data)) ==
           exec_target.value + 7U);
    assert(test_elf_read_u64(image + 24U) == exec_start.value);
    assert(exec_ehdr.section_index == 1U &&
           exec_ehdr.value == UINT64_C(0x400000));
    assert(exec_dynamic.section_index == LD_ELF_SHN_UNDEF &&
           exec_dynamic.value == 0U);
    assert((exec_ehdr.info >> 4U) == LD_ELF_STB_LOCAL);
    assert((exec_dynamic.info >> 4U) == LD_ELF_STB_LOCAL);
    free(image);

    capture = (diagnostic_capture_t) {0};
    assert(test_pie_link(object_path, pie_output, true, &capture) == LD_OK);
    assert(capture.count == 0U);
    image = read_test_fixture(pie_output, &image_size, NULL);
    assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_DYN);
    assert(test_elf_read_u16(image + 18U) == LD_ELF_EM_X86_64);

    load = test_pie_find_program_header(
            image, image_size, LD_ELF_PT_LOAD, &match_count);
    assert(load != NULL && match_count == 2U);
    assert(test_elf_read_u64(load + 8U) == 0U);
    assert(test_elf_read_u64(load + 16U) == 0U);
    assert(test_elf_read_u64(load + 24U) == 0U);
    assert(test_pie_find_program_header(
                   image, image_size, LD_ELF_PT_INTERP, &match_count) ==
           NULL);
    assert(match_count == 0U);

    const uint8_t *dynamic_program = test_pie_find_program_header(
            image, image_size, LD_ELF_PT_DYNAMIC, &match_count);
    assert(dynamic_program != NULL && match_count == 1U);

    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *data =
            test_elf_find_output_section(image, image_size, ".data");
    const uint8_t *dynstr =
            test_elf_find_output_section(image, image_size, ".dynstr");
    const uint8_t *dynsym =
            test_elf_find_output_section(image, image_size, ".dynsym");
    const uint8_t *hash =
            test_elf_find_output_section(image, image_size, ".hash");
    const uint8_t *gnu_hash =
            test_elf_find_output_section(image, image_size, ".gnu.hash");
    const uint8_t *rela =
            test_elf_find_output_section(image, image_size, ".rela.dyn");
    const uint8_t *dynamic =
            test_elf_find_output_section(image, image_size, ".dynamic");
    assert(text && data && dynstr && dynsym && hash && gnu_hash && rela &&
           dynamic);
    test_pie_expect_section_header_links(image, image_size, dynstr, dynsym,
                                         hash, gnu_hash, rela, dynamic);

    assert(test_elf_read_u64(dynamic_program + 8U) ==
           test_elf_read_u64(dynamic + 24U));
    assert(test_elf_read_u64(dynamic_program + 16U) ==
           test_elf_read_u64(dynamic + 16U));
    assert(test_elf_read_u64(dynamic_program + 24U) ==
           test_elf_read_u64(dynamic + 16U));
    assert(test_elf_read_u64(dynamic_program + 32U) ==
           test_elf_read_u64(dynamic + 32U));
    assert(test_elf_read_u64(dynamic_program + 40U) ==
           test_elf_read_u64(dynamic + 32U));
    test_pie_expect_dynamic_tags(image, image_size, dynamic);

    assert(test_elf_read_u64(dynsym + 32U) == LD_ELF64_SYM_SIZE);
    const uint8_t *dynsym_contents =
            test_pie_section_contents(image, image_size, dynsym);
    for (size_t i = 0U; i < LD_ELF64_SYM_SIZE; i++)
        assert(dynsym_contents[i] == 0U);
    assert(test_elf_read_u64(dynstr + 32U) == 1U);
    assert(*test_pie_section_contents(image, image_size, dynstr) == 0U);

    test_pie_output_symbol_t target =
            test_pie_find_output_symbol(image, image_size, "target");
    test_pie_output_symbol_t start =
            test_pie_find_output_symbol(image, image_size, "_start");
    test_pie_output_symbol_t ehdr =
            test_pie_find_output_symbol(image, image_size, "__ehdr_start");
    test_pie_output_symbol_t dynamic_symbol =
            test_pie_find_output_symbol(image, image_size, "_DYNAMIC");
    test_pie_output_symbol_t rela_iplt_start = test_pie_find_output_symbol(
            image, image_size, "__rela_iplt_start");
    test_pie_output_symbol_t rela_iplt_end = test_pie_find_output_symbol(
            image, image_size, "__rela_iplt_end");
    assert(target.found && start.found && ehdr.found &&
           dynamic_symbol.found && rela_iplt_start.found &&
           rela_iplt_end.found);
    assert(target.value == test_elf_read_u64(data + 16U) + 16U);
    assert(start.value == test_elf_read_u64(text + 16U));
    assert(test_elf_read_u64(image + 24U) == start.value);
    assert(ehdr.section_index == 1U && ehdr.value == 0U);
    assert((ehdr.info >> 4U) == LD_ELF_STB_LOCAL);
    assert(dynamic_symbol.section_index ==
           test_pie_section_index(image, image_size, dynamic));
    assert(dynamic_symbol.value == test_elf_read_u64(dynamic + 16U));
    assert((dynamic_symbol.info >> 4U) == LD_ELF_STB_LOCAL);
    assert((dynamic_symbol.other & 3U) == LD_ELF_STV_HIDDEN);
    assert(rela_iplt_start.section_index == LD_ELF_SHN_UNDEF &&
           rela_iplt_start.value == 0U);
    assert(rela_iplt_end.section_index == LD_ELF_SHN_UNDEF &&
           rela_iplt_end.value == 0U);
    assert((rela_iplt_start.info >> 4U) == LD_ELF_STB_LOCAL);
    assert((rela_iplt_end.info >> 4U) == LD_ELF_STB_LOCAL);

    assert(test_elf_read_u64(rela + 32U) == LD_ELF64_RELA_SIZE);
    const uint8_t *rela_contents =
            test_pie_section_contents(image, image_size, rela);
    const uint64_t expected_target = target.value + 7U;
    assert(test_elf_read_u64(rela_contents) ==
           test_elf_read_u64(data + 16U));
    assert(test_elf_read_u64(rela_contents + 8U) ==
           LD_ELF_R_X86_64_RELATIVE);
    assert(test_elf_read_u64(rela_contents + 16U) == expected_target);
    assert(test_elf_read_u64(
                   test_pie_section_contents(image, image_size, data)) ==
           expected_target);

    free(image);
    unlink(object_path);
    unlink(exec_output);
    unlink(pie_output);
}

static void test_pie_arch_relative_final_outputs(void) {
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t input_type;
        uint32_t relative_type;
        const char *suffix;
    } cases[] = {
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_ABS64,
             LD_ELF_R_AARCH64_RELATIVE, "arm64"},
            {LD_ARCH_RISCV64, LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64,
             LD_ELF_R_RISCV_RELATIVE, "riscv64"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = test_pie_make_relocation_object(
                cases[i].machine, cases[i].input_type, false, 0U,
                &object_size);
        char object_path[] = "/tmp/nature-ld-pie-arch-object-XXXXXX";
        write_fixture(object_path, object, object_size);
        free(object);

        char output_path[sizeof(object_path) + 16U];
        assert(snprintf(output_path, sizeof(output_path), "%s.%s",
                        object_path, cases[i].suffix) > 0);
        unlink(output_path);
        diagnostic_capture_t capture = {0};
        assert(test_pie_link_arch(object_path, output_path, true,
                                  cases[i].arch, &capture) == LD_OK);
        assert(capture.count == 0U);

        size_t image_size;
        uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
        assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_DYN);
        assert(test_elf_read_u16(image + 18U) == cases[i].machine);
        size_t match_count = 0U;
        assert(test_pie_find_program_header(
                       image, image_size, LD_ELF_PT_INTERP,
                       &match_count) == NULL);
        assert(match_count == 0U);
        assert(test_pie_find_program_header(
                       image, image_size, LD_ELF_PT_DYNAMIC,
                       &match_count) != NULL &&
               match_count == 1U);

        const uint8_t *data =
                test_elf_find_output_section(image, image_size, ".data");
        const uint8_t *rela = test_elf_find_output_section(
                image, image_size, ".rela.dyn");
        const uint8_t *dynamic = test_elf_find_output_section(
                image, image_size, ".dynamic");
        assert(data != NULL && rela != NULL && dynamic != NULL);
        assert(test_elf_read_u64(rela + 32U) == LD_ELF64_RELA_SIZE);
        assert(test_elf_read_u64(rela + 56U) == LD_ELF64_RELA_SIZE);
        const uint8_t *rela_contents =
                test_pie_section_contents(image, image_size, rela);
        test_pie_output_symbol_t target =
                test_pie_find_output_symbol(image, image_size, "target");
        assert(target.found);
        const uint64_t expected = target.value + 7U;
        assert(test_elf_read_u64(rela_contents) ==
               test_elf_read_u64(data + 16U));
        assert(test_elf_read_u64(rela_contents + 8U) ==
               cases[i].relative_type);
        assert(test_elf_read_u64(rela_contents + 16U) == expected);
        assert(test_elf_read_u64(
                       test_pie_section_contents(image, image_size, data)) ==
               expected);

        uint64_t value = 0U;
        assert(test_pie_find_dynamic_tag(image, image_size, dynamic,
                                         LD_ELF_DT_RELA, &value));
        assert(value == test_elf_read_u64(rela + 16U));
        assert(test_pie_find_dynamic_tag(image, image_size, dynamic,
                                         LD_ELF_DT_RELASZ, &value));
        assert(value == LD_ELF64_RELA_SIZE);
        assert(test_pie_find_dynamic_tag(image, image_size, dynamic,
                                         LD_ELF_DT_RELAENT, &value));
        assert(value == LD_ELF64_RELA_SIZE);

        free(image);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_pie_narrow_absolute_failure_is_atomic(void) {
    size_t object_size;
    uint8_t *object = test_pie_make_relocation_object(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_32, false, 0U,
            &object_size);
    char object_path[] = "/tmp/nature-ld-pie-narrow-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const uint8_t sentinel[] = {
            0x4eU,
            0x41U,
            0x54U,
            0x55U,
            0x52U,
            0x45U,
            0x2dU,
            0x50U,
            0x49U,
            0x45U,
            0xa5U,
    };
    char output_path[] = "/tmp/nature-ld-pie-atomic-XXXXXX";
    write_fixture(output_path, sentinel, sizeof(sentinel));

    diagnostic_capture_t capture = {0};
    assert(test_pie_link(object_path, output_path, true, &capture) ==
           LD_RELOCATION_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "R_X86_64_32") != NULL);
    assert(strstr(capture.message, "cannot be used in an ELF static PIE") !=
           NULL);

    size_t output_size;
    uint8_t *output = read_test_fixture(output_path, &output_size, NULL);
    assert(output_size == sizeof(sentinel));
    assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
    free(output);

    unlink(object_path);
    unlink(output_path);
}

static void test_pie_gottpoff_sets_static_tls_flag(void) {
    size_t object_size;
    uint8_t *object =
            test_pie_make_x86_64_gottpoff_object(&object_size);
    char object_path[] = "/tmp/nature-ld-pie-gottpoff-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    char output_path[sizeof(object_path) + 8U];
    assert(snprintf(output_path, sizeof(output_path), "%s.pie",
                    object_path) > 0);
    unlink(output_path);
    diagnostic_capture_t capture = {0};
    assert(test_pie_link(object_path, output_path, true, &capture) == LD_OK);
    assert(capture.count == 0U);

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    const uint8_t *text =
            test_elf_find_output_section(image, image_size, ".text");
    const uint8_t *tdata =
            test_elf_find_output_section(image, image_size, ".tdata");
    const uint8_t *got =
            test_elf_find_output_section(image, image_size, ".got");
    const uint8_t *dynamic =
            test_elf_find_output_section(image, image_size, ".dynamic");
    assert(text && tdata && got && dynamic);
    assert((test_elf_read_u64(tdata + 8U) & LD_ELF_SHF_TLS) != 0U);
    assert(test_elf_read_u64(got + 32U) == sizeof(uint64_t));

    size_t match_count;
    const uint8_t *tls_program = test_pie_find_program_header(
            image, image_size, LD_ELF_PT_TLS, &match_count);
    assert(tls_program != NULL && match_count == 1U);
    assert(test_elf_read_u64(tls_program + 8U) ==
           test_elf_read_u64(tdata + 24U));
    assert(test_elf_read_u64(tls_program + 16U) ==
           test_elf_read_u64(tdata + 16U));
    assert(test_elf_read_u64(tls_program + 32U) ==
           test_elf_read_u64(tdata + 32U));

    uint64_t dynamic_value = 0U;
    assert(test_pie_find_dynamic_tag(image, image_size, dynamic,
                                     LD_ELF_DT_FLAGS, &dynamic_value));
    assert(dynamic_value == LD_ELF_DF_STATIC_TLS);
    assert(test_pie_find_dynamic_tag(image, image_size, dynamic,
                                     LD_ELF_DT_FLAGS_1,
                                     &dynamic_value));
    assert(dynamic_value == LD_ELF_DF_1_PIE);

    const uint8_t *text_contents =
            test_pie_section_contents(image, image_size, text);
    assert(text_contents[0] == 0x48U && text_contents[1] == 0x03U &&
           text_contents[2] == 0x05U && text_contents[7] == 0xc3U);
    int32_t displacement =
            (int32_t) test_elf_read_u32(text_contents + 3U);
    uint64_t text_address = test_elf_read_u64(text + 16U);
    assert(text_address <= (uint64_t) INT64_MAX - 7U);
    int64_t got_target = (int64_t) text_address + 7 + displacement;
    assert(got_target >= 0 &&
           (uint64_t) got_target == test_elf_read_u64(got + 16U));

    free(image);
    unlink(object_path);
    unlink(output_path);
}

static void test_pie_pc32_absolute_failure_is_atomic(void) {
    size_t object_size;
    uint8_t *object = test_pie_make_relocation_object(
            LD_ELF_EM_X86_64, LD_ELF_R_X86_64_PC32, true, 0U,
            &object_size);
    char object_path[] = "/tmp/nature-ld-pie-pc32-abs-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    /* The relocation itself is valid in ET_EXEC mode.  Static PIE rejects
     * this otherwise representable PC-relative reference specifically
     * because Zig does not permit PC32 against an SHN_ABS definition. */
    char exec_output[sizeof(object_path) + 8U];
    assert(snprintf(exec_output, sizeof(exec_output), "%s.exec",
                    object_path) > 0);
    unlink(exec_output);
    diagnostic_capture_t capture = {0};
    assert(test_pie_link(object_path, exec_output, false, &capture) ==
           LD_OK);
    assert(capture.count == 0U);

    static const uint8_t sentinel[] = {
            0x50U,
            0x43U,
            0x33U,
            0x32U,
            0x2dU,
            0x41U,
            0x42U,
            0x53U,
            0x2dU,
            0x50U,
            0x49U,
            0x45U,
    };
    char output_path[] = "/tmp/nature-ld-pie-pc32-atomic-XXXXXX";
    write_fixture(output_path, sentinel, sizeof(sentinel));

    capture = (diagnostic_capture_t) {0};
    assert(test_pie_link(object_path, output_path, true, &capture) ==
           LD_RELOCATION_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, "R_X86_64_PC32") != NULL);
    assert(strstr(capture.message, "against absolute symbol 'target'") !=
           NULL);
    assert(strstr(capture.message, "cannot be used in an ELF static PIE") !=
           NULL);

    size_t output_size;
    uint8_t *output = read_test_fixture(output_path, &output_size, NULL);
    assert(output_size == sizeof(sentinel));
    assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
    free(output);

    unlink(object_path);
    unlink(exec_output);
    unlink(output_path);
}

static void test_pie_pc_relative_weak_undefined_is_absolute_zero(void) {
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t type;
        uint32_t instruction;
        const char *name;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64, LD_ELF_R_X86_64_PC32,
             0U, "R_X86_64_PC32"},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
             LD_ELF_R_AARCH64_ADR_PREL_PG_HI21,
             UINT32_C(0x90000000), "R_AARCH64_ADR_PREL_PG_HI21"},
    };
    static const uint8_t sentinel[] = {
            0x57U,
            0x45U,
            0x41U,
            0x4bU,
            0x2dU,
            0x50U,
            0x49U,
            0x45U,
    };

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = test_pie_make_relocation_object(
                cases[i].machine, cases[i].type, false,
                cases[i].instruction, &object_size);
        test_pie_mutate_target_and_addend(
                object, object_size, LD_ELF_STB_WEAK, LD_ELF_SHN_UNDEF,
                0U, 0);
        char object_path[] = "/tmp/nature-ld-pie-weak-pc-XXXXXX";
        write_fixture(object_path, object, object_size);
        free(object);

        char exec_output[sizeof(object_path) + 8U];
        assert(snprintf(exec_output, sizeof(exec_output), "%s.exec",
                        object_path) > 0);
        unlink(exec_output);
        diagnostic_capture_t capture = {0};
        assert(test_pie_link_arch(object_path, exec_output, false,
                                  cases[i].arch, &capture) == LD_OK);
        assert(capture.count == 0U);

        char pie_output[] = "/tmp/nature-ld-pie-weak-pc-out-XXXXXX";
        write_fixture(pie_output, sentinel, sizeof(sentinel));
        capture = (diagnostic_capture_t) {0};
        assert(test_pie_link_arch(object_path, pie_output, true,
                                  cases[i].arch, &capture) ==
               LD_RELOCATION_ERROR);
        assert(capture.count > 0U);
        assert(strstr(capture.message, cases[i].name) != NULL);
        assert(strstr(capture.message,
                      "against absolute symbol 'target'") != NULL);

        size_t output_size;
        uint8_t *output = read_test_fixture(
                pie_output, &output_size, NULL);
        assert(output_size == sizeof(sentinel));
        assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
        free(output);
        unlink(object_path);
        unlink(exec_output);
        unlink(pie_output);
    }
}

static void test_pie_relative_accepts_negative_signed_addend(void) {
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t input_type;
        uint32_t relative_type;
        const char *suffix;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64, LD_ELF_R_X86_64_64,
             LD_ELF_R_X86_64_RELATIVE, "x86"},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64, LD_ELF_R_AARCH64_ABS64,
             LD_ELF_R_AARCH64_RELATIVE, "arm"},
            {LD_ARCH_RISCV64, LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64,
             LD_ELF_R_RISCV_RELATIVE, "riscv"},
    };
    const int64_t input_addend = -INT64_C(0x100000);

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = test_pie_make_relocation_object(
                cases[i].machine, cases[i].input_type, false, 0U,
                &object_size);
        test_pie_mutate_target_and_addend(
                object, object_size, LD_ELF_STB_GLOBAL, TEST_PIE_DATA,
                16U, input_addend);
        char object_path[] = "/tmp/nature-ld-pie-negative-XXXXXX";
        write_fixture(object_path, object, object_size);
        free(object);

        char output_path[sizeof(object_path) + 16U];
        assert(snprintf(output_path, sizeof(output_path), "%s.%s",
                        object_path, cases[i].suffix) > 0);
        unlink(output_path);
        diagnostic_capture_t capture = {0};
        assert(test_pie_link_arch(object_path, output_path, true,
                                  cases[i].arch, &capture) == LD_OK);
        assert(capture.count == 0U);

        size_t image_size;
        uint8_t *image = read_test_fixture(
                output_path, &image_size, NULL);
        const uint8_t *data = test_elf_find_output_section(
                image, image_size, ".data");
        const uint8_t *rela = test_elf_find_output_section(
                image, image_size, ".rela.dyn");
        assert(data != NULL && rela != NULL);
        assert(test_elf_read_u64(rela + 32U) == LD_ELF64_RELA_SIZE);
        const uint8_t *rela_contents = test_pie_section_contents(
                image, image_size, rela);
        test_pie_output_symbol_t target = test_pie_find_output_symbol(
                image, image_size, "target");
        assert(target.found && target.value < UINT64_C(0x100000));
        int64_t expected = (int64_t) target.value + input_addend;
        assert(expected < 0);
        assert(test_elf_read_u64(rela_contents + 8U) ==
               cases[i].relative_type);
        assert(test_elf_read_u64(rela_contents + 16U) ==
               (uint64_t) expected);
        assert(test_elf_read_u64(
                       test_pie_section_contents(image, image_size, data)) ==
               (uint64_t) expected);

        free(image);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_pie_relative_signed_addend_overflow_is_atomic(void) {
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t relocation_type;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64, LD_ELF_R_X86_64_64},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
             LD_ELF_R_AARCH64_ABS64},
            {LD_ARCH_RISCV64, LD_ELF_EM_RISCV, LD_ELF_R_RISCV_64},
    };
    static const uint8_t sentinel[] = {
            0x53U,
            0x49U,
            0x47U,
            0x4eU,
            0x45U,
            0x44U,
            0x2dU,
            0x41U,
            0x44U,
            0x44U,
            0x45U,
            0x4eU,
            0x44U,
    };

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = test_pie_make_relocation_object(
                cases[i].machine, cases[i].relocation_type, false, 0U,
                &object_size);
        test_pie_mutate_target_and_addend(
                object, object_size, LD_ELF_STB_GLOBAL, TEST_PIE_DATA,
                16U, INT64_MAX);
        char object_path[] = "/tmp/nature-ld-pie-addend-overflow-XXXXXX";
        write_fixture(object_path, object, object_size);
        free(object);

        char output_path[] = "/tmp/nature-ld-pie-addend-atomic-XXXXXX";
        write_fixture(output_path, sentinel, sizeof(sentinel));
        diagnostic_capture_t capture = {0};
        assert(test_pie_link_arch(object_path, output_path, true,
                                  cases[i].arch, &capture) ==
               LD_RELOCATION_ERROR);
        assert(capture.count > 0U);
        assert(strstr(capture.message,
                      "ELF static PIE S + A overflows") != NULL);
        assert(strstr(capture.message, "symbol 'target'") != NULL);

        size_t output_size;
        uint8_t *output = read_test_fixture(
                output_path, &output_size, NULL);
        assert(output_size == sizeof(sentinel));
        assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
        free(output);
        unlink(object_path);
        unlink(output_path);
    }
}

static void test_pie_expect_zig_unhandled_failure(
        ld_arch_t arch, uint16_t machine, uint32_t relocation_type,
        bool target_absolute, uint32_t relocation_word,
        const char *relocation_name) {
    size_t object_size;
    uint8_t *object = test_pie_make_relocation_object(
            machine, relocation_type, target_absolute, relocation_word,
            &object_size);
    char object_path[] = "/tmp/nature-ld-pie-zig-policy-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const uint8_t sentinel[] = {
            0x5aU,
            0x49U,
            0x47U,
            0x2dU,
            0x50U,
            0x49U,
            0x45U,
            0x2dU,
            0x50U,
            0x4fU,
            0x4cU,
            0x49U,
            0x43U,
            0x59U,
    };
    char output_path[] = "/tmp/nature-ld-pie-zig-atomic-XXXXXX";
    write_fixture(output_path, sentinel, sizeof(sentinel));

    diagnostic_capture_t capture = {0};
    assert(test_pie_link_arch(object_path, output_path, true, arch,
                              &capture) == LD_RELOCATION_ERROR);
    assert(capture.count > 0U);
    assert(strstr(capture.message, relocation_name) != NULL);
    assert(strstr(capture.message,
                  "is not supported in an ELF static PIE") != NULL);

    size_t output_size;
    uint8_t *output = read_test_fixture(output_path, &output_size, NULL);
    assert(output_size == sizeof(sentinel));
    assert(memcmp(output, sentinel, sizeof(sentinel)) == 0);
    free(output);

    unlink(object_path);
    unlink(output_path);
}

static void test_pie_rejects_zig_unhandled_aarch64_movw(void) {
    static const struct {
        uint32_t type;
        const char *name;
    } cases[] = {
            {LD_ELF_R_AARCH64_MOVW_UABS_G0,
             "R_AARCH64_MOVW_UABS_G0"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G0_NC,
             "R_AARCH64_MOVW_UABS_G0_NC"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G1,
             "R_AARCH64_MOVW_UABS_G1"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G1_NC,
             "R_AARCH64_MOVW_UABS_G1_NC"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G2,
             "R_AARCH64_MOVW_UABS_G2"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G2_NC,
             "R_AARCH64_MOVW_UABS_G2_NC"},
            {LD_ELF_R_AARCH64_MOVW_UABS_G3,
             "R_AARCH64_MOVW_UABS_G3"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_pie_expect_zig_unhandled_failure(
                LD_ARCH_ARM64, LD_ELF_EM_AARCH64, cases[i].type, false,
                UINT32_C(0xd2800000), cases[i].name);
    }
}

static void test_pie_rejects_zig_unhandled_x86_pc_absolute(void) {
    static const struct {
        uint32_t type;
        const char *name;
    } cases[] = {
            {LD_ELF_R_X86_64_PC8, "R_X86_64_PC8"},
            {LD_ELF_R_X86_64_PC16, "R_X86_64_PC16"},
            {LD_ELF_R_X86_64_PC64, "R_X86_64_PC64"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_pie_expect_zig_unhandled_failure(
                LD_ARCH_AMD64, LD_ELF_EM_X86_64, cases[i].type, true, 0U,
                cases[i].name);
    }
}

static void test_pie_rejects_zig_unhandled_absolute_widths(void) {
    static const struct {
        ld_arch_t arch;
        uint16_t machine;
        uint32_t type;
        const char *name;
    } cases[] = {
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64, LD_ELF_R_X86_64_8,
             "R_X86_64_8"},
            {LD_ARCH_AMD64, LD_ELF_EM_X86_64, LD_ELF_R_X86_64_16,
             "R_X86_64_16"},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
             LD_ELF_R_AARCH64_ABS16, "R_AARCH64_ABS16"},
            {LD_ARCH_ARM64, LD_ELF_EM_AARCH64,
             LD_ELF_R_AARCH64_ABS32, "R_AARCH64_ABS32"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_pie_expect_zig_unhandled_failure(
                cases[i].arch, cases[i].machine, cases[i].type, true, 0U,
                cases[i].name);
    }
}

static void test_pie_rejects_zig_unhandled_riscv_extensions(void) {
    static const struct {
        uint32_t type;
        const char *name;
    } cases[] = {
            {LD_ELF_R_RISCV_BRANCH, "R_RISCV_BRANCH"},
            {LD_ELF_R_RISCV_JAL, "R_RISCV_JAL"},
            {LD_ELF_R_RISCV_CALL, "R_RISCV_CALL"},
            {LD_ELF_R_RISCV_ADD16, "R_RISCV_ADD16"},
            {LD_ELF_R_RISCV_SUB64, "R_RISCV_SUB64"},
            {LD_ELF_R_RISCV_RELAX, "R_RISCV_RELAX"},
            {LD_ELF_R_RISCV_32_PCREL, "R_RISCV_32_PCREL"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_pie_expect_zig_unhandled_failure(
                LD_ARCH_RISCV64, LD_ELF_EM_RISCV, cases[i].type, true,
                0U, cases[i].name);
    }
}

static uint32_t test_pie_riscv_i_immediate(uint32_t instruction) {
    return instruction >> 20U;
}

static uint32_t test_pie_riscv_s_immediate(uint32_t instruction) {
    return ((instruction >> 25U) << 5U) |
           ((instruction >> 7U) & 0x1fU);
}

static void test_pie_accepts_riscv_lo12(void) {
    static const struct {
        uint32_t type;
        uint32_t instruction;
        bool is_i_type;
    } cases[] = {
            {LD_ELF_R_RISCV_LO12_I, UINT32_C(0x00000013), true},
            {LD_ELF_R_RISCV_LO12_S, UINT32_C(0x00002023), false},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        size_t object_size;
        uint8_t *object = test_pie_make_relocation_object(
                LD_ELF_EM_RISCV, cases[i].type, false,
                cases[i].instruction, &object_size);
        char object_path[] = "/tmp/nature-ld-pie-riscv-lo12-XXXXXX";
        write_fixture(object_path, object, object_size);
        free(object);

        char output_path[sizeof(object_path) + 8U];
        assert(snprintf(output_path, sizeof(output_path), "%s.pie",
                        object_path) > 0);
        unlink(output_path);
        diagnostic_capture_t capture = {0};
        assert(test_pie_link_arch(object_path, output_path, true,
                                  LD_ARCH_RISCV64, &capture) == LD_OK);
        assert(capture.count == 0U);

        size_t image_size;
        uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
        assert(test_elf_read_u16(image + 16U) == LD_ELF_ET_DYN);
        assert(test_elf_read_u16(image + 18U) == LD_ELF_EM_RISCV);
        const uint8_t *data =
                test_elf_find_output_section(image, image_size, ".data");
        const uint8_t *rela = test_elf_find_output_section(
                image, image_size, ".rela.dyn");
        const uint8_t *dynamic = test_elf_find_output_section(
                image, image_size, ".dynamic");
        assert(data != NULL && rela != NULL && dynamic != NULL);
        assert(test_elf_read_u64(rela + 32U) == 0U);
        uint64_t dynamic_value = 0U;
        assert(!test_pie_find_dynamic_tag(
                image, image_size, dynamic, LD_ELF_DT_RELA,
                &dynamic_value));
        assert(!test_pie_find_dynamic_tag(
                image, image_size, dynamic, LD_ELF_DT_RELASZ,
                &dynamic_value));
        assert(!test_pie_find_dynamic_tag(
                image, image_size, dynamic, LD_ELF_DT_RELAENT,
                &dynamic_value));

        test_pie_output_symbol_t target =
                test_pie_find_output_symbol(image, image_size, "target");
        assert(target.found);
        uint32_t instruction = test_elf_read_u32(
                test_pie_section_contents(image, image_size, data));
        uint32_t immediate = cases[i].is_i_type
                                     ? test_pie_riscv_i_immediate(instruction)
                                     : test_pie_riscv_s_immediate(instruction);
        assert(immediate == ((target.value + 7U) & 0xfffU));

        free(image);
        unlink(object_path);
        unlink(output_path);
    }
}

void test_ld_elf_pie(void) {
    test_pie_zig_relocation_policy();
    test_pie_mode_and_backend_metadata();
    test_pie_arch_relative_final_outputs();
    test_pie_narrow_absolute_failure_is_atomic();
    test_pie_gottpoff_sets_static_tls_flag();
    test_pie_pc32_absolute_failure_is_atomic();
    test_pie_pc_relative_weak_undefined_is_absolute_zero();
    test_pie_relative_accepts_negative_signed_addend();
    test_pie_relative_signed_addend_overflow_is_atomic();
    test_pie_rejects_zig_unhandled_aarch64_movw();
    test_pie_rejects_zig_unhandled_x86_pc_absolute();
    test_pie_rejects_zig_unhandled_absolute_widths();
    test_pie_rejects_zig_unhandled_riscv_extensions();
    test_pie_accepts_riscv_lo12();
}

#ifdef TEST_LD_ELF_PIE_MAIN
int main(void) {
    test_ld_elf_pie();
    return 0;
}
#endif
