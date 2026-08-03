#ifndef NATURE_LD_ELF_FORMAT_H
#define NATURE_LD_ELF_FORMAT_H

/*
 * ELF and archive wire-format definitions used by Nature's portable linker.
 * The parser and data model are informed by Zig's ELF linker, in particular
 * src/link/Elf/Object.zig and src/link/Elf/Archive.zig at commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. Zig is distributed under the MIT
 * license; the complete license text is kept in src/ld/ZIG-LICENSE.txt.
 *
 * This file intentionally does not include the host's <elf.h> or <ar.h> so a
 * non-Linux host can parse and emit Linux objects with identical definitions.
 */

#include <stddef.h>
#include <stdint.h>

#define LD_ELF_IDENT_SIZE 16U
#define LD_ELF_MAGIC_0 0x7fU
#define LD_ELF_MAGIC_1 'E'
#define LD_ELF_MAGIC_2 'L'
#define LD_ELF_MAGIC_3 'F'

#define LD_ELF_EI_CLASS 4U
#define LD_ELF_EI_DATA 5U
#define LD_ELF_EI_VERSION 6U
#define LD_ELF_EI_OSABI 7U
#define LD_ELF_CLASS_64 2U
#define LD_ELF_DATA_LSB 1U
#define LD_ELF_VERSION_CURRENT 1U

#define LD_ELF_ET_NONE 0U
#define LD_ELF_ET_REL 1U
#define LD_ELF_ET_EXEC 2U
#define LD_ELF_ET_DYN 3U

#define LD_ELF_PT_NULL 0U
#define LD_ELF_PT_LOAD 1U
#define LD_ELF_PT_DYNAMIC 2U
#define LD_ELF_PT_INTERP 3U
#define LD_ELF_PT_NOTE 4U
#define LD_ELF_PT_PHDR 6U
#define LD_ELF_PT_TLS 7U
#define LD_ELF_PT_GNU_EH_FRAME 0x6474e550U
#define LD_ELF_PT_GNU_STACK 0x6474e551U
#define LD_ELF_PT_GNU_RELRO 0x6474e552U
#define LD_ELF_PT_GNU_PROPERTY 0x6474e553U

#define LD_ELF_DT_NULL 0U
#define LD_ELF_DT_NEEDED 1U
#define LD_ELF_DT_HASH 4U
#define LD_ELF_DT_STRTAB 5U
#define LD_ELF_DT_SYMTAB 6U
#define LD_ELF_DT_RELA 7U
#define LD_ELF_DT_RELASZ 8U
#define LD_ELF_DT_RELAENT 9U
#define LD_ELF_DT_STRSZ 10U
#define LD_ELF_DT_SYMENT 11U
#define LD_ELF_DT_INIT 12U
#define LD_ELF_DT_FINI 13U
#define LD_ELF_DT_DEBUG 21U
#define LD_ELF_DT_INIT_ARRAY 25U
#define LD_ELF_DT_FINI_ARRAY 26U
#define LD_ELF_DT_INIT_ARRAYSZ 27U
#define LD_ELF_DT_FINI_ARRAYSZ 28U
#define LD_ELF_DT_FLAGS 30U
#define LD_ELF_DT_FLAGS_1 0x6ffffffbU
#define LD_ELF_DT_GNU_HASH 0x6ffffef5U

#define LD_ELF_DF_STATIC_TLS 0x10ULL
#define LD_ELF_DF_1_PIE 0x08000000ULL

#define LD_ELF_PF_X 0x1U
#define LD_ELF_PF_W 0x2U
#define LD_ELF_PF_R 0x4U

#define LD_ELF_EM_X86_64 62U
#define LD_ELF_EM_AARCH64 183U
#define LD_ELF_EM_RISCV 243U

#define LD_ELF_SHT_NULL 0U
#define LD_ELF_SHT_PROGBITS 1U
#define LD_ELF_SHT_SYMTAB 2U
#define LD_ELF_SHT_STRTAB 3U
#define LD_ELF_SHT_RELA 4U
#define LD_ELF_SHT_HASH 5U
#define LD_ELF_SHT_DYNAMIC 6U
#define LD_ELF_SHT_NOTE 7U
#define LD_ELF_SHT_NOBITS 8U
#define LD_ELF_SHT_REL 9U
#define LD_ELF_SHT_SHLIB 10U
#define LD_ELF_SHT_DYNSYM 11U
#define LD_ELF_SHT_INIT_ARRAY 14U
#define LD_ELF_SHT_FINI_ARRAY 15U
#define LD_ELF_SHT_PREINIT_ARRAY 16U
#define LD_ELF_SHT_GROUP 17U
#define LD_ELF_SHT_SYMTAB_SHNDX 18U
#define LD_ELF_SHT_X86_64_UNWIND 0x70000001U
#define LD_ELF_SHT_GNU_ATTRIBUTES 0x6ffffff5U
#define LD_ELF_SHT_GNU_HASH 0x6ffffff6U
#define LD_ELF_SHT_GNU_VERDEF 0x6ffffffdU
#define LD_ELF_SHT_GNU_VERNEED 0x6ffffffeU
#define LD_ELF_SHT_GNU_VERSYM 0x6fffffffU

#define LD_ELF_SHF_WRITE 0x1ULL
#define LD_ELF_SHF_ALLOC 0x2ULL
#define LD_ELF_SHF_EXECINSTR 0x4ULL
#define LD_ELF_SHF_MERGE 0x10ULL
#define LD_ELF_SHF_STRINGS 0x20ULL
#define LD_ELF_SHF_INFO_LINK 0x40ULL
#define LD_ELF_SHF_LINK_ORDER 0x80ULL
#define LD_ELF_SHF_OS_NONCONFORMING 0x100ULL
#define LD_ELF_SHF_GROUP 0x200ULL
#define LD_ELF_SHF_TLS 0x400ULL

#define LD_ELF_NT_GNU_PROPERTY_TYPE_0 5U
#define LD_ELF_GNU_PROPERTY_STACK_SIZE 1U
#define LD_ELF_GNU_PROPERTY_NO_COPY_ON_PROTECTED 2U
#define LD_ELF_GNU_PROPERTY_UINT32_AND_LO 0xb0000000U
#define LD_ELF_GNU_PROPERTY_UINT32_AND_HI 0xb0007fffU
#define LD_ELF_GNU_PROPERTY_UINT32_OR_LO 0xb0008000U
#define LD_ELF_GNU_PROPERTY_UINT32_OR_HI 0xb000ffffU
#define LD_ELF_GNU_PROPERTY_1_NEEDED LD_ELF_GNU_PROPERTY_UINT32_OR_LO
#define LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND 0xc0000000U
#define LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND 0xc0000002U
#define LD_ELF_GNU_PROPERTY_X86_FEATURE_2_NEEDED 0xc0008001U
#define LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED 0xc0008002U
#define LD_ELF_GNU_PROPERTY_X86_FEATURE_2_USED 0xc0010001U
#define LD_ELF_GNU_PROPERTY_X86_ISA_1_USED 0xc0010002U
#define LD_ELF_GNU_PROPERTY_X86_FEATURE_1_IBT (1U << 0U)
#define LD_ELF_GNU_PROPERTY_X86_FEATURE_1_SHSTK (1U << 1U)
#define LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_BTI (1U << 0U)
#define LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_PAC (1U << 1U)
#define LD_ELF_SHF_COMPRESSED 0x800ULL
#define LD_ELF_SHF_EXCLUDE 0x80000000ULL

#define LD_ELF_COMPRESS_ZLIB 1U

#define LD_ELF_GRP_COMDAT 0x1U

#define LD_ELF_SHN_UNDEF 0U
#define LD_ELF_SHN_LORESERVE 0xff00U
#define LD_ELF_SHN_LOPROC 0xff00U
#define LD_ELF_SHN_HIPROC 0xff1fU
#define LD_ELF_SHN_LOOS 0xff20U
#define LD_ELF_SHN_HIOS 0xff3fU
#define LD_ELF_SHN_ABS 0xfff1U
#define LD_ELF_SHN_COMMON 0xfff2U
#define LD_ELF_SHN_XINDEX 0xffffU

#define LD_ELF_STB_LOCAL 0U
#define LD_ELF_STB_GLOBAL 1U
#define LD_ELF_STB_WEAK 2U
#define LD_ELF_STB_GNU_UNIQUE 10U

#define LD_ELF_STT_NOTYPE 0U
#define LD_ELF_STT_OBJECT 1U
#define LD_ELF_STT_FUNC 2U
#define LD_ELF_STT_SECTION 3U
#define LD_ELF_STT_FILE 4U
#define LD_ELF_STT_COMMON 5U
#define LD_ELF_STT_TLS 6U
#define LD_ELF_STT_GNU_IFUNC 10U

#define LD_ELF_STV_DEFAULT 0U
#define LD_ELF_STV_INTERNAL 1U
#define LD_ELF_STV_HIDDEN 2U
#define LD_ELF_STV_PROTECTED 3U

#define LD_ELF_R_AARCH64_NONE 0U
#define LD_ELF_R_AARCH64_ABS64 257U
#define LD_ELF_R_AARCH64_ABS32 258U
#define LD_ELF_R_AARCH64_ABS16 259U
#define LD_ELF_R_AARCH64_PREL64 260U
#define LD_ELF_R_AARCH64_PREL32 261U
#define LD_ELF_R_AARCH64_PREL16 262U
#define LD_ELF_R_AARCH64_MOVW_UABS_G0 263U
#define LD_ELF_R_AARCH64_MOVW_UABS_G0_NC 264U
#define LD_ELF_R_AARCH64_MOVW_UABS_G1 265U
#define LD_ELF_R_AARCH64_MOVW_UABS_G1_NC 266U
#define LD_ELF_R_AARCH64_MOVW_UABS_G2 267U
#define LD_ELF_R_AARCH64_MOVW_UABS_G2_NC 268U
#define LD_ELF_R_AARCH64_MOVW_UABS_G3 269U
#define LD_ELF_R_AARCH64_LD_PREL_LO19 273U
#define LD_ELF_R_AARCH64_ADR_PREL_LO21 274U
#define LD_ELF_R_AARCH64_ADR_PREL_PG_HI21 275U
#define LD_ELF_R_AARCH64_ADD_ABS_LO12_NC 277U
#define LD_ELF_R_AARCH64_LDST8_ABS_LO12_NC 278U
#define LD_ELF_R_AARCH64_TSTBR14 279U
#define LD_ELF_R_AARCH64_CONDBR19 280U
#define LD_ELF_R_AARCH64_JUMP26 282U
#define LD_ELF_R_AARCH64_CALL26 283U
#define LD_ELF_R_AARCH64_LDST16_ABS_LO12_NC 284U
#define LD_ELF_R_AARCH64_LDST32_ABS_LO12_NC 285U
#define LD_ELF_R_AARCH64_LDST64_ABS_LO12_NC 286U
#define LD_ELF_R_AARCH64_LDST128_ABS_LO12_NC 299U
#define LD_ELF_R_AARCH64_GOT_LD_PREL19 309U
#define LD_ELF_R_AARCH64_ADR_GOT_PAGE 311U
#define LD_ELF_R_AARCH64_LD64_GOT_LO12_NC 312U
#define LD_ELF_R_AARCH64_LD64_GOTPAGE_LO15 313U
#define LD_ELF_R_AARCH64_TLSGD_ADR_PAGE21 513U
#define LD_ELF_R_AARCH64_TLSGD_ADD_LO12_NC 514U
#define LD_ELF_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 541U
#define LD_ELF_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC 542U
#define LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_HI12 549U
#define LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12 550U
#define LD_ELF_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC 551U
#define LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12 552U
#define LD_ELF_R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC 553U
#define LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12 554U
#define LD_ELF_R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC 555U
#define LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12 556U
#define LD_ELF_R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC 557U
#define LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12 558U
#define LD_ELF_R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC 559U
#define LD_ELF_R_AARCH64_TLSDESC_ADR_PAGE21 562U
#define LD_ELF_R_AARCH64_TLSDESC_LD64_LO12 563U
#define LD_ELF_R_AARCH64_TLSDESC_ADD_LO12 564U
#define LD_ELF_R_AARCH64_TLSDESC_CALL 569U
#define LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12 570U
#define LD_ELF_R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC 571U
#define LD_ELF_R_AARCH64_IRELATIVE 1032U
#define LD_ELF_R_AARCH64_RELATIVE 1027U

#define LD_ELF_R_X86_64_NONE 0U
#define LD_ELF_R_X86_64_64 1U
#define LD_ELF_R_X86_64_PC32 2U
#define LD_ELF_R_X86_64_GOT32 3U
#define LD_ELF_R_X86_64_PLT32 4U
#define LD_ELF_R_X86_64_GOTPCREL 9U
#define LD_ELF_R_X86_64_32 10U
#define LD_ELF_R_X86_64_32S 11U
#define LD_ELF_R_X86_64_16 12U
#define LD_ELF_R_X86_64_PC16 13U
#define LD_ELF_R_X86_64_8 14U
#define LD_ELF_R_X86_64_PC8 15U
#define LD_ELF_R_X86_64_DTPOFF64 17U
#define LD_ELF_R_X86_64_TPOFF64 18U
#define LD_ELF_R_X86_64_TLSGD 19U
#define LD_ELF_R_X86_64_TLSLD 20U
#define LD_ELF_R_X86_64_DTPOFF32 21U
#define LD_ELF_R_X86_64_GOTTPOFF 22U
#define LD_ELF_R_X86_64_TPOFF32 23U
#define LD_ELF_R_X86_64_PC64 24U
#define LD_ELF_R_X86_64_GOTOFF64 25U
#define LD_ELF_R_X86_64_GOTPC32 26U
#define LD_ELF_R_X86_64_GOT64 27U
#define LD_ELF_R_X86_64_GOTPCREL64 28U
#define LD_ELF_R_X86_64_GOTPC64 29U
#define LD_ELF_R_X86_64_GOTPLT64 30U
#define LD_ELF_R_X86_64_PLTOFF64 31U
#define LD_ELF_R_X86_64_SIZE32 32U
#define LD_ELF_R_X86_64_SIZE64 33U
#define LD_ELF_R_X86_64_GOTPC32_TLSDESC 34U
#define LD_ELF_R_X86_64_TLSDESC_CALL 35U
#define LD_ELF_R_X86_64_TLSDESC 36U
#define LD_ELF_R_X86_64_IRELATIVE 37U
#define LD_ELF_R_X86_64_RELATIVE 8U
#define LD_ELF_R_X86_64_GOTPCRELX 41U
#define LD_ELF_R_X86_64_REX_GOTPCRELX 42U

#define LD_ELF_R_RISCV_NONE 0U
#define LD_ELF_R_RISCV_32 1U
#define LD_ELF_R_RISCV_64 2U
#define LD_ELF_R_RISCV_RELATIVE 3U
#define LD_ELF_R_RISCV_COPY 4U
#define LD_ELF_R_RISCV_JUMP_SLOT 5U
#define LD_ELF_R_RISCV_TLS_DTPMOD32 6U
#define LD_ELF_R_RISCV_TLS_DTPMOD64 7U
#define LD_ELF_R_RISCV_TLS_DTPREL32 8U
#define LD_ELF_R_RISCV_TLS_DTPREL64 9U
#define LD_ELF_R_RISCV_TLS_TPREL32 10U
#define LD_ELF_R_RISCV_TLS_TPREL64 11U
#define LD_ELF_R_RISCV_TLSDESC 12U
#define LD_ELF_R_RISCV_BRANCH 16U
#define LD_ELF_R_RISCV_JAL 17U
#define LD_ELF_R_RISCV_CALL 18U
#define LD_ELF_R_RISCV_CALL_PLT 19U
#define LD_ELF_R_RISCV_GOT_HI20 20U
#define LD_ELF_R_RISCV_TLS_GOT_HI20 21U
#define LD_ELF_R_RISCV_TLS_GD_HI20 22U
#define LD_ELF_R_RISCV_PCREL_HI20 23U
#define LD_ELF_R_RISCV_PCREL_LO12_I 24U
#define LD_ELF_R_RISCV_PCREL_LO12_S 25U
#define LD_ELF_R_RISCV_HI20 26U
#define LD_ELF_R_RISCV_LO12_I 27U
#define LD_ELF_R_RISCV_LO12_S 28U
#define LD_ELF_R_RISCV_TPREL_HI20 29U
#define LD_ELF_R_RISCV_TPREL_LO12_I 30U
#define LD_ELF_R_RISCV_TPREL_LO12_S 31U
#define LD_ELF_R_RISCV_TPREL_ADD 32U
#define LD_ELF_R_RISCV_ADD8 33U
#define LD_ELF_R_RISCV_ADD16 34U
#define LD_ELF_R_RISCV_ADD32 35U
#define LD_ELF_R_RISCV_ADD64 36U
#define LD_ELF_R_RISCV_SUB8 37U
#define LD_ELF_R_RISCV_SUB16 38U
#define LD_ELF_R_RISCV_SUB32 39U
#define LD_ELF_R_RISCV_SUB64 40U
#define LD_ELF_R_RISCV_ALIGN 43U
#define LD_ELF_R_RISCV_RVC_BRANCH 44U
#define LD_ELF_R_RISCV_RVC_JUMP 45U
#define LD_ELF_R_RISCV_RELAX 51U
#define LD_ELF_R_RISCV_SUB6 52U
#define LD_ELF_R_RISCV_SET6 53U
#define LD_ELF_R_RISCV_SET8 54U
#define LD_ELF_R_RISCV_SET16 55U
#define LD_ELF_R_RISCV_SET32 56U
#define LD_ELF_R_RISCV_32_PCREL 57U
#define LD_ELF_R_RISCV_IRELATIVE 58U
#define LD_ELF_R_RISCV_PLT32 59U
#define LD_ELF_R_RISCV_SET_ULEB128 60U
#define LD_ELF_R_RISCV_SUB_ULEB128 61U

/* RISC-V psABI bias between the DTV address and the TLS block start. */
#define LD_ELF_RISCV_DTP_OFFSET 0x800U

#define LD_ELF_SYM_BIND(info) ((uint8_t) ((info) >> 4U))
#define LD_ELF_SYM_TYPE(info) ((uint8_t) ((info) & 0x0fU))
#define LD_ELF_SYM_INFO(bind, type) \
    ((uint8_t) ((((uint32_t) (bind)) << 4U) | (((uint32_t) (type)) & 0x0fU)))
#define LD_ELF_SYM_VISIBILITY(other) ((uint8_t) ((other) & 0x03U))
#define LD_ELF_RELA_SYMBOL(info) ((uint32_t) ((info) >> 32U))
#define LD_ELF_RELA_TYPE(info) ((uint32_t) (info))
#define LD_ELF_RELA_INFO(symbol, type) (((uint64_t) (symbol) << 32U) | (uint32_t) (type))

#define LD_ELF64_EHDR_SIZE 64U
#define LD_ELF64_PHDR_SIZE 56U
#define LD_ELF64_SHDR_SIZE 64U
#define LD_ELF64_SYM_SIZE 24U
#define LD_ELF64_REL_SIZE 16U
#define LD_ELF64_RELA_SIZE 24U
#define LD_ELF64_CHDR_SIZE 24U
#define LD_ELF64_DYN_SIZE 16U

#define LD_ELF_AR_MAGIC "!<arch>\n"
#define LD_ELF_AR_THIN_MAGIC "!<thin>\n"
#define LD_ELF_AR_MAGIC_SIZE 8U
#define LD_ELF_AR_HEADER_SIZE 60U
#define LD_ELF_AR_NAME_SIZE 16U

typedef struct {
    uint8_t e_ident[LD_ELF_IDENT_SIZE];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} ld_elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} ld_elf64_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} ld_elf64_shdr_t;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} ld_elf64_sym_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} ld_elf64_rela_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
} ld_elf64_rel_t;

typedef struct {
    uint32_t ch_type;
    uint32_t ch_reserved;
    uint64_t ch_size;
    uint64_t ch_addralign;
} ld_elf64_chdr_t;

typedef struct {
    int64_t d_tag;
    uint64_t d_value;
} ld_elf64_dyn_t;

typedef struct {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
} ld_elf_ar_header_t;

_Static_assert(sizeof(ld_elf64_ehdr_t) == LD_ELF64_EHDR_SIZE, "ELF64 Ehdr layout");
_Static_assert(sizeof(ld_elf64_phdr_t) == LD_ELF64_PHDR_SIZE, "ELF64 Phdr layout");
_Static_assert(sizeof(ld_elf64_shdr_t) == LD_ELF64_SHDR_SIZE, "ELF64 Shdr layout");
_Static_assert(sizeof(ld_elf64_sym_t) == LD_ELF64_SYM_SIZE, "ELF64 Sym layout");
_Static_assert(sizeof(ld_elf64_rel_t) == LD_ELF64_REL_SIZE, "ELF64 Rel layout");
_Static_assert(sizeof(ld_elf64_rela_t) == LD_ELF64_RELA_SIZE, "ELF64 Rela layout");
_Static_assert(sizeof(ld_elf64_chdr_t) == LD_ELF64_CHDR_SIZE, "ELF64 Chdr layout");
_Static_assert(sizeof(ld_elf64_dyn_t) == LD_ELF64_DYN_SIZE, "ELF64 Dyn layout");
_Static_assert(sizeof(ld_elf_ar_header_t) == LD_ELF_AR_HEADER_SIZE, "archive header layout");

#endif
