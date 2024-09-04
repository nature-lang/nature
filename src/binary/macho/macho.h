#ifndef NATURE_MACHO_H
#define NATURE_MACHO_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "src/binary/linker.h"
#include "utils/string_view.h"
#include "utils/ct_list.h"

enum : uint32_t {
    EXPORT_SYMBOL_FLAGS_KIND_MASK = 0x03,
    EXPORT_SYMBOL_FLAGS_KIND_REGULAR = 0x00,
    EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL = 0x01,
    EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE = 0x02,
    EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION = 0x04,
    EXPORT_SYMBOL_FLAGS_REEXPORT = 0x08,
    EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10,
};

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} SegmentCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t nameoff;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
} DylibCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t nameoff;
} DylinkerCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} SymtabCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
} DysymtabCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t version;
    uint32_t sdk;
} VersionMinCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
} DyldInfoCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint8_t uuid[16];
} UUIDCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t path_off;
} RpathCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t dataoff;
    uint32_t datasize;
} LinkEditDataCommand;

typedef struct {
    uint32_t tool;
    uint32_t version;
} BuildToolVersion;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
} BuildVersionCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
} EntryPointCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t version;
} SourceVersionCommand;

typedef struct {
    uint32_t offset;
    uint16_t length;
    uint16_t kind;
} DataInCodeEntry;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t umbrella_off;
} UmbrellaCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t count;
} LinkerOptionCommand;

typedef enum {
    FILETYPE_UNKNOWN,
    FILETYPE_EMPTY,
    FILETYPE_ELF_OBJ,
    FILETYPE_ELF_DSO,
    FILETYPE_MACH_OBJ,
    FILETYPE_MACH_EXE,
    FILETYPE_MACH_DYLIB,
    FILETYPE_MACH_BUNDLE,
    FILETYPE_MACH_UNIVERSAL,
    FILETYPE_AR,
    FILETYPE_THIN_AR,
    FILETYPE_TAPI,
    FILETYPE_TEXT,
    FILETYPE_GCC_LTO_OBJ,
    FILETYPE_LLVM_BITCODE,
} FileType;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
} LoadCommand;

enum : uint32_t {
    S_ATTR_LOC_RELOC = 0x1,
    S_ATTR_EXT_RELOC = 0x2,
    S_ATTR_SOME_INSTRUCTIONS = 0x4,
    S_ATTR_DEBUG = 0x20000,
    S_ATTR_SELF_MODIFYING_CODE = 0x40000,
    S_ATTR_LIVE_SUPPORT = 0x80000,
    S_ATTR_NO_DEAD_STRIP = 0x100000,
    S_ATTR_STRIP_STATIC_SYMS = 0x200000,
    S_ATTR_NO_TOC = 0x400000,
    S_ATTR_PURE_INSTRUCTIONS = 0x800000,
};

enum : uint32_t {
    BIND_SPECIAL_DYLIB_SELF = 0,
    BIND_TYPE_POINTER = 1,
    BIND_TYPE_TEXT_ABSOLUTE32 = 2,
    BIND_TYPE_TEXT_PCREL32 = 3,
    BIND_SPECIAL_DYLIB_WEAK_LOOKUP = (uint32_t) -3,
    BIND_SPECIAL_DYLIB_FLAT_LOOKUP = (uint32_t) -2,
    BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE = (uint32_t) -1,
    BIND_SYMBOL_FLAGS_WEAK_IMPORT = 1,
    BIND_SYMBOL_FLAGS_NON_WEAK_DEFINITION = 8,
    BIND_OPCODE_MASK = 0xF0,
    BIND_IMMEDIATE_MASK = 0x0F,
    BIND_OPCODE_DONE = 0x00,
    BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10,
    BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB = 0x20,
    BIND_OPCODE_SET_DYLIB_SPECIAL_IMM = 0x30,
    BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40,
    BIND_OPCODE_SET_TYPE_IMM = 0x50,
    BIND_OPCODE_SET_ADDEND_SLEB = 0x60,
    BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70,
    BIND_OPCODE_ADD_ADDR_ULEB = 0x80,
    BIND_OPCODE_DO_BIND = 0x90,
    BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB = 0xA0,
    BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED = 0xB0,
    BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB = 0xC0,
    BIND_OPCODE_THREADED = 0xD0,
    BIND_SUBOPCODE_THREADED_SET_BIND_ORDINAL_TABLE_SIZE_ULEB = 0x00,
    BIND_SUBOPCODE_THREADED_APPLY = 0x01,
};

enum : uint32_t {
    MH_NOUNDEFS = 0x1,
    MH_INCRLINK = 0x2,
    MH_DYLDLINK = 0x4,
    MH_BINDATLOAD = 0x8,
    MH_PREBOUND = 0x10,
    MH_SPLIT_SEGS = 0x20,
    MH_LAZY_INIT = 0x40,
    MH_TWOLEVEL = 0x80,
    MH_FORCE_FLAT = 0x100,
    MH_NOMULTIDEFS = 0x200,
    MH_NOFIXPREBINDING = 0x400,
    MH_PREBINDABLE = 0x800,
    MH_ALLMODSBOUND = 0x1000,
    MH_SUBSECTIONS_VIA_SYMBOLS = 0x2000,
    MH_CANONICAL = 0x4000,
    MH_WEAK_DEFINES = 0x8000,
    MH_BINDS_TO_WEAK = 0x10000,
    MH_ALLOW_STACK_EXECUTION = 0x20000,
    MH_ROOT_SAFE = 0x40000,
    MH_SETUID_SAFE = 0x80000,
    MH_NO_REEXPORTED_DYLIBS = 0x100000,
    MH_PIE = 0x200000,
    MH_DEAD_STRIPPABLE_DYLIB = 0x400000,
    MH_HAS_TLV_DESCRIPTORS = 0x800000,
    MH_NO_HEAP_EXECUTION = 0x1000000,
    MH_APP_EXTENSION_SAFE = 0x02000000,
    MH_NLIST_OUTOFSYNC_WITH_DYLDINFO = 0x04000000,
    MH_SIM_SUPPORT = 0x08000000,
};
enum : uint32_t {
    LC_REQ_DYLD = 0x80000000,
};
enum : uint32_t {
    LC_SEGMENT = 0x1,
    LC_SYMTAB = 0x2,
    LC_SYMSEG = 0x3,
    LC_THREAD = 0x4,
    LC_UNIXTHREAD = 0x5,
    LC_LOADFVMLIB = 0x6,
    LC_IDFVMLIB = 0x7,
    LC_IDENT = 0x8,
    LC_FVMFILE = 0x9,
    LC_PREPAGE = 0xa,
    LC_DYSYMTAB = 0xb,
    LC_LOAD_DYLIB = 0xc,
    LC_ID_DYLIB = 0xd,
    LC_LOAD_DYLINKER = 0xe,
    LC_ID_DYLINKER = 0xf,
    LC_PREBOUND_DYLIB = 0x10,
    LC_ROUTINES = 0x11,
    LC_SUB_FRAMEWORK = 0x12,
    LC_SUB_UMBRELLA = 0x13,
    LC_SUB_CLIENT = 0x14,
    LC_SUB_LIBRARY = 0x15,
    LC_TWOLEVEL_HINTS = 0x16,
    LC_PREBIND_CKSUM = 0x17,
    LC_LOAD_WEAK_DYLIB = (0x18 | LC_REQ_DYLD),
    LC_SEGMENT_64 = 0x19,
    LC_ROUTINES_64 = 0x1a,
    LC_UUID = 0x1b,
    LC_RPATH = (0x1c | LC_REQ_DYLD),
    LC_CODE_SIGNATURE = 0x1d,
    LC_SEGMENT_SPLIT_INFO = 0x1e,
    LC_REEXPORT_DYLIB = (0x1f | LC_REQ_DYLD),
    LC_LAZY_LOAD_DYLIB = 0x20,
    LC_ENCRYPTION_INFO = 0x21,
    LC_DYLD_INFO = 0x22,
    LC_DYLD_INFO_ONLY = (0x22 | LC_REQ_DYLD),
    LC_LOAD_UPWARD_DYLIB = (0x23 | LC_REQ_DYLD),
    LC_VERSION_MIN_MACOSX = 0x24,
    LC_VERSION_MIN_IPHONEOS = 0x25,
    LC_FUNCTION_STARTS = 0x26,
    LC_DYLD_ENVIRONMENT = 0x27,
    LC_MAIN = (0x28 | LC_REQ_DYLD),
    LC_DATA_IN_CODE = 0x29,
    LC_SOURCE_VERSION = 0x2A,
    LC_DYLIB_CODE_SIGN_DRS = 0x2B,
    LC_ENCRYPTION_INFO_64 = 0x2C,
    LC_LINKER_OPTION = 0x2D,
    LC_LINKER_OPTIMIZATION_HINT = 0x2E,
    LC_VERSION_MIN_TVOS = 0x2F,
    LC_VERSION_MIN_WATCHOS = 0x30,
    LC_NOTE = 0x31,
    LC_BUILD_VERSION = 0x32,
    LC_DYLD_EXPORTS_TRIE = (0x33 | LC_REQ_DYLD),
    LC_DYLD_CHAINED_FIXUPS = (0x34 | LC_REQ_DYLD),
};

typedef struct Subsection {
    union {
        struct InputSection *isec;
        struct Subsection *replacer;
    };

    uint32_t input_addr;
    uint32_t input_size;
    uint32_t output_offset;
    uint32_t rel_offset;
    uint32_t nrels;
    uint32_t unwind_offset;
    uint32_t nunwind;

    _Atomic char p2align;
    _Atomic bool is_alive;

    bool added_to_osec: 1;
    bool is_replaced: 1;
    bool has_compact_unwind: 1;
} Subsection;

typedef struct {
    string_view_t *install_name; // string_view*
    slice_t *reexported_libs; // string_view*
    slice_t *exports; // string_view*, TODO 增加 table 表和重复检测
    slice_t *weak_exports; // string_view*
} TextDylib;


typedef struct {
    bool all_load;
    bool needed;
    bool hidden;
    bool weak;
    bool reexport;
    bool implicit;
} ReaderContext;

typedef struct {
    FILE *file;
    char *name;
    char *data;
    int64_t size;
} MappedFile;

typedef struct {
    // int fd; // 文件描述符
    FILE *file; // 文件指针
    char *path; // 文件路径
    size_t size; // 文件大小
    uint8_t *buf; // 缓冲区指针
} OutputFile;

typedef struct {
} OutputSection;

typedef struct {
    int32_t seg_idx; // default = -1;
} OutputSegment;

typedef struct {
    void *ptr;
    uint32_t count;
} MachoSpan;


static inline MachoSpan *macho_span_new(void *ptr, uint32_t count) {
    MachoSpan *span = NEW(MachoSpan);
    span->ptr = ptr;
    span->count = count;
}

typedef struct {
    string_view_t *filename;
    slice_t *syms; // Symbol
    int64_t priority;
    _Atomic bool is_alive;
    bool is_dylib;
    bool is_hidden;
    bool is_weak;
    string_view_t *archive_name;

    int32_t num_stabs;
    int32_t num_locals;
    int32_t num_globals;
    int32_t num_undefs;
    int32_t stabs_offset;
    int32_t locals_offset;
    int32_t globals_offset;
    int32_t undefs_offset;
    int32_t strtab_size;
    int32_t strtab_offset;
    char *oso_name;
} InputFile;


typedef struct {
    string_view_t *name;
    InputFile *file;
    Subsection *subsec;
    uint64_t value;

    int32_t stub_idx;
    int32_t got_idx;
    int32_t tlv_idx;

    pthread_mutex_t mu;

    _Atomic uint8_t flags;
} Symbol;

static Symbol *symbol_new(string_view_t *name) {
    Symbol *s = NEW(Symbol);
    s->name = name;
    pthread_mutex_init(&s->mu, NULL);
    return s;
}

typedef struct {
    int64_t priority;
    int64_t dylib_idx;
    string_view_t *install_name;
    bool is_reexported;
    bool is_dylib;
    bool is_hidden;
    bool is_weak;
    bool is_alive;

    slice_t *reexported_libs; // string_view*
    slice_t *rpaths; // char*
    slice_t *hoisted_libs; // DylibFile*

    slice_t *exports; // string_view*
    table_t *export_table; // key: itoa(string_view*), value: u64

    slice_t *syms; // Symbol

    MappedFile *mf;
} DylibFile;


typedef struct {
    uint32_t stroff;
    union {
        uint8_t n_type;
        struct {
            uint8_t is_extern: 1;
            uint8_t type: 3;
            uint8_t is_private_extern: 1;
            uint8_t stab: 3;
        };
    };

    uint8_t sect;

    union {
        uint16_t desc;
        struct {
            uint8_t padding;
            uint8_t common_p2align: 4;
        };
    };

    uint64_t value;
} MachSym;

static inline bool machsym_is_common(MachSym *sym) {
    return sym->type == 0 && sym->is_extern && sym->value != 0; // 假设 N_UNDF 为 0
}

// 函数定义
static inline bool machsym_is_undef(MachSym *sym) {
    return sym->type == 0 && !machsym_is_common(sym); // 假设 N_UNDF 为 0
}

typedef struct {
    char sectname[16];
    char segname[16];
    uint64_t addr;  // 假设 Word<E> 是 64 位
    uint64_t size;
    uint32_t offset;
    uint32_t p2align;
    uint32_t reloff;
    uint32_t nreloc;
    uint8_t type;
    uint8_t attr[3];  // 使用 3 字节数组来精确匹配 ul24
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} MachSection;

typedef struct {
    uint32_t version;
    uint8_t flags;
    uint8_t swift_version;
    uint16_t swift_lang_version;
} ObjcImageInfo;

typedef struct {
    slice_t *sections; // InputSection
    slice_t *subsections;
    slice_t *sym_to_subsec;
    MachoSpan *mach_syms;
    slice_t *local_syms;
    slice_t *unwind_records;
    slice_t *cies;
    slice_t *fdes;
    MachSection *eh_frame_sec;

    ObjcImageInfo *objc_image_info;
    void *lto_module;
    MachSection *mod_init_func;

    slice_t *init_functions;
    slice_t *mach_syms2; // 当前 object file 匹配的文件列表

    MachSection *unwind_sec;
    MachSection *common_hdr;
    struct InputSection *common_sec;
    bool has_debug_info;

    slice_t *subsec_pool; // Subsection*
    slice_t *mach_sec_pool; // MachSection*

    char *archive_name;
    bool is_alive;
    bool is_hidden;

    int64_t priority;
    MappedFile *mf;
} ObjectFile;

typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} MachHeader;

typedef struct {
} OutputMachHeader;

typedef struct {
} StubsSection;

typedef struct {
} UnwindInfoSection;

typedef struct {
} EhFrameSection;

typedef struct {
} GotSection;

typedef struct {
} ThreadPtrsSection;

typedef struct {
} ExportSection;

typedef struct {
} SymtabSection;

typedef struct {
} StrtabSection;

typedef struct {
} IndirectSymtabSection;

typedef struct {
    MachSection hdr;
    uint32_t sect_idx;
    bool is_hidden;
    OutputSegment *seg;
    bool is_output_section;
} Chunk;

typedef struct {
    ReaderContext reader;
    void *tg;

    uint8_t output_type;
    int64_t file_priority; // default 10000
    table_t *missing_files;
    uint8_t uuid[16];

    // arg ---
    int64_t thread_count;
    int64_t pagezero_size; // ctx.arg.pagezero_size = (ctx.output_type == MH_EXECUTE) ? 0x100000000 : 0;
    slice_t *library_paths;
    bool dead_strip_dylibs; // default true
    bool implicit_dylibs;
    slice_t *syslibroot;
    char *executable_path;
    bool application_extension;

    slice_t *input_files; // a.o/b.o/c.o/libuv.a
    slice_t *input_libs; // lSystem

    uint64_t tls_begin; // 这个字段用于存储线程局部存储（Thread Local Storage, TLS）的起始地址。
    char *cwd;


    // 移除 LTO 相关字段
    // LTOPlugin lto;
    // pthread_once_t lto_plugin_loaded;

    table_t *symbol_table; // Symbol

    char *output_name;
    OutputFile *output_file;
    uint8_t *buf;

    slice_t *obj_pool; // object_file
    slice_t *dylib_pool; // dylib_file
    slice_t *string_pool;
    slice_t *mf_pool; // mapped_file
    slice_t *chunk_pool;

    slice_t *timer_records; // TimerRecord

    slice_t *objs; // ObjectFile
    slice_t *dylibs; // DylibFile

    ObjectFile *internal_obj;

    void *text_seg;
    void *data_const_seg;
    void *data_seg;
    void *linkedit_seg;

    slice_t *segments; // OutputSegment
    slice_t *chunks; // chunk

    OutputMachHeader mach_hdr;
    StubsSection stubs;
    UnwindInfoSection unwind_info;
    EhFrameSection eh_frame;
    GotSection got;
    ThreadPtrsSection thread_ptrs;
    ExportSection export_;
    SymtabSection symtab;
    StrtabSection strtab;
    IndirectSymtabSection indir_symtab;


    void *stub_helper;
    void *lazy_symbol_ptr;
    void *lazy_bind;
    void *rebase;
    void *bind;
    void *chained_fixups;
    void *function_starts;
    void *image_info;
    void *code_sig;
    void *objc_stubs;
    void *data_in_code;
    void *init_offsets;

    OutputSection *text;
    OutputSection *data;
    OutputSection *bss;
    OutputSection *common;
} macho_context_t;

enum : uint32_t {
    S_REGULAR = 0x0,
    S_ZEROFILL = 0x1,
    S_CSTRING_LITERALS = 0x2,
    S_4BYTE_LITERALS = 0x3,
    S_8BYTE_LITERALS = 0x4,
    S_LITERAL_POINTERS = 0x5,
    S_NON_LAZY_SYMBOL_POINTERS = 0x6,
    S_LAZY_SYMBOL_POINTERS = 0x7,
    S_SYMBOL_STUBS = 0x8,
    S_MOD_INIT_FUNC_POINTERS = 0x9,
    S_MOD_TERM_FUNC_POINTERS = 0xa,
    S_COALESCED = 0xb,
    S_GB_ZEROFILL = 0xc,
    S_INTERPOSING = 0xd,
    S_16BYTE_LITERALS = 0xe,
    S_DTRACE_DOF = 0xf,
    S_LAZY_DYLIB_SYMBOL_POINTERS = 0x10,
    S_THREAD_LOCAL_REGULAR = 0x11,
    S_THREAD_LOCAL_ZEROFILL = 0x12,
    S_THREAD_LOCAL_VARIABLES = 0x13,
    S_THREAD_LOCAL_VARIABLE_POINTERS = 0x14,
    S_THREAD_LOCAL_INIT_FUNCTION_POINTERS = 0x15,
    S_INIT_FUNC_OFFSETS = 0x16,
};

typedef struct {
    ObjectFile *file;
    MachSection *hdr;
    uint32_t secidx;
    string_view_t *contents;
    slice_t *syms; // Symbol
    slice_t *rels; // Relocation
    OutputSection *osec;
} InputSection;


void macho_output_file();

#endif //NATURE_MACHO_H
