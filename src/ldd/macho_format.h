#ifndef NATURE_LDD_MACHO_FORMAT_H
#define NATURE_LDD_MACHO_FORMAT_H

#include <stdint.h>

#define LDD_MH_MAGIC_64 0xfeedfacfU
#define LDD_FAT_MAGIC 0xcafebabeU
#define LDD_FAT_MAGIC_64 0xcafebabfU
#define LDD_CPU_ARCH_ABI64 0x01000000
#define LDD_CPU_TYPE_ARM64 (LDD_CPU_ARCH_ABI64 | 12)
#define LDD_CPU_SUBTYPE_ARM64_ALL 0
#define LDD_CPU_SUBTYPE_ARM64E 2

#define LDD_MH_OBJECT 0x1U
#define LDD_MH_EXECUTE 0x2U
#define LDD_MH_DYLIB 0x6U
#define LDD_MH_NOUNDEFS 0x1U
#define LDD_MH_DYLDLINK 0x4U
#define LDD_MH_TWOLEVEL 0x80U
#define LDD_MH_WEAK_DEFINES 0x8000U
#define LDD_MH_BINDS_TO_WEAK 0x10000U
#define LDD_MH_NO_REEXPORTED_DYLIBS 0x100000U
#define LDD_MH_PIE 0x200000U
#define LDD_MH_HAS_TLV_DESCRIPTORS 0x800000U

#define LDD_LC_REQ_DYLD 0x80000000U
#define LDD_LC_SEGMENT_64 0x19U
#define LDD_LC_SYMTAB 0x2U
#define LDD_LC_DYSYMTAB 0xbU
#define LDD_LC_LOAD_DYLIB 0xcU
#define LDD_LC_ID_DYLIB 0xdU
#define LDD_LC_LOAD_DYLINKER 0xeU
#define LDD_LC_UUID 0x1bU
#define LDD_LC_CODE_SIGNATURE 0x1dU
#define LDD_LC_REEXPORT_DYLIB (0x1fU | LDD_LC_REQ_DYLD)
#define LDD_LC_DYLD_INFO_ONLY (0x22U | LDD_LC_REQ_DYLD)
#define LDD_LC_FUNCTION_STARTS 0x26U
#define LDD_LC_MAIN (0x28U | LDD_LC_REQ_DYLD)
#define LDD_LC_DATA_IN_CODE 0x29U
#define LDD_LC_SOURCE_VERSION 0x2aU
#define LDD_LC_BUILD_VERSION 0x32U
#define LDD_LC_DYLD_EXPORTS_TRIE (0x33U | LDD_LC_REQ_DYLD)

#define LDD_PLATFORM_MACOS 1U
#define LDD_VM_PROT_READ 1
#define LDD_VM_PROT_WRITE 2
#define LDD_VM_PROT_EXECUTE 4

#define LDD_SECTION_TYPE 0x000000ffU
#define LDD_S_REGULAR 0x0U
#define LDD_S_ZEROFILL 0x1U
#define LDD_S_CSTRING_LITERALS 0x2U
#define LDD_S_4BYTE_LITERALS 0x3U
#define LDD_S_8BYTE_LITERALS 0x4U
#define LDD_S_NON_LAZY_SYMBOL_POINTERS 0x6U
#define LDD_S_LAZY_SYMBOL_POINTERS 0x7U
#define LDD_S_SYMBOL_STUBS 0x8U
#define LDD_S_MOD_INIT_FUNC_POINTERS 0x9U
#define LDD_S_GB_ZEROFILL 0xcU
#define LDD_S_16BYTE_LITERALS 0xeU
#define LDD_S_THREAD_LOCAL_REGULAR 0x11U
#define LDD_S_THREAD_LOCAL_ZEROFILL 0x12U
#define LDD_S_THREAD_LOCAL_VARIABLES 0x13U
#define LDD_S_THREAD_LOCAL_VARIABLE_POINTERS 0x14U
#define LDD_S_THREAD_LOCAL_INIT_FUNCTION_POINTERS 0x15U
#define LDD_S_ATTR_PURE_INSTRUCTIONS 0x80000000U
#define LDD_S_ATTR_DEBUG 0x02000000U
#define LDD_S_ATTR_SOME_INSTRUCTIONS 0x00000400U

#define LDD_N_STAB 0xe0U
#define LDD_N_PEXT 0x10U
#define LDD_N_TYPE 0x0eU
#define LDD_N_EXT 0x01U
#define LDD_N_UNDF 0x0U
#define LDD_N_ABS 0x2U
#define LDD_N_INDR 0xaU
#define LDD_N_SECT 0xeU
#define LDD_N_REFERENCED_DYNAMICALLY 0x0010U
#define LDD_N_WEAK_REF 0x0040U
#define LDD_N_WEAK_DEF 0x0080U

#define LDD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00U
#define LDD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL 0x01U
#define LDD_EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE 0x02U
#define LDD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04U

#define LDD_ARM64_RELOC_UNSIGNED 0U
#define LDD_ARM64_RELOC_SUBTRACTOR 1U
#define LDD_ARM64_RELOC_BRANCH26 2U
#define LDD_ARM64_RELOC_PAGE21 3U
#define LDD_ARM64_RELOC_PAGEOFF12 4U
#define LDD_ARM64_RELOC_GOT_LOAD_PAGE21 5U
#define LDD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 6U
#define LDD_ARM64_RELOC_POINTER_TO_GOT 7U
#define LDD_ARM64_RELOC_TLVP_LOAD_PAGE21 8U
#define LDD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12 9U
#define LDD_ARM64_RELOC_ADDEND 10U

#define LDD_EXPORT_SYMBOL_FLAGS_KIND_MASK 0x03U
#define LDD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00U
#define LDD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL 0x01U
#define LDD_EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE 0x02U
#define LDD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04U
#define LDD_EXPORT_SYMBOL_FLAGS_REEXPORT 0x08U
#define LDD_EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER 0x10U

#define LDD_REBASE_TYPE_POINTER 1U
#define LDD_REBASE_OPCODE_DONE 0x00U
#define LDD_REBASE_OPCODE_SET_TYPE_IMM 0x10U
#define LDD_REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x20U
#define LDD_REBASE_OPCODE_DO_REBASE_IMM_TIMES 0x50U

#define LDD_BIND_TYPE_POINTER 1U
#define LDD_BIND_OPCODE_DONE 0x00U
#define LDD_BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10U
#define LDD_BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB 0x20U
#define LDD_BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40U
#define LDD_BIND_OPCODE_SET_TYPE_IMM 0x50U
#define LDD_BIND_OPCODE_SET_ADDEND_SLEB 0x60U
#define LDD_BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70U
#define LDD_BIND_OPCODE_DO_BIND 0x90U
#define LDD_BIND_SYMBOL_FLAGS_WEAK_IMPORT 0x1U

#define LDD_UNWIND_SECTION_VERSION 1U
#define LDD_UNWIND_SECOND_LEVEL_REGULAR 2U
#define LDD_UNWIND_HAS_LSDA 0x40000000U
#define LDD_UNWIND_PERSONALITY_MASK 0x30000000U
#define LDD_UNWIND_ARM64_MODE_MASK 0x0f000000U
#define LDD_UNWIND_ARM64_MODE_DWARF 0x03000000U

#define LDD_CSMAGIC_CODEDIRECTORY 0xfade0c02U
#define LDD_CSMAGIC_EMBEDDED_SIGNATURE 0xfade0cc0U
#define LDD_CS_SUPPORTSEXECSEG 0x20400U
#define LDD_CS_ADHOC 0x2U
#define LDD_CS_LINKER_SIGNED 0x20000U
#define LDD_CS_EXECSEG_MAIN_BINARY 0x1U

typedef struct {
    uint32_t magic;
    int32_t cputype;
    int32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} ldd_mach_header_64_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
} ldd_load_command_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t maxprot;
    int32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} ldd_segment_command_64_t;

typedef struct {
    char sectname[16];
    char segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} ldd_section_64_t;

typedef struct {
    uint32_t n_strx;
    uint8_t n_type;
    uint8_t n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} ldd_nlist_64_t;

/* Relocatable objects carry one of these records in __LD,__compact_unwind
   for each function.  Final images use the two-level __TEXT,__unwind_info
   representation below.  These definitions mirror Apple's public wire
   format and intentionally do not depend on <mach-o/...> headers. */
typedef struct {
    uint64_t range_start;
    uint32_t range_length;
    uint32_t compact_encoding;
    uint64_t personality_function;
    uint64_t lsda;
} ldd_compact_unwind_entry_t;

typedef struct {
    uint32_t version;
    uint32_t common_encodings_offset;
    uint32_t common_encodings_count;
    uint32_t personalities_offset;
    uint32_t personalities_count;
    uint32_t index_offset;
    uint32_t index_count;
} ldd_unwind_info_header_t;

typedef struct {
    uint32_t function_offset;
    uint32_t second_level_page_offset;
    uint32_t lsda_index_offset;
} ldd_unwind_info_index_entry_t;

typedef struct {
    uint32_t function_offset;
    uint32_t lsda_offset;
} ldd_unwind_info_lsda_entry_t;

typedef struct {
    uint32_t kind;
    uint16_t entry_page_offset;
    uint16_t entry_count;
} ldd_unwind_info_regular_page_header_t;

typedef struct {
    uint32_t function_offset;
    uint32_t encoding;
} ldd_unwind_info_regular_entry_t;

_Static_assert(sizeof(ldd_compact_unwind_entry_t) == 32U,
               "compact unwind entry wire size must be 32 bytes");
_Static_assert(sizeof(ldd_unwind_info_header_t) == 28U,
               "unwind info header wire size must be 28 bytes");
_Static_assert(sizeof(ldd_unwind_info_index_entry_t) == 12U,
               "unwind info index wire size must be 12 bytes");
_Static_assert(sizeof(ldd_unwind_info_lsda_entry_t) == 8U,
               "unwind LSDA entry wire size must be 8 bytes");
_Static_assert(sizeof(ldd_unwind_info_regular_page_header_t) == 8U,
               "regular unwind page header wire size must be 8 bytes");
_Static_assert(sizeof(ldd_unwind_info_regular_entry_t) == 8U,
               "regular unwind entry wire size must be 8 bytes");

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} ldd_symtab_command_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym, nlocalsym;
    uint32_t iextdefsym, nextdefsym;
    uint32_t iundefsym, nundefsym;
    uint32_t tocoff, ntoc;
    uint32_t modtaboff, nmodtab;
    uint32_t extrefsymoff, nextrefsyms;
    uint32_t indirectsymoff, nindirectsyms;
    uint32_t extreloff, nextrel;
    uint32_t locreloff, nlocrel;
} ldd_dysymtab_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t rebase_off, rebase_size;
    uint32_t bind_off, bind_size;
    uint32_t weak_bind_off, weak_bind_size;
    uint32_t lazy_bind_off, lazy_bind_size;
    uint32_t export_off, export_size;
} ldd_dyld_info_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t name_offset;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
} ldd_dylib_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t dataoff, datasize;
} ldd_linkedit_data_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint8_t uuid[16];
} ldd_uuid_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t platform, minos, sdk, ntools;
} ldd_build_version_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint64_t entryoff, stacksize;
} ldd_entry_point_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint64_t version;
} ldd_source_version_command_t;

typedef struct {
    uint32_t offset;
    uint16_t length;
    uint16_t kind;
} ldd_data_in_code_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t nfat_arch;
} ldd_fat_header_t;

typedef struct {
    int32_t cputype;
    int32_t cpusubtype;
    uint32_t offset;
    uint32_t size;
    uint32_t align;
} ldd_fat_arch_t;

typedef struct {
    int32_t cputype;
    int32_t cpusubtype;
    uint64_t offset;
    uint64_t size;
    uint32_t align;
    uint32_t reserved;
} ldd_fat_arch_64_t;

#endif
