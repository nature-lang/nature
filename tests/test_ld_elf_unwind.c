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

static void test_elf_init_rel_header(uint8_t *bytes, uint16_t machine,
                                     size_t section_table_offset,
                                     uint16_t section_count,
                                     uint16_t section_names_index) {
    memset(bytes, 0, LD_ELF64_EHDR_SIZE);
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
    test_elf_write_u16(bytes + 62U, section_names_index);
}

typedef struct {
    bool dwarf64;
    bool owner_relocation;
    bool cie_pointer_into_record;
    bool nonzero_after_terminator;
    bool terminator_relocation;
    bool cross_record_relocation;
    bool section_anchor_relocation;
    const char *symbol_name;
    uint8_t cie_return_register;
} test_elf_eh_frame_options_t;

/*
 * Build an x86_64 ET_REL containing one CIE and one FDE.  DWARF64 changes
 * only the initial-length field: the .eh_frame CIE id/pointer remains four
 * bytes wide.  The optional malformed forms exercise record-boundary checks
 * without relying on a host assembler.
 */
static uint8_t *make_test_elf_eh_frame_object(
        const test_elf_eh_frame_options_t *options, size_t *result_size) {
    enum {
        text_index = 1,
        eh_frame_index = 2,
        rela_index = 3,
        symtab_index = 4,
        strtab_index = 5,
        shstrtab_index = 6,
        section_count = 7,
    };
    assert(options != NULL && result_size != NULL);

    char symbol_names[32] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            options->symbol_name ? options->symbol_name : "_start");

    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text");
    const uint32_t eh_frame_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".eh_frame");
    const uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.eh_frame");
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
    const size_t eh_frame_offset =
            test_elf_align(text_offset + text_size, 8U);
    const size_t length_field_size = options->dwarf64 ? 12U : 4U;
    const size_t cie_size = length_field_size + 12U;
    const size_t fde_offset = cie_size;
    const size_t fde_size = length_field_size + 20U;
    const size_t terminator_offset = fde_offset + fde_size;
    const size_t eh_frame_size = terminator_offset + 2U * sizeof(uint32_t);
    size_t relocation_count = options->owner_relocation ? 1U : 0U;
    if (options->terminator_relocation) relocation_count++;
    if (options->cross_record_relocation) relocation_count++;
    if (options->section_anchor_relocation) relocation_count++;
    const size_t rela_offset =
            test_elf_align(eh_frame_offset + eh_frame_size, 8U);
    const size_t relocation_size = relocation_count * LD_ELF64_RELA_SIZE;
    const size_t symbol_names_offset = rela_offset + relocation_size;
    const size_t section_names_offset = symbol_names_offset + symbol_names_size;
    const size_t symbols_offset = test_elf_align(
            section_names_offset + section_names_size, 8U);
    const size_t symbol_count = 4U;
    const size_t section_table_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);
    test_elf_init_rel_header(bytes, LD_ELF_EM_X86_64, section_table_offset,
                             section_count, shstrtab_index);

    /* _start exits with status zero if a failed test is ever run manually. */
    static const uint8_t text[] = {
            0xb8U, 0x3cU, 0x00U, 0x00U, 0x00U, /* mov $60, %eax */
            0x31U, 0xffU, /* xor %edi, %edi */
            0x0fU, 0x05U, /* syscall */
    };
    memcpy(bytes + text_offset, text, sizeof(text));

    uint8_t *eh_frame = bytes + eh_frame_offset;
    if (options->dwarf64) {
        test_elf_write_u32(eh_frame, UINT32_MAX);
        test_elf_write_u64(eh_frame + 4U, 12U);
    } else {
        test_elf_write_u32(eh_frame, 12U);
    }
    const size_t cie_id_offset = length_field_size;
    test_elf_write_u32(eh_frame + cie_id_offset, 0U);
    eh_frame[cie_id_offset + 4U] = 1U; /* version */
    eh_frame[cie_id_offset + 5U] = 0U; /* augmentation */
    eh_frame[cie_id_offset + 6U] = 1U; /* code alignment */
    eh_frame[cie_id_offset + 7U] = 0x7cU; /* data alignment, SLEB -4 */
    eh_frame[cie_id_offset + 8U] = options->cie_return_register
                                           ? options->cie_return_register
                                           : 16U;

    uint8_t *fde = eh_frame + fde_offset;
    if (options->dwarf64) {
        test_elf_write_u32(fde, UINT32_MAX);
        test_elf_write_u64(fde + 4U, 20U);
    } else {
        test_elf_write_u32(fde, 20U);
    }
    const size_t cie_pointer_offset = fde_offset + length_field_size;
    uint32_t cie_delta = (uint32_t) cie_pointer_offset;
    if (options->cie_pointer_into_record) {
        assert(cie_delta >= sizeof(uint32_t));
        cie_delta -= sizeof(uint32_t);
    }
    test_elf_write_u32(eh_frame + cie_pointer_offset, cie_delta);
    test_elf_write_u32(eh_frame + terminator_offset, 0U);
    if (options->nonzero_after_terminator)
        eh_frame[terminator_offset + sizeof(uint32_t)] = 0x7fU;

    uint8_t *relocations = bytes + rela_offset;
    size_t relocation_index = 0U;
    if (options->owner_relocation) {
        uint8_t *relocation =
                relocations + relocation_index++ * LD_ELF64_RELA_SIZE;
        test_elf_write_u64(relocation,
                           cie_pointer_offset + sizeof(uint32_t));
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(1U, LD_ELF_R_X86_64_64));
    }
    if (options->section_anchor_relocation) {
        uint8_t *relocation =
                relocations + relocation_index++ * LD_ELF64_RELA_SIZE;
        /* Model crtbegin's reference to the zero-valued .eh_frame section
           symbol.  The field is inside the live FDE, while the input CIE at
           offset zero can be removed by cross-object CIE deduplication. */
        test_elf_write_u64(relocation, cie_pointer_offset + 12U);
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(2U, LD_ELF_R_X86_64_64));
    }
    if (options->cross_record_relocation) {
        uint8_t *relocation =
                relocations + relocation_index++ * LD_ELF64_RELA_SIZE;
        /* The relocation begins in the CIE but its eight-byte write reaches
           into the following FDE. */
        test_elf_write_u64(relocation, cie_size - sizeof(uint32_t));
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(1U, LD_ELF_R_X86_64_64));
    }
    if (options->terminator_relocation) {
        uint8_t *relocation =
                relocations + relocation_index++ * LD_ELF64_RELA_SIZE;
        test_elf_write_u64(relocation, terminator_offset);
        test_elf_write_u64(
                relocation + 8U,
                LD_ELF_RELA_INFO(1U, LD_ELF_R_X86_64_64));
    }
    assert(relocation_index == relocation_count);

    memcpy(bytes + symbol_names_offset, symbol_names, symbol_names_size);
    memcpy(bytes + section_names_offset, section_names, section_names_size);
    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *text_section = symbols + LD_ELF64_SYM_SIZE;
    text_section[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(text_section + 6U, text_index);
    uint8_t *eh_frame_section = text_section + LD_ELF64_SYM_SIZE;
    eh_frame_section[4] =
            LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(eh_frame_section + 6U, eh_frame_index);
    uint8_t *start = eh_frame_section + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, text_index);
    test_elf_write_u64(start + 16U, text_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(
            sections + (size_t) text_index * LD_ELF64_SHDR_SIZE, text_name,
            LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, text_offset, text_size,
            0U, 0U, 16U, 0U);
    test_elf_write_section(
            sections + (size_t) eh_frame_index * LD_ELF64_SHDR_SIZE,
            eh_frame_name, LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC,
            eh_frame_offset, eh_frame_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + (size_t) rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset, relocation_size, symtab_index,
            eh_frame_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 3U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
            symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *make_test_elf_eh_comdat_winner(size_t *result_size) {
    enum {
        group_index = 1,
        text_index = 2,
        symtab_index = 3,
        strtab_index = 4,
        shstrtab_index = 5,
        section_count = 6,
    };
    const uint32_t winner_marker = 0xc0dec0deU;
    const size_t group_offset = LD_ELF64_EHDR_SIZE;
    const size_t group_size = 2U * sizeof(uint32_t);
    const size_t text_offset = test_elf_align(group_offset + group_size, 4U);
    const size_t text_size = 8U;

    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t signature_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "comdat.eh");
    const uint32_t start_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size, "_start");

    char section_names[160] = {0};
    size_t section_names_size = 1U;
    const uint32_t group_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".group");
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.comdat.eh");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t symbol_names_offset = text_offset + text_size;
    const size_t section_names_offset = symbol_names_offset + symbol_names_size;
    const size_t symbols_offset = test_elf_align(
            section_names_offset + section_names_size, 8U);
    const size_t symbol_count = 3U;
    const size_t section_table_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);
    test_elf_init_rel_header(bytes, LD_ELF_EM_X86_64, section_table_offset,
                             section_count, shstrtab_index);

    uint8_t *group = bytes + group_offset;
    test_elf_write_u32(group, LD_ELF_GRP_COMDAT);
    test_elf_write_u32(group + 4U, text_index);

    test_elf_write_u32(bytes + text_offset, winner_marker);
    test_elf_write_u32(bytes + text_offset + 4U, 0xc3U); /* ret */
    memcpy(bytes + symbol_names_offset, symbol_names, symbol_names_size);
    memcpy(bytes + section_names_offset, section_names, section_names_size);

    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *signature = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(signature, signature_name);
    signature[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(signature + 6U, text_index);
    uint8_t *start = signature + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(start, start_name);
    start[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(start + 6U, text_index);
    test_elf_write_u64(start + 8U, 0U);
    test_elf_write_u64(start + 16U, text_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + (size_t) group_index * LD_ELF64_SHDR_SIZE,
                           group_name, LD_ELF_SHT_GROUP, 0U, group_offset,
                           group_size, symtab_index, 1U, 4U, 4U);
    test_elf_write_section(sections + (size_t) text_index * LD_ELF64_SHDR_SIZE,
                           text_name, LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR |
                                   LD_ELF_SHF_GROUP,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 2U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
            symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *make_test_elf_eh_comdat_loser(size_t *result_size) {
    enum {
        group_index = 1,
        dead_text_index = 2,
        live_text_index = 3,
        eh_frame_index = 4,
        rela_index = 5,
        symtab_index = 6,
        strtab_index = 7,
        shstrtab_index = 8,
        section_count = 9,
    };
    const uint32_t dead_marker = 0xd3ad0badU;
    const uint32_t live_marker = 0x11a7e5edU;
    const size_t group_offset = LD_ELF64_EHDR_SIZE;
    const size_t group_size = 2U * sizeof(uint32_t);
    const size_t dead_text_offset = test_elf_align(group_offset + group_size, 4U);
    const size_t text_size = 8U;
    const size_t live_text_offset =
            test_elf_align(dead_text_offset + text_size, 4U);
    const size_t eh_frame_offset =
            test_elf_align(live_text_offset + text_size, 8U);
    const size_t eh_frame_size = 60U;
    const size_t rela_offset = test_elf_align(eh_frame_offset + eh_frame_size,
                                              8U);
    const size_t relocation_count = 3U;
    const size_t relocation_size = relocation_count * LD_ELF64_RELA_SIZE;

    char symbol_names[128] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t signature_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "comdat.eh");
    const uint32_t live_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "live_marker");
    const uint32_t dead_provider_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "dead_provider");

    char section_names[256] = {0};
    size_t section_names_size = 1U;
    const uint32_t group_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".group");
    const uint32_t dead_text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.dead.comdat");
    const uint32_t live_text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.live");
    const uint32_t eh_frame_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".eh_frame");
    const uint32_t rela_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".rela.eh_frame");
    const uint32_t symtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".symtab");
    const uint32_t strtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".strtab");
    const uint32_t shstrtab_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".shstrtab");

    const size_t symbols_offset = test_elf_align(
            rela_offset + relocation_size + symbol_names_size +
                    section_names_size,
            8U);
    const size_t symbol_count = 6U;
    const size_t symbol_names_offset = rela_offset + relocation_size;
    const size_t section_names_offset = symbol_names_offset + symbol_names_size;
    const size_t section_table_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t size = section_table_offset +
                        (size_t) section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);
    test_elf_init_rel_header(bytes, LD_ELF_EM_X86_64, section_table_offset,
                             section_count, shstrtab_index);

    uint8_t *group = bytes + group_offset;
    test_elf_write_u32(group, LD_ELF_GRP_COMDAT);
    test_elf_write_u32(group + 4U, dead_text_index);

    test_elf_write_u32(bytes + dead_text_offset, dead_marker);
    test_elf_write_u32(bytes + dead_text_offset + 4U, 0xc3U);
    test_elf_write_u32(bytes + live_text_offset, live_marker);
    test_elf_write_u32(bytes + live_text_offset + 4U, 0xc3U);

    /* CIE, dead FDE, live FDE, and the required zero terminator. */
    uint8_t *eh_frame = bytes + eh_frame_offset;
    test_elf_write_u32(eh_frame, 12U);
    test_elf_write_u32(eh_frame + 4U, 0U);
    eh_frame[8] = 1U; /* version */
    eh_frame[9] = 0U; /* augmentation */
    eh_frame[10] = 1U; /* code alignment */
    eh_frame[11] = 0x7cU; /* data alignment, SLEB -4 */
    eh_frame[12] = 16U; /* return register */

    test_elf_write_u32(eh_frame + 16U, 20U);
    test_elf_write_u32(eh_frame + 20U, 20U); /* CIE at offset zero */
    /* [24, 32) is FDE_A's initial location, relocated to dead text. */
    /* [32, 40) is a dead-only strong undefined relocation. */

    test_elf_write_u32(eh_frame + 40U, 12U);
    test_elf_write_u32(eh_frame + 44U, 44U); /* original CIE delta */
    /* [48, 56) is FDE_B's initial location, relocated to live text. */
    test_elf_write_u32(eh_frame + 56U, 0U); /* terminator */

    uint8_t *relocations = bytes + rela_offset;
    test_elf_write_u64(relocations, 24U);
    test_elf_write_u64(relocations + 8U,
                       LD_ELF_RELA_INFO(2U, LD_ELF_R_X86_64_64));
    test_elf_write_u64(relocations + 16U, 0U);
    test_elf_write_u64(relocations + LD_ELF64_RELA_SIZE, 32U);
    test_elf_write_u64(relocations + LD_ELF64_RELA_SIZE + 8U,
                       LD_ELF_RELA_INFO(5U, LD_ELF_R_X86_64_64));
    test_elf_write_u64(relocations + LD_ELF64_RELA_SIZE + 16U, 0U);
    test_elf_write_u64(relocations + 2U * LD_ELF64_RELA_SIZE, 48U);
    test_elf_write_u64(relocations + 2U * LD_ELF64_RELA_SIZE + 8U,
                       LD_ELF_RELA_INFO(3U, LD_ELF_R_X86_64_64));
    test_elf_write_u64(relocations + 2U * LD_ELF64_RELA_SIZE + 16U, 0U);

    memcpy(bytes + symbol_names_offset, symbol_names, symbol_names_size);
    memcpy(bytes + section_names_offset, section_names, section_names_size);
    uint8_t *symbols = bytes + symbols_offset;
    uint8_t *signature = symbols + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(signature, signature_name);
    signature[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(signature + 6U, dead_text_index);
    uint8_t *dead_section = signature + LD_ELF64_SYM_SIZE;
    dead_section[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(dead_section + 6U, dead_text_index);
    uint8_t *live_section = dead_section + LD_ELF64_SYM_SIZE;
    live_section[4] = LD_ELF_SYM_INFO(LD_ELF_STB_LOCAL, LD_ELF_STT_SECTION);
    test_elf_write_u16(live_section + 6U, live_text_index);
    uint8_t *live = live_section + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(live, live_name);
    live[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(live + 6U, live_text_index);
    test_elf_write_u64(live + 16U, text_size);
    uint8_t *undefined = live + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(undefined, dead_provider_name);
    undefined[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_NOTYPE);
    test_elf_write_u16(undefined + 6U, LD_ELF_SHN_UNDEF);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + (size_t) group_index * LD_ELF64_SHDR_SIZE,
                           group_name, LD_ELF_SHT_GROUP, 0U, group_offset,
                           group_size, symtab_index, 1U, 4U, 4U);
    test_elf_write_section(
            sections + (size_t) dead_text_index * LD_ELF64_SHDR_SIZE,
            dead_text_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_GROUP,
            dead_text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + (size_t) live_text_index * LD_ELF64_SHDR_SIZE,
            live_text_name, LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, live_text_offset,
            text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(
            sections + (size_t) eh_frame_index * LD_ELF64_SHDR_SIZE,
            eh_frame_name, LD_ELF_SHT_PROGBITS, LD_ELF_SHF_ALLOC,
            eh_frame_offset, eh_frame_size, 0U, 0U, 8U, 0U);
    test_elf_write_section(
            sections + (size_t) rela_index * LD_ELF64_SHDR_SIZE, rela_name,
            LD_ELF_SHT_RELA, 0U, rela_offset, relocation_size, symtab_index,
            eh_frame_index, 8U, LD_ELF64_RELA_SIZE);
    test_elf_write_section(
            sections + (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
            symbol_count * LD_ELF64_SYM_SIZE, strtab_index, 4U, 8U,
            LD_ELF64_SYM_SIZE);
    test_elf_write_section(
            sections + (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
            symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(
            sections + (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, section_names_offset,
            section_names_size, 0U, 0U, 1U, 0U);

    *result_size = size;
    return bytes;
}

static uint8_t *make_test_elf_eh_archive_provider(size_t *result_size) {
    static const uint32_t provider_marker = 0xbadc0ffeU;
    char symbol_names[64] = {0};
    size_t symbol_names_size = 1U;
    const uint32_t provider_name = test_elf_append_name(
            symbol_names, sizeof(symbol_names), &symbol_names_size,
            "dead_provider");
    char section_names[128] = {0};
    size_t section_names_size = 1U;
    const uint32_t text_name = test_elf_append_name(
            section_names, sizeof(section_names), &section_names_size,
            ".text.provider");
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
    const size_t symbol_names_offset = text_offset + text_size;
    const size_t section_names_offset = symbol_names_offset + symbol_names_size;
    const size_t symbols_offset = test_elf_align(
            section_names_offset + section_names_size, 8U);
    const size_t symbol_count = 2U;
    const size_t section_table_offset =
            symbols_offset + symbol_count * LD_ELF64_SYM_SIZE;
    const size_t section_count = 5U;
    const size_t size = section_table_offset +
                        section_count * LD_ELF64_SHDR_SIZE;
    uint8_t *bytes = calloc(1, size);
    assert(bytes != NULL);
    test_elf_init_rel_header(bytes, LD_ELF_EM_X86_64, section_table_offset,
                             (uint16_t) section_count, 4U);
    test_elf_write_u32(bytes + text_offset, provider_marker);
    test_elf_write_u32(bytes + text_offset + 4U, 0xc3U);
    memcpy(bytes + symbol_names_offset, symbol_names, symbol_names_size);
    memcpy(bytes + section_names_offset, section_names, section_names_size);
    uint8_t *provider = bytes + symbols_offset + LD_ELF64_SYM_SIZE;
    test_elf_write_u32(provider, provider_name);
    provider[4] = LD_ELF_SYM_INFO(LD_ELF_STB_GLOBAL, LD_ELF_STT_FUNC);
    test_elf_write_u16(provider + 6U, 1U);
    test_elf_write_u64(provider + 16U, text_size);

    uint8_t *sections = bytes + section_table_offset;
    test_elf_write_section(sections + LD_ELF64_SHDR_SIZE, text_name,
                           LD_ELF_SHT_PROGBITS,
                           LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR,
                           text_offset, text_size, 0U, 0U, 4U, 0U);
    test_elf_write_section(sections + 2U * LD_ELF64_SHDR_SIZE, symtab_name,
                           LD_ELF_SHT_SYMTAB, 0U, symbols_offset,
                           symbol_count * LD_ELF64_SYM_SIZE, 3U, 1U, 8U,
                           LD_ELF64_SYM_SIZE);
    test_elf_write_section(sections + 3U * LD_ELF64_SHDR_SIZE, strtab_name,
                           LD_ELF_SHT_STRTAB, 0U, symbol_names_offset,
                           symbol_names_size, 0U, 0U, 1U, 0U);
    test_elf_write_section(sections + 4U * LD_ELF64_SHDR_SIZE, shstrtab_name,
                           LD_ELF_SHT_STRTAB, 0U, section_names_offset,
                           section_names_size, 0U, 0U, 1U, 0U);
    *result_size = size;
    return bytes;
}

static int link_test_elf_amd64_inputs(const char *output_path,
                                      const char *const *input_paths,
                                      size_t input_count,
                                      diagnostic_capture_t *capture) {
    unlink(output_path);
    ld_options_t options;
    ld_options_init(&options);
    options.os = LD_OS_LINUX;
    options.arch = LD_ARCH_AMD64;
    options.output_path = output_path;
    options.entry_symbol = "_start";
    options.pie = false;
    options.adhoc_codesign = false;
    options.diagnostic = capture_diagnostic;
    options.diagnostic_context = capture;
    for (size_t i = 0; i < input_count; i++)
        assert(ld_add_input(&options, input_paths[i]) == LD_OK);
    int status = ld_link(&options);
    ld_options_deinit(&options);
    return status;
}

static void test_elf_assert_eh_record_layout(
        const uint8_t *image, size_t image_size, size_t expected_cies,
        size_t expected_fdes, uint64_t expected_size,
        bool every_fde_uses_first_cie) {
    const uint8_t *section =
            test_elf_find_output_section(image, image_size, ".eh_frame");
    assert(section != NULL);
    uint64_t section_offset = test_elf_read_u64(section + 24U);
    uint64_t section_size = test_elf_read_u64(section + 32U);
    assert(section_size == expected_size);
    assert(section_offset <= image_size &&
           section_size <= image_size - section_offset);
    const uint8_t *bytes = image + (size_t) section_offset;
    uint64_t cie_offsets[8];
    size_t cie_count = 0U;
    size_t fde_count = 0U;
    uint64_t cursor = 0U;
    while (cursor + sizeof(uint32_t) <= section_size) {
        uint32_t length = test_elf_read_u32(bytes + (size_t) cursor);
        if (length == 0U) {
            cursor += sizeof(uint32_t);
            break;
        }
        assert(length != UINT32_MAX);
        uint64_t record_size = sizeof(uint32_t) + (uint64_t) length;
        assert(record_size >= 8U && record_size <= section_size - cursor);
        uint32_t cie_pointer =
                test_elf_read_u32(bytes + (size_t) cursor + 4U);
        if (cie_pointer == 0U) {
            assert(cie_count < sizeof(cie_offsets) / sizeof(cie_offsets[0]));
            cie_offsets[cie_count++] = cursor;
        } else {
            assert(cie_pointer <= cursor + 4U);
            uint64_t target = cursor + 4U - cie_pointer;
            bool found = false;
            for (size_t i = 0; i < cie_count; i++) {
                if (cie_offsets[i] == target) found = true;
            }
            assert(found);
            if (every_fde_uses_first_cie) {
                assert(cie_count != 0U && target == cie_offsets[0]);
            }
            fde_count++;
        }
        cursor += record_size;
    }
    assert(cursor == section_size);
    assert(cie_count == expected_cies);
    assert(fde_count == expected_fdes);

    const uint8_t *header =
            test_elf_find_output_section(image, image_size, ".eh_frame_hdr");
    assert(header != NULL);
    uint64_t header_offset = test_elf_read_u64(header + 24U);
    uint64_t header_size = test_elf_read_u64(header + 32U);
    assert(header_offset <= image_size &&
           header_size <= image_size - header_offset && header_size >= 12U);
    assert(test_elf_read_u32(image + (size_t) header_offset + 8U) ==
           expected_fdes);
}

static void test_elf_cross_object_cie_deduplication(void) {
    test_elf_eh_frame_options_t first_options = {
            .owner_relocation = true,
    };
    test_elf_eh_frame_options_t same_options = {
            .owner_relocation = true,
            .section_anchor_relocation = true,
            .symbol_name = "second_fn",
    };
    test_elf_eh_frame_options_t different_options = {
            .owner_relocation = true,
            .symbol_name = "second_fn",
            .cie_return_register = 17U,
    };
    size_t first_size, same_size, different_size;
    uint8_t *first =
            make_test_elf_eh_frame_object(&first_options, &first_size);
    uint8_t *same =
            make_test_elf_eh_frame_object(&same_options, &same_size);
    uint8_t *different = make_test_elf_eh_frame_object(
            &different_options, &different_size);
    char first_path[] = "/tmp/nature-ld-cie-first-XXXXXX";
    char same_path[] = "/tmp/nature-ld-cie-same-XXXXXX";
    char different_path[] = "/tmp/nature-ld-cie-different-XXXXXX";
    write_fixture(first_path, first, first_size);
    write_fixture(same_path, same, same_size);
    write_fixture(different_path, different, different_size);
    free(first);
    free(same);
    free(different);

    static const char *output_path = "/tmp/nature-ld-cie-dedup-output";
    diagnostic_capture_t capture = {0};
    const char *same_inputs[] = {first_path, same_path};
    assert(link_test_elf_amd64_inputs(output_path, same_inputs, 2U,
                                      &capture) == LD_OK);
    assert(capture.count == 0U);
    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    test_elf_assert_eh_record_layout(image, image_size, 1U, 2U, 68U, true);
    const uint8_t *eh_frame =
            test_elf_find_output_section(image, image_size, ".eh_frame");
    assert(eh_frame != NULL);
    uint64_t eh_frame_address = test_elf_read_u64(eh_frame + 16U);
    uint64_t eh_frame_offset = test_elf_read_u64(eh_frame + 24U);
    /* CIE (16) + first FDE (24) + 16-byte offset of the range field in
       the second FDE.  The relocated value must be the combined section
       start even though the second object's input CIE was deduplicated. */
    assert(eh_frame_offset <= image_size &&
           64U <= image_size - eh_frame_offset);
    assert(test_elf_read_u64(image + (size_t) eh_frame_offset + 56U) ==
           eh_frame_address);
    free(image);

    memset(&capture, 0, sizeof(capture));
    const char *different_inputs[] = {first_path, different_path};
    assert(link_test_elf_amd64_inputs(output_path, different_inputs, 2U,
                                      &capture) == LD_OK);
    assert(capture.count == 0U);
    image = read_test_fixture(output_path, &image_size, NULL);
    test_elf_assert_eh_record_layout(image, image_size, 2U, 2U, 84U, false);
    free(image);

    unlink(first_path);
    unlink(same_path);
    unlink(different_path);
    unlink(output_path);
}

static bool test_elf_find_eh_cie(const uint8_t *image, size_t image_size,
                                 size_t *offset_result) {
    for (size_t offset = 0; offset + 16U <= image_size; offset++) {
        if (test_elf_read_u32(image + offset) != 12U ||
            test_elf_read_u32(image + offset + 4U) != 0U ||
            image[offset + 8U] != 1U || image[offset + 9U] != 0U ||
            image[offset + 10U] != 1U || image[offset + 11U] != 0x7cU ||
            image[offset + 12U] != 16U) {
            continue;
        }
        if (offset_result) *offset_result = offset;
        return true;
    }
    return false;
}

static bool test_elf_find_dwarf64_eh_cie(const uint8_t *image,
                                         size_t image_size,
                                         size_t *offset_result) {
    for (size_t offset = 0; offset + 24U <= image_size; offset++) {
        if (test_elf_read_u32(image + offset) != UINT32_MAX ||
            test_elf_read_u64(image + offset + 4U) != 12U ||
            test_elf_read_u32(image + offset + 12U) != 0U ||
            image[offset + 16U] != 1U || image[offset + 17U] != 0U ||
            image[offset + 18U] != 1U || image[offset + 19U] != 0x7cU ||
            image[offset + 20U] != 16U) {
            continue;
        }
        if (offset_result) *offset_result = offset;
        return true;
    }
    return false;
}

static void test_elf_expect_eh_frame_failure(
        const test_elf_eh_frame_options_t *fixture_options,
        int expected_status, const char *message_fragment) {
    size_t object_size;
    uint8_t *object =
            make_test_elf_eh_frame_object(fixture_options, &object_size);
    char object_path[] = "/tmp/nature-ld-eh-invalid-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);

    static const char *output_path = "/tmp/nature-ld-eh-invalid-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    int status =
            link_test_elf_amd64_inputs(output_path, inputs, 1U, &capture);
    if (status != expected_status || capture.count == 0U ||
        strstr(capture.message, message_fragment) == NULL) {
        fprintf(stderr,
                ".eh_frame failure mismatch: status=%d expected=%d, "
                "diagnostic='%s', expected fragment='%s'\n",
                status, expected_status, capture.message, message_fragment);
    }
    assert(status == expected_status);
    assert(capture.count > 0U);
    assert(strstr(capture.message, message_fragment) != NULL);
    assert(access(output_path, F_OK) != 0);
    unlink(object_path);
    unlink(output_path);
}

static void test_elf_eh_frame_boundaries(void) {
    static const char *output_path = "/tmp/nature-ld-eh-boundary-output";

    /* An ET_REL FDE without an initial-location relocation has no input atom
       owner.  Match Zig by dropping both the FDE and its now-unused CIE. */
    test_elf_eh_frame_options_t fixture_options = {0};
    size_t object_size;
    uint8_t *object =
            make_test_elf_eh_frame_object(&fixture_options, &object_size);
    char object_path[] = "/tmp/nature-ld-eh-no-owner-XXXXXX";
    write_fixture(object_path, object, object_size);
    free(object);
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {object_path};
    assert(link_test_elf_amd64_inputs(output_path, inputs, 1U, &capture) ==
           LD_OK);
    assert(capture.count == 0U);
    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    assert(!test_elf_find_eh_cie(image, image_size, NULL));
    free(image);
    unlink(object_path);
    unlink(output_path);

    /* In the DWARF64 initial-length form the CIE pointer is still a u32.  A
       live FDE proves that both owner discovery and output pointer rewriting
       advance only four bytes after that pointer. */
    fixture_options = (test_elf_eh_frame_options_t) {
            .dwarf64 = true,
            .owner_relocation = true,
    };
    object = make_test_elf_eh_frame_object(&fixture_options, &object_size);
    char dwarf64_path[] = "/tmp/nature-ld-eh-dwarf64-XXXXXX";
    write_fixture(dwarf64_path, object, object_size);
    free(object);
    memset(&capture, 0, sizeof(capture));
    inputs[0] = dwarf64_path;
    assert(link_test_elf_amd64_inputs(output_path, inputs, 1U, &capture) ==
           LD_OK);
    assert(capture.count == 0U);
    image = read_test_fixture(output_path, &image_size, NULL);
    size_t cie_offset;
    assert(test_elf_find_dwarf64_eh_cie(image, image_size, &cie_offset));
    assert(cie_offset + 60U <= image_size);
    assert(test_elf_read_u32(image + cie_offset + 24U) == UINT32_MAX);
    assert(test_elf_read_u64(image + cie_offset + 28U) == 20U);
    assert(test_elf_read_u32(image + cie_offset + 36U) == 36U);
    assert(test_elf_read_u32(image + cie_offset + 56U) == 0U);

    const uint8_t *eh_frame_header =
            test_elf_find_output_section(image, image_size, ".eh_frame");
    const uint8_t *eh_frame_hdr_header =
            test_elf_find_output_section(image, image_size,
                                         ".eh_frame_hdr");
    assert(eh_frame_header != NULL && eh_frame_hdr_header != NULL);
    assert(test_elf_read_u32(eh_frame_header + 4U) ==
           LD_ELF_SHT_X86_64_UNWIND);
    assert(test_elf_read_u64(eh_frame_header + 8U) == LD_ELF_SHF_ALLOC);
    assert(test_elf_read_u32(eh_frame_hdr_header + 4U) ==
           LD_ELF_SHT_PROGBITS);
    assert(test_elf_read_u64(eh_frame_hdr_header + 8U) == LD_ELF_SHF_ALLOC);
    assert(test_elf_read_u64(eh_frame_hdr_header + 48U) == 4U);
    uint64_t eh_frame_address =
            test_elf_read_u64(eh_frame_header + 16U);
    uint64_t eh_frame_file_offset =
            test_elf_read_u64(eh_frame_header + 24U);
    uint64_t header_address =
            test_elf_read_u64(eh_frame_hdr_header + 16U);
    uint64_t header_file_offset =
            test_elf_read_u64(eh_frame_hdr_header + 24U);
    uint64_t header_size = test_elf_read_u64(eh_frame_hdr_header + 32U);
    assert(eh_frame_file_offset == cie_offset);
    assert(header_size == 20U);
    assert(header_file_offset <= image_size &&
           header_size <= image_size - header_file_offset);
    const uint8_t *header = image + (size_t) header_file_offset;
    assert(header[0] == 1U && header[1] == 0x1bU &&
           header[2] == 0x03U && header[3] == 0x3bU);
    assert(test_elf_add_signed_u32(
                   header_address + 4U,
                   test_elf_read_u32(header + 4U)) == eh_frame_address);
    assert(test_elf_read_u32(header + 8U) == 1U);
    assert(test_elf_add_signed_u32(
                   header_address,
                   test_elf_read_u32(header + 12U)) ==
           test_elf_read_u64(image + 24U));
    assert(test_elf_add_signed_u32(
                   header_address,
                   test_elf_read_u32(header + 16U)) ==
           eh_frame_address + 24U);

    uint64_t program_offset = test_elf_read_u64(image + 32U);
    uint16_t program_count = test_elf_read_u16(image + 56U);
    assert(program_count == 5U);
    assert(program_offset <= image_size &&
           (uint64_t) program_count * LD_ELF64_PHDR_SIZE <=
                   image_size - program_offset);
    size_t gnu_eh_frame_count = 0U;
    for (uint16_t i = 0; i < program_count; i++) {
        const uint8_t *program =
                image + (size_t) program_offset +
                (size_t) i * LD_ELF64_PHDR_SIZE;
        if (test_elf_read_u32(program) != LD_ELF_PT_GNU_EH_FRAME) continue;
        gnu_eh_frame_count++;
        assert(test_elf_read_u32(program + 4U) == LD_ELF_PF_R);
        assert(test_elf_read_u64(program + 8U) == header_file_offset);
        assert(test_elf_read_u64(program + 16U) == header_address);
        assert(test_elf_read_u64(program + 32U) == header_size);
        assert(test_elf_read_u64(program + 40U) == header_size);
        assert(test_elf_read_u64(program + 48U) == 4U);
    }
    assert(gnu_eh_frame_count == 1U);
    free(image);
    unlink(dwarf64_path);
    unlink(output_path);

    fixture_options = (test_elf_eh_frame_options_t) {
            .cie_pointer_into_record = true,
    };
    test_elf_expect_eh_frame_failure(&fixture_options, LD_INVALID_INPUT,
                                     "has no matching CIE");

    fixture_options = (test_elf_eh_frame_options_t) {
            .nonzero_after_terminator = true,
    };
    test_elf_expect_eh_frame_failure(
            &fixture_options, LD_INVALID_INPUT,
            "non-zero bytes follow the .eh_frame terminator");

    fixture_options = (test_elf_eh_frame_options_t) {
            .terminator_relocation = true,
    };
    test_elf_expect_eh_frame_failure(
            &fixture_options, LD_INVALID_INPUT,
            "terminator, padding, or outside a record");

    fixture_options = (test_elf_eh_frame_options_t) {
            .owner_relocation = true,
            .cross_record_relocation = true,
    };
    test_elf_expect_eh_frame_failure(&fixture_options, LD_RELOCATION_ERROR,
                                     "cross-record");
}

static void test_elf_comdat_eh_frame_compaction(void) {
    size_t winner_size, loser_size, provider_size;
    uint8_t *winner = make_test_elf_eh_comdat_winner(&winner_size);
    uint8_t *loser = make_test_elf_eh_comdat_loser(&loser_size);
    uint8_t *provider = make_test_elf_eh_archive_provider(&provider_size);

    char winner_path[] = "/tmp/nature-ld-eh-comdat-winner-XXXXXX";
    char loser_path[] = "/tmp/nature-ld-eh-comdat-loser-XXXXXX";
    write_fixture(winner_path, winner, winner_size);
    write_fixture(loser_path, loser, loser_size);
    free(winner);
    free(loser);

    size_t archive_capacity = LD_ELF_AR_MAGIC_SIZE +
                              LD_ELF_AR_HEADER_SIZE + provider_size + 1U;
    uint8_t *archive = calloc(1, archive_capacity);
    assert(archive != NULL);
    memcpy(archive, LD_ELF_AR_MAGIC, LD_ELF_AR_MAGIC_SIZE);
    size_t archive_size = LD_ELF_AR_MAGIC_SIZE;
    test_archive_append(archive, archive_capacity, &archive_size,
                        "dead-provider.o/", provider, provider_size);
    free(provider);
    char archive_path[] = "/tmp/nature-ld-eh-comdat-provider-XXXXXX";
    write_fixture(archive_path, archive, archive_size);
    free(archive);

    static const char *output_path = "/tmp/nature-ld-eh-comdat-output";
    diagnostic_capture_t capture = {0};
    const char *inputs[] = {winner_path, loser_path, archive_path};
    int link_status =
            link_test_elf_amd64_inputs(output_path, inputs, 3U, &capture);
    if (link_status != LD_OK)
        fprintf(stderr, "COMDAT .eh_frame link failed (%d): %s\n", link_status,
                capture.message);
    assert(link_status == LD_OK);
    assert(capture.count == 0U);

    /* The duplicate COMDAT's dead text and its archive-only provider must not
       survive the link, while the loser's non-COMDAT live text must remain. */
    assert(!test_elf_file_contains_u32(output_path, 0xd3ad0badU));
    assert(test_elf_file_contains_u32(output_path, 0x11a7e5edU));
    assert(!test_elf_file_contains_u32(output_path, 0xbadc0ffeU));

    size_t image_size;
    uint8_t *image = read_test_fixture(output_path, &image_size, NULL);
    size_t cie_offset;
    assert(test_elf_find_eh_cie(image, image_size, &cie_offset));
    assert(cie_offset + 36U <= image_size);
    /* CIE + live FDE only; the original dead FDE was removed. */
    assert(test_elf_read_u32(image + cie_offset + 16U) == 12U);
    assert(test_elf_read_u32(image + cie_offset + 20U) == 20U);
    assert(test_elf_read_u32(image + cie_offset + 36U) == 0U);
    free(image);

    unlink(winner_path);
    unlink(loser_path);
    unlink(archive_path);
    unlink(output_path);
}


void test_ld_elf_unwind(void) {
    test_elf_cross_object_cie_deduplication();
    test_elf_eh_frame_boundaries();
    test_elf_comdat_eh_frame_compaction();
}
