#ifndef NATURE_LD_MACHO_FORMAT_H
#define NATURE_LD_MACHO_FORMAT_H

#include <stdint.h>

#define LD_MH_MAGIC_64 0xfeedfacfU
#define LD_FAT_MAGIC 0xcafebabeU
#define LD_FAT_MAGIC_64 0xcafebabfU
#define LD_CPU_ARCH_ABI64 0x01000000
#define LD_CPU_TYPE_ARM64 (LD_CPU_ARCH_ABI64 | 12)
#define LD_CPU_SUBTYPE_MASK 0xff000000U
#define LD_CPU_SUBTYPE_ARM64_ALL 0
#define LD_CPU_SUBTYPE_ARM64E 2

#define LD_MH_OBJECT 0x1U
#define LD_MH_EXECUTE 0x2U
#define LD_MH_DYLIB 0x6U
#define LD_MH_NOUNDEFS 0x1U
#define LD_MH_DYLDLINK 0x4U
#define LD_MH_TWOLEVEL 0x80U
#define LD_MH_WEAK_DEFINES 0x8000U
#define LD_MH_BINDS_TO_WEAK 0x10000U
#define LD_MH_NO_REEXPORTED_DYLIBS 0x100000U
#define LD_MH_PIE 0x200000U
#define LD_MH_HAS_TLV_DESCRIPTORS 0x800000U

#define LD_LC_REQ_DYLD 0x80000000U
#define LD_LC_SEGMENT_64 0x19U
#define LD_LC_SYMTAB 0x2U
#define LD_LC_DYSYMTAB 0xbU
#define LD_LC_LOAD_DYLIB 0xcU
#define LD_LC_ID_DYLIB 0xdU
#define LD_LC_LOAD_DYLINKER 0xeU
#define LD_LC_RPATH (0x1cU | LD_LC_REQ_DYLD)
#define LD_LC_UUID 0x1bU
#define LD_LC_CODE_SIGNATURE 0x1dU
#define LD_LC_VERSION_MIN_MACOSX 0x24U
#define LD_LC_VERSION_MIN_IPHONEOS 0x25U
#define LD_LC_VERSION_MIN_TVOS 0x2fU
#define LD_LC_VERSION_MIN_WATCHOS 0x30U
#define LD_LC_REEXPORT_DYLIB (0x1fU | LD_LC_REQ_DYLD)
#define LD_LC_DYLD_INFO_ONLY (0x22U | LD_LC_REQ_DYLD)
#define LD_LC_FUNCTION_STARTS 0x26U
#define LD_LC_MAIN (0x28U | LD_LC_REQ_DYLD)
#define LD_LC_DATA_IN_CODE 0x29U
#define LD_LC_SOURCE_VERSION 0x2aU
#define LD_LC_BUILD_VERSION 0x32U
#define LD_LC_DYLD_EXPORTS_TRIE (0x33U | LD_LC_REQ_DYLD)

#define LD_PLATFORM_MACOS 1U
#define LD_PLATFORM_IOS 2U
#define LD_PLATFORM_TVOS 3U
#define LD_PLATFORM_WATCHOS 4U
#define LD_PLATFORM_BRIDGEOS 5U
#define LD_PLATFORM_MACCATALYST 6U
#define LD_PLATFORM_IOSSIMULATOR 7U
#define LD_PLATFORM_TVOSSIMULATOR 8U
#define LD_PLATFORM_WATCHOSSIMULATOR 9U
#define LD_PLATFORM_DRIVERKIT 10U
#define LD_PLATFORM_VISIONOS 11U
#define LD_PLATFORM_VISIONOSSIMULATOR 12U
#define LD_VM_PROT_READ 1
#define LD_VM_PROT_WRITE 2
#define LD_VM_PROT_EXECUTE 4

#define LD_SECTION_TYPE 0x000000ffU
#define LD_S_REGULAR 0x0U
#define LD_S_ZEROFILL 0x1U
#define LD_S_CSTRING_LITERALS 0x2U
#define LD_S_4BYTE_LITERALS 0x3U
#define LD_S_8BYTE_LITERALS 0x4U
#define LD_S_NON_LAZY_SYMBOL_POINTERS 0x6U
#define LD_S_LAZY_SYMBOL_POINTERS 0x7U
#define LD_S_SYMBOL_STUBS 0x8U
#define LD_S_MOD_INIT_FUNC_POINTERS 0x9U
#define LD_S_GB_ZEROFILL 0xcU
#define LD_S_16BYTE_LITERALS 0xeU
#define LD_S_THREAD_LOCAL_REGULAR 0x11U
#define LD_S_THREAD_LOCAL_ZEROFILL 0x12U
#define LD_S_THREAD_LOCAL_VARIABLES 0x13U
#define LD_S_THREAD_LOCAL_VARIABLE_POINTERS 0x14U
#define LD_S_THREAD_LOCAL_INIT_FUNCTION_POINTERS 0x15U
#define LD_S_ATTR_PURE_INSTRUCTIONS 0x80000000U
#define LD_S_ATTR_DEBUG 0x02000000U
#define LD_S_ATTR_SOME_INSTRUCTIONS 0x00000400U

#define LD_N_STAB 0xe0U
#define LD_N_PEXT 0x10U
#define LD_N_TYPE 0x0eU
#define LD_N_EXT 0x01U
#define LD_N_UNDF 0x0U
#define LD_N_ABS 0x2U
#define LD_N_INDR 0xaU
#define LD_N_SECT 0xeU
#define LD_N_REFERENCED_DYNAMICALLY 0x0010U
#define LD_N_WEAK_REF 0x0040U
#define LD_N_WEAK_DEF 0x0080U
#define LD_N_REF_TO_WEAK 0x0080U

#define LD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00U
#define LD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL 0x01U
#define LD_EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE 0x02U
#define LD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04U

#define LD_ARM64_RELOC_UNSIGNED 0U
#define LD_ARM64_RELOC_SUBTRACTOR 1U
#define LD_ARM64_RELOC_BRANCH26 2U
#define LD_ARM64_RELOC_PAGE21 3U
#define LD_ARM64_RELOC_PAGEOFF12 4U
#define LD_ARM64_RELOC_GOT_LOAD_PAGE21 5U
#define LD_ARM64_RELOC_GOT_LOAD_PAGEOFF12 6U
#define LD_ARM64_RELOC_POINTER_TO_GOT 7U
#define LD_ARM64_RELOC_TLVP_LOAD_PAGE21 8U
#define LD_ARM64_RELOC_TLVP_LOAD_PAGEOFF12 9U
#define LD_ARM64_RELOC_ADDEND 10U

#define LD_EXPORT_SYMBOL_FLAGS_KIND_MASK 0x03U
#define LD_EXPORT_SYMBOL_FLAGS_KIND_REGULAR 0x00U
#define LD_EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL 0x01U
#define LD_EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE 0x02U
#define LD_EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04U
#define LD_EXPORT_SYMBOL_FLAGS_REEXPORT 0x08U
#define LD_EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER 0x10U

#define LD_REBASE_TYPE_POINTER 1U
#define LD_REBASE_OPCODE_DONE 0x00U
#define LD_REBASE_OPCODE_SET_TYPE_IMM 0x10U
#define LD_REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x20U
#define LD_REBASE_OPCODE_DO_REBASE_IMM_TIMES 0x50U

#define LD_BIND_TYPE_POINTER 1U
#define LD_BIND_OPCODE_DONE 0x00U
#define LD_BIND_OPCODE_SET_DYLIB_ORDINAL_IMM 0x10U
#define LD_BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB 0x20U
#define LD_BIND_OPCODE_SET_DYLIB_SPECIAL_IMM 0x30U
#define LD_BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM 0x40U
#define LD_BIND_OPCODE_SET_TYPE_IMM 0x50U
#define LD_BIND_OPCODE_SET_ADDEND_SLEB 0x60U
#define LD_BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB 0x70U
#define LD_BIND_OPCODE_DO_BIND 0x90U
#define LD_BIND_SYMBOL_FLAGS_WEAK_IMPORT 0x1U

#define LD_UNWIND_SECTION_VERSION 1U
#define LD_UNWIND_SECOND_LEVEL_REGULAR 2U
#define LD_UNWIND_HAS_LSDA 0x40000000U
#define LD_UNWIND_PERSONALITY_MASK 0x30000000U
#define LD_UNWIND_ARM64_MODE_MASK 0x0f000000U
#define LD_UNWIND_ARM64_MODE_DWARF 0x03000000U

#define LD_CSMAGIC_CODEDIRECTORY 0xfade0c02U
#define LD_CSMAGIC_EMBEDDED_SIGNATURE 0xfade0cc0U
#define LD_CS_SUPPORTSEXECSEG 0x20400U
#define LD_CS_ADHOC 0x2U
#define LD_CS_LINKER_SIGNED 0x20000U
#define LD_CS_EXECSEG_MAIN_BINARY 0x1U

typedef struct {
    uint32_t magic;
    int32_t cputype;
    int32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} ld_mach_header_64_t;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
} ld_load_command_t;

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
} ld_segment_command_64_t;

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
} ld_section_64_t;

typedef struct {
    uint32_t n_strx;
    uint8_t n_type;
    uint8_t n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} ld_nlist_64_t;

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
} ld_compact_unwind_entry_t;

typedef struct {
    uint32_t version;
    uint32_t common_encodings_offset;
    uint32_t common_encodings_count;
    uint32_t personalities_offset;
    uint32_t personalities_count;
    uint32_t index_offset;
    uint32_t index_count;
} ld_unwind_info_header_t;

typedef struct {
    uint32_t function_offset;
    uint32_t second_level_page_offset;
    uint32_t lsda_index_offset;
} ld_unwind_info_index_entry_t;

typedef struct {
    uint32_t function_offset;
    uint32_t lsda_offset;
} ld_unwind_info_lsda_entry_t;

typedef struct {
    uint32_t kind;
    uint16_t entry_page_offset;
    uint16_t entry_count;
} ld_unwind_info_regular_page_header_t;

typedef struct {
    uint32_t function_offset;
    uint32_t encoding;
} ld_unwind_info_regular_entry_t;

_Static_assert(sizeof(ld_compact_unwind_entry_t) == 32U,
               "compact unwind entry wire size must be 32 bytes");
_Static_assert(sizeof(ld_unwind_info_header_t) == 28U,
               "unwind info header wire size must be 28 bytes");
_Static_assert(sizeof(ld_unwind_info_index_entry_t) == 12U,
               "unwind info index wire size must be 12 bytes");
_Static_assert(sizeof(ld_unwind_info_lsda_entry_t) == 8U,
               "unwind LSDA entry wire size must be 8 bytes");
_Static_assert(sizeof(ld_unwind_info_regular_page_header_t) == 8U,
               "regular unwind page header wire size must be 8 bytes");
_Static_assert(sizeof(ld_unwind_info_regular_entry_t) == 8U,
               "regular unwind entry wire size must be 8 bytes");

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} ld_symtab_command_t;

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
} ld_dysymtab_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t rebase_off, rebase_size;
    uint32_t bind_off, bind_size;
    uint32_t weak_bind_off, weak_bind_size;
    uint32_t lazy_bind_off, lazy_bind_size;
    uint32_t export_off, export_size;
} ld_dyld_info_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t name_offset;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
} ld_dylib_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t dataoff, datasize;
} ld_linkedit_data_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint8_t uuid[16];
} ld_uuid_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t platform, minos, sdk, ntools;
} ld_build_version_command_t;

typedef struct {
    uint32_t tool;
    uint32_t version;
} ld_build_tool_version_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t version, sdk;
} ld_version_min_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint32_t path_offset;
} ld_rpath_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint64_t entryoff, stacksize;
} ld_entry_point_command_t;

typedef struct {
    uint32_t cmd, cmdsize;
    uint64_t version;
} ld_source_version_command_t;

typedef struct {
    uint32_t offset;
    uint16_t length;
    uint16_t kind;
} ld_data_in_code_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t nfat_arch;
} ld_fat_header_t;

typedef struct {
    int32_t cputype;
    int32_t cpusubtype;
    uint32_t offset;
    uint32_t size;
    uint32_t align;
} ld_fat_arch_t;

typedef struct {
    int32_t cputype;
    int32_t cpusubtype;
    uint64_t offset;
    uint64_t size;
    uint32_t align;
    uint32_t reserved;
} ld_fat_arch_64_t;

#endif
