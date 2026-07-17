#ifndef NATURE_LD_INTERNAL_H
#define NATURE_LD_INTERNAL_H

#include "ld.h"
#include "macho_format.h"

#include "utils/sc_map.h"
#include "utils/uthash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LD_IMAGE_BASE 0x100000000ULL
#define LD_DEFAULT_DYLIB_VERSION 0x00010000U

typedef struct ld_context ld_context_t;
typedef struct ld_object ld_object_t;
typedef struct ld_output_section ld_output_section_t;
typedef struct ld_symbol ld_symbol_t;
typedef struct ld_relocation ld_relocation_t;

/* A Mach-O relocation can name either a resolved global symbol or a local
   nlist entry owned by one input object.  Synthetic sections must preserve
   that distinction: reducing a local reference to a global name loses valid
   GOT/TLVP targets emitted by clang. */
typedef struct {
    ld_symbol_t *global;
    ld_object_t *object;
    uint32_t input_index;
} ld_macho_ref_t;

typedef struct {
    ld_macho_ref_t *items;
    size_t count;
    size_t capacity;
} ld_macho_ref_list_t;

typedef struct ld_string_set_entry {
    UT_hash_handle hh;
} ld_string_set_entry_t;

typedef struct {
    uint8_t *bytes;
    size_t size;
    char *path;
    size_t input_priority;
} ld_file_t;

typedef struct {
    ld_section_64_t header;
    const uint8_t *data;
    const uint8_t *relocations;
    ld_object_t *object;
    ld_output_section_t *output;
    uint64_t output_offset;
    bool ignored;
} ld_input_section_t;

typedef struct {
    ld_nlist_64_t entry;
    const char *name;
    /* N_INDR stores the target symbol's string-table offset in n_value. */
    const char *alias_name;
    ld_object_t *object;
    uint32_t index;
    uint32_t output_symtab_index;
} ld_input_symbol_t;

typedef struct ld_relocation {
    uint32_t address;
    uint32_t symbolnum;
    uint8_t pcrel;
    uint8_t length;
    uint8_t external;
    uint8_t type;
} ld_relocation_t;

struct ld_object {
    ld_file_t *file;
    const uint8_t *bytes;
    size_t size;
    char *member_name;
    bool archive_member;
    bool selected;
    uint32_t ncmds;
    uint32_t flags;
    ld_input_section_t *sections;
    size_t section_count;
    ld_input_symbol_t *symbols;
    size_t symbol_count;
    const char *strtab;
    uint32_t strtab_size;
    /* Linkedit tables may start at an offset that is not naturally aligned;
       decode entries with memcpy at the consumer instead of dereferencing a
       wire-format struct through an unaligned pointer. */
    const uint8_t *data_in_code;
    size_t data_in_code_count;
};

typedef enum {
    LD_SYMBOL_UNDEFINED,
    LD_SYMBOL_DEFINED,
    LD_SYMBOL_ABSOLUTE,
    LD_SYMBOL_COMMON,
    LD_SYMBOL_IMPORT,
} ld_symbol_kind_t;

typedef enum {
    LD_VISIBILITY_GLOBAL = 0,
    LD_VISIBILITY_HIDDEN,
    LD_VISIBILITY_LOCAL,
} ld_symbol_visibility_t;

/* Mach-O's ld64 exposes these names as synthetic symbols.  They are kept
   separate from ordinary absolute symbols until section layout has finished,
   because their value depends on the final image addresses. */
typedef enum {
    LD_BOUNDARY_NONE = 0,
    LD_BOUNDARY_SECTION_START,
    LD_BOUNDARY_SECTION_END,
    LD_BOUNDARY_SEGMENT_START,
    LD_BOUNDARY_SEGMENT_END,
} ld_boundary_kind_t;

struct ld_symbol {
    char *name;
    ld_symbol_kind_t kind;
    ld_object_t *object;
    ld_input_symbol_t *input;
    uint64_t value;
    uint64_t size;
    uint32_t align;
    bool weak;
    bool weak_ref;
    bool alias;
    ld_symbol_t *alias_target;
    bool dynamic;
    bool referenced_dynamically;
    bool tlv;
    bool dylib_absolute;
    bool dylib_weak_definition;
    ld_symbol_visibility_t visibility;
    uint32_t resolver_class;
    size_t resolver_priority;
    size_t resolver_order;
    size_t dylib_index;
    uint32_t got_index;
    uint32_t tlv_ptr_index;
    uint32_t stub_index;
    uint32_t symtab_index;
    uint32_t dylib_ordinal;
    bool objc_selector_stub;
    ld_symbol_t *objc_dispatch;
    uint32_t objc_selector_index;
    uint32_t objc_stub_index;
    uint64_t objc_methname_offset;
    uint64_t objc_selref_offset;
    ld_boundary_kind_t boundary_kind;
    bool linker_defined;
    bool execute_header;
    char boundary_segment[17];
    char boundary_section[17];
    ld_output_section_t *output;
    uint64_t output_offset;
    UT_hash_handle hh;
};

struct ld_output_section {
    char segname[17];
    char sectname[17];
    uint32_t flags;
    uint32_t align;
    uint64_t addr;
    uint64_t fileoff;
    uint64_t size;
    uint64_t file_size;
    uint8_t *data;
    size_t data_capacity;
    bool zerofill;
    uint32_t segment_index;
    uint32_t section_index;
    uint32_t reserved1;
    uint32_t reserved2;
};

typedef struct {
    ld_object_t **items;
    size_t count;
    size_t capacity;
} ld_object_list_t;

typedef struct {
    ld_file_t **items;
    size_t count;
    size_t capacity;
} ld_file_list_t;

typedef struct {
    char *name;
    char *import_name;
    bool weak;
    bool absolute;
    bool tlv;
    bool reexport;
} ld_dylib_symbol_t;

typedef struct {
    char *path;
    char *install_name;
    size_t input_priority;
    uint32_t current_version;
    uint32_t compatibility_version;
    char **exports;
    size_t export_count;
    size_t export_capacity;
    ld_string_set_entry_t *export_set;
    char **weak_exports;
    size_t weak_export_count;
    size_t weak_export_capacity;
    ld_string_set_entry_t *weak_export_set;
    char **reexports;
    size_t reexport_count;
    size_t reexport_capacity;
    ld_string_set_entry_t *reexport_set;
    char **rpaths;
    size_t rpath_count;
    size_t rpath_capacity;
    ld_string_set_entry_t *rpath_set;
    ld_dylib_symbol_t *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    /* Names are separately allocated and stable; indices survive symbols realloc. */
    struct sc_map_s64 symbol_index;
    size_t reexport_owner;
    bool weak;
    bool reexport_only;
    bool has_reexport_owner;
    bool reexports_scanned;
} ld_dylib_input_t;

typedef struct {
    ld_dylib_input_t *items;
    size_t count;
    size_t capacity;
} ld_dylib_list_t;

typedef struct {
    ld_output_section_t **items;
    size_t count;
    size_t capacity;
} ld_output_list_t;

typedef struct {
    ld_symbol_t **items;
    size_t count;
    size_t capacity;
} ld_symbol_list_t;

typedef struct {
    uint32_t segment;
    uint64_t offset;
} ld_fixup_t;

typedef struct {
    ld_fixup_t *items;
    size_t count;
    size_t capacity;
} ld_fixup_list_t;

typedef struct {
    ld_symbol_t *symbol;
    uint32_t segment;
    uint64_t offset;
    int64_t addend;
    bool weak;
    bool weak_definition;
} ld_bind_t;

typedef struct {
    ld_bind_t *items;
    size_t count;
    size_t capacity;
} ld_bind_list_t;

typedef struct {
    ld_object_t *object;
    uint32_t section_index;
    uint32_t relocation_index;
    uint64_t output_offset;
    int64_t addend;
} ld_branch_thunk_t;

typedef struct {
    ld_branch_thunk_t *items;
    size_t count;
    size_t capacity;
} ld_branch_thunk_list_t;

typedef struct {
    ld_object_t *object;
    ld_relocation_t start_relocation;
    ld_relocation_t lsda_relocation;
    uint64_t start_addend;
    uint64_t lsda_addend;
    uint32_t length;
    uint32_t encoding;
    uint32_t function_offset;
    uint32_t lsda_offset;
    uint8_t personality_index;
    bool has_start_relocation;
    bool has_lsda_relocation;
} ld_unwind_record_t;

typedef struct {
    ld_unwind_record_t *records;
    size_t count;
    size_t capacity;
    ld_macho_ref_t personalities[3];
    size_t personality_count;
    size_t lsda_count;
    ld_output_section_t *output;
} ld_unwind_state_t;

typedef struct {
    char name[17];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t maxprot;
    int32_t initprot;
    uint32_t command_index;
} ld_segment_layout_t;

struct ld_context {
    const ld_options_t *options;
    ld_file_list_t files;
    ld_dylib_list_t dylibs;
    ld_object_list_t objects;
    ld_output_list_t outputs;
    ld_symbol_list_t dynamic_symbols;
    ld_fixup_list_t rebases;
    ld_bind_list_t binds;
    ld_branch_thunk_list_t branch_thunks;
    ld_unwind_state_t unwind;
    ld_symbol_t *symbols;
    ld_output_section_t *got;
    ld_output_section_t *tlv_ptrs;
    ld_output_section_t *stubs;
    ld_output_section_t *objc_stubs;
    ld_output_section_t *objc_methname;
    ld_output_section_t *objc_selrefs;
    ld_output_section_t *branch_islands;
    ld_output_section_t *common;
    ld_macho_ref_list_t got_refs;
    ld_macho_ref_list_t tlv_ptr_refs;
    uint32_t got_count;
    uint32_t tlv_ptr_count;
    uint32_t stub_count;
    uint32_t objc_stub_count;
    uint32_t min_version;
    uint32_t sdk_version;
    uint64_t entry_address;
    uint64_t entry_fileoff;
    char entry_name[1024];
    ld_segment_layout_t segments[5];
    size_t segment_count;
    uint64_t header_size;
    uint64_t linkedit_fileoff;
    uint64_t linkedit_size;
    int error;
};

void *ld_realloc_array(void *old, size_t old_count, size_t new_count, size_t element_size);
void ld_string_set_deinit(ld_string_set_entry_t **set);
int ld_fail(ld_context_t *ctx, int code, const char *format, ...);

int ld_parse_input_file(ld_context_t *ctx, const char *path);
int ld_resolve_requested_libraries(ld_context_t *ctx);
int ld_resolve_reexport_libraries(ld_context_t *ctx);
int ld_link_macho(ld_context_t *ctx);
int ld_link_elf(const ld_options_t *options);
int ld_unwind_prepare(ld_context_t *ctx);
int ld_unwind_emit(ld_context_t *ctx);

#endif
