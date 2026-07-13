#ifndef NATURE_LDD_INTERNAL_H
#define NATURE_LDD_INTERNAL_H

#include "ldd.h"
#include "macho_format.h"

#include "utils/uthash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LDD_IMAGE_BASE 0x100000000ULL
#define LDD_DEFAULT_DYLIB_VERSION 0x00010000U

typedef struct ldd_context ldd_context_t;
typedef struct ldd_object ldd_object_t;
typedef struct ldd_output_section ldd_output_section_t;
typedef struct ldd_symbol ldd_symbol_t;
typedef struct ldd_relocation ldd_relocation_t;

typedef struct ldd_string_set_entry {
    UT_hash_handle hh;
} ldd_string_set_entry_t;

typedef struct {
    uint8_t *bytes;
    size_t size;
    char *path;
} ldd_file_t;

typedef struct {
    ldd_section_64_t header;
    const uint8_t *data;
    const uint8_t *relocations;
    ldd_object_t *object;
    ldd_output_section_t *output;
    uint64_t output_offset;
    bool ignored;
} ldd_input_section_t;

typedef struct {
    ldd_nlist_64_t entry;
    const char *name;
    /* N_INDR stores the target symbol's string-table offset in n_value. */
    const char *alias_name;
    ldd_object_t *object;
    uint32_t index;
} ldd_input_symbol_t;

typedef struct ldd_relocation {
    uint32_t address;
    uint32_t symbolnum;
    uint8_t pcrel;
    uint8_t length;
    uint8_t external;
    uint8_t type;
} ldd_relocation_t;

struct ldd_object {
    ldd_file_t *file;
    const uint8_t *bytes;
    size_t size;
    char *member_name;
    bool archive_member;
    bool selected;
    uint32_t ncmds;
    uint32_t flags;
    ldd_input_section_t *sections;
    size_t section_count;
    ldd_input_symbol_t *symbols;
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
    LDD_SYMBOL_UNDEFINED,
    LDD_SYMBOL_DEFINED,
    LDD_SYMBOL_ABSOLUTE,
    LDD_SYMBOL_COMMON,
    LDD_SYMBOL_IMPORT,
} ldd_symbol_kind_t;

/* Mach-O's ld64 exposes these names as synthetic symbols.  They are kept
   separate from ordinary absolute symbols until section layout has finished,
   because their value depends on the final image addresses. */
typedef enum {
    LDD_BOUNDARY_NONE = 0,
    LDD_BOUNDARY_SECTION_START,
    LDD_BOUNDARY_SECTION_END,
    LDD_BOUNDARY_SEGMENT_START,
    LDD_BOUNDARY_SEGMENT_END,
} ldd_boundary_kind_t;

struct ldd_symbol {
    char *name;
    ldd_symbol_kind_t kind;
    ldd_object_t *object;
    ldd_input_symbol_t *input;
    uint64_t value;
    uint64_t size;
    uint32_t align;
    bool weak;
    bool weak_ref;
    bool alias;
    ldd_symbol_t *alias_target;
    bool dynamic;
    uint32_t got_index;
    uint32_t stub_index;
    uint32_t symtab_index;
    uint32_t dylib_ordinal;
    bool objc_selector_stub;
    ldd_symbol_t *objc_dispatch;
    uint32_t objc_selector_index;
    uint32_t objc_stub_index;
    uint64_t objc_methname_offset;
    uint64_t objc_selref_offset;
    ldd_boundary_kind_t boundary_kind;
    bool linker_defined;
    bool execute_header;
    char boundary_segment[17];
    char boundary_section[17];
    ldd_output_section_t *output;
    uint64_t output_offset;
    UT_hash_handle hh;
};

struct ldd_output_section {
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
    ldd_object_t **items;
    size_t count;
    size_t capacity;
} ldd_object_list_t;

typedef struct {
    ldd_file_t **items;
    size_t count;
    size_t capacity;
} ldd_file_list_t;

typedef struct {
    char *path;
    char *install_name;
    uint32_t current_version;
    uint32_t compatibility_version;
    char **exports;
    size_t export_count;
    size_t export_capacity;
    ldd_string_set_entry_t *export_set;
    char **weak_exports;
    size_t weak_export_count;
    size_t weak_export_capacity;
    ldd_string_set_entry_t *weak_export_set;
    char **reexports;
    size_t reexport_count;
    size_t reexport_capacity;
    ldd_string_set_entry_t *reexport_set;
    size_t reexport_owner;
    bool weak;
    bool reexport_only;
    bool has_reexport_owner;
    bool reexports_scanned;
} ldd_dylib_input_t;

typedef struct {
    ldd_dylib_input_t *items;
    size_t count;
    size_t capacity;
} ldd_dylib_list_t;

typedef struct {
    ldd_output_section_t **items;
    size_t count;
    size_t capacity;
} ldd_output_list_t;

typedef struct {
    ldd_symbol_t **items;
    size_t count;
    size_t capacity;
} ldd_symbol_list_t;

typedef struct {
    uint32_t segment;
    uint64_t offset;
} ldd_fixup_t;

typedef struct {
    ldd_fixup_t *items;
    size_t count;
    size_t capacity;
} ldd_fixup_list_t;

typedef struct {
    ldd_symbol_t *symbol;
    uint32_t segment;
    uint64_t offset;
    int64_t addend;
    bool weak;
    bool weak_definition;
} ldd_bind_t;

typedef struct {
    ldd_bind_t *items;
    size_t count;
    size_t capacity;
} ldd_bind_list_t;

typedef struct {
    ldd_object_t *object;
    uint32_t section_index;
    uint32_t relocation_index;
    uint64_t output_offset;
    int64_t addend;
} ldd_branch_thunk_t;

typedef struct {
    ldd_branch_thunk_t *items;
    size_t count;
    size_t capacity;
} ldd_branch_thunk_list_t;

typedef struct {
    ldd_object_t *object;
    ldd_relocation_t start_relocation;
    ldd_relocation_t lsda_relocation;
    uint64_t start_addend;
    uint64_t lsda_addend;
    uint32_t length;
    uint32_t encoding;
    uint32_t function_offset;
    uint32_t lsda_offset;
    uint8_t personality_index;
    bool has_start_relocation;
    bool has_lsda_relocation;
} ldd_unwind_record_t;

typedef struct {
    ldd_unwind_record_t *records;
    size_t count;
    size_t capacity;
    ldd_symbol_t *personalities[3];
    size_t personality_count;
    size_t lsda_count;
    ldd_output_section_t *output;
} ldd_unwind_state_t;

typedef struct {
    char name[17];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t maxprot;
    int32_t initprot;
    uint32_t command_index;
} ldd_segment_layout_t;

struct ldd_context {
    const ldd_options_t *options;
    ldd_file_list_t files;
    ldd_dylib_list_t dylibs;
    ldd_object_list_t objects;
    ldd_output_list_t outputs;
    ldd_symbol_list_t dynamic_symbols;
    ldd_fixup_list_t rebases;
    ldd_bind_list_t binds;
    ldd_branch_thunk_list_t branch_thunks;
    ldd_unwind_state_t unwind;
    ldd_symbol_t *symbols;
    ldd_output_section_t *got;
    ldd_output_section_t *stubs;
    ldd_output_section_t *objc_stubs;
    ldd_output_section_t *objc_methname;
    ldd_output_section_t *objc_selrefs;
    ldd_output_section_t *branch_islands;
    ldd_output_section_t *common;
    uint32_t got_count;
    uint32_t stub_count;
    uint32_t objc_stub_count;
    uint32_t min_version;
    uint32_t sdk_version;
    uint64_t entry_address;
    uint64_t entry_fileoff;
    char entry_name[1024];
    ldd_segment_layout_t segments[5];
    size_t segment_count;
    uint64_t header_size;
    uint64_t linkedit_fileoff;
    uint64_t linkedit_size;
    int error;
};

void *ldd_realloc_array(void *old, size_t old_count, size_t new_count, size_t element_size);
void ldd_string_set_deinit(ldd_string_set_entry_t **set);
int ldd_fail(ldd_context_t *ctx, int code, const char *format, ...);

int ldd_parse_input_file(ldd_context_t *ctx, const char *path);
int ldd_resolve_requested_libraries(ldd_context_t *ctx);
int ldd_resolve_reexport_libraries(ldd_context_t *ctx);
int ldd_link_macho(ldd_context_t *ctx);
int ldd_unwind_prepare(ldd_context_t *ctx);
int ldd_unwind_emit(ldd_context_t *ctx);

#endif
