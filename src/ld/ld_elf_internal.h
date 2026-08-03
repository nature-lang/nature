#ifndef NATURE_LD_ELF_INTERNAL_H
#define NATURE_LD_ELF_INTERNAL_H

#include "elf_format.h"
#include "ld.h"
#include "ld_elf_riscv_relax.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ld_elf_context ld_elf_context_t;
typedef struct ld_elf_file ld_elf_file_t;
typedef struct ld_elf_archive ld_elf_archive_t;
typedef struct ld_elf_object ld_elf_object_t;
typedef struct ld_elf_section ld_elf_section_t;
typedef struct ld_elf_symbol ld_elf_symbol_t;
typedef struct ld_elf_relocation ld_elf_relocation_t;
typedef struct ld_elf_group ld_elf_group_t;
typedef struct ld_elf_eh_record ld_elf_eh_record_t;
typedef struct ld_elf_output_section ld_elf_output_section_t;
typedef struct ld_elf_global ld_elf_global_t;

#define LD_ELF_GROUP_NONE UINT32_MAX
#define LD_ELF_EH_CIE_NONE UINT32_MAX
#define LD_ELF_EH_RELOCATION_NONE SIZE_MAX
#define LD_ELF_SCRIPT_LOAD_DEPTH 32U

struct ld_elf_file {
    uint8_t *bytes;
    size_t size;
    char *path;
};

struct ld_elf_relocation {
    uint64_t offset;
    uint32_t symbol_index;
    uint32_t type;
    int64_t addend;
    uint32_t relocation_section_index;
    size_t x86_tls_pair_index;
    uint32_t aarch64_thunk_entry_index;
    /*
     * Set by the x86-64 TLS sequence prepass when this relocation is either
     * the __tls_get_addr call paired with TLSGD/TLSLD or the TLSDESC_CALL
     * marker paired with GOTPC32_TLSDESC. Static local-exec relaxation
     * consumes the follower, so it must not participate in archive
     * extraction, undefined-symbol checks, GOT allocation, or the ordinary
     * relocation pass.
     */
    bool x86_tls_pair_follower;
    bool x86_gottpoff_relax;
    /*
     * A static, non-preemptible GOTPCRELX target can be addressed directly.
     * The allocation scan sets this after validating the compiler-emitted
     * instruction, allowing the ordinary GOT entry to be omitted.
     */
    bool x86_gotpcrelx_relax;
    bool ifunc_irelative;
    bool pie_relative;
};

struct ld_elf_eh_record {
    uint64_t input_offset;
    uint64_t size;
    uint64_t output_offset;
    uint32_t cie_record_index;
    ld_elf_section_t *canonical_cie_section;
    uint32_t canonical_cie_record_index;
    uint32_t owner_section_index;
    size_t owner_relocation_index;
    uint8_t length_field_size;
    uint8_t offset_size;
    bool cie;
    bool alive;
};

struct ld_elf_section {
    ld_elf64_shdr_t header;
    uint32_t index;
    const char *name;
    const uint8_t *data;
    uint8_t *owned_data;
    size_t data_size;
    ld_elf_relocation_t *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    ld_elf_eh_record_t *eh_records;
    size_t eh_record_count;
    size_t eh_record_capacity;
    ld_elf_riscv_relax_plan_t riscv_relax_plan;
    uint64_t eh_output_size;
    ld_elf_output_section_t *output;
    void *merge_input;
    uint64_t output_offset;
    uint32_t group_index;
    bool nobits;
    bool discarded;
    bool group_discarded;
    uint8_t link_order_discard_state;
};

struct ld_elf_output_section {
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    uint64_t entry_size;
    uint64_t addr;
    uint64_t file_offset;
    uint64_t size;
    uint64_t file_size;
    uint8_t *data;
    uint32_t index;
    ld_elf_output_section_t *header_link;
    uint32_t header_info;
    ld_elf_output_section_t *link_order_target;
    bool link_order_target_recorded;
};

struct ld_elf_symbol {
    ld_elf64_sym_t entry;
    uint32_t index;
    const char *name;
    uint8_t binding;
    uint8_t type;
    uint8_t visibility;
    ld_elf_section_t *section;
    ld_elf_global_t *resolved;
    uint32_t got_index;
    uint32_t gottp_index;
    uint32_t tlsgd_index;
    uint32_t pltgot_index;
    uint64_t common_offset;
};

struct ld_elf_group {
    uint32_t index;
    uint32_t section_index;
    uint32_t signature_symbol_index;
    uint32_t flags;
    const char *signature;
    uint32_t *members;
    size_t member_count;
    bool is_comdat;
    bool discarded;
};

struct ld_elf_object {
    ld_elf_file_t *file;
    ld_elf_archive_t *archive;
    const uint8_t *bytes;
    size_t size;
    uint64_t file_offset;
    char *member_name;
    char *display_name;
    ld_elf64_ehdr_t header;
    ld_elf_section_t *sections;
    size_t section_count;
    ld_elf_symbol_t *symbols;
    size_t symbol_count;
    ld_elf_group_t *groups;
    size_t group_count;
    uint32_t first_global_symbol;
    uint32_t symtab_section_index;
    const char *section_names;
    size_t section_names_size;
    const char *symbol_names;
    size_t symbol_names_size;
    uint32_t archive_member_index;
    bool archive_member;
    bool lazy;
    bool selected;
};

struct ld_elf_archive {
    ld_elf_file_t *file;
    ld_elf_object_t **members;
    size_t member_count;
    size_t member_capacity;
    const char *gnu_name_table;
    size_t gnu_name_table_size;
    size_t selected_member_count;
    bool thin;
};

typedef struct {
    ld_elf_file_t **items;
    size_t count;
    size_t capacity;
} ld_elf_file_list_t;

typedef struct {
    ld_elf_archive_t **items;
    size_t count;
    size_t capacity;
} ld_elf_archive_list_t;

typedef struct {
    ld_elf_object_t **items;
    size_t count;
    size_t capacity;
} ld_elf_object_list_t;

struct ld_elf_context {
    const ld_options_t *options;
    ld_elf_file_list_t files;
    ld_elf_archive_list_t archives;
    ld_elf_object_list_t objects;
    const char *script_stack[LD_ELF_SCRIPT_LOAD_DEPTH];
    size_t script_depth;
    void *backend_state;
    int error;
};

void ld_elf_context_init(ld_elf_context_t *ctx, const ld_options_t *options);
void ld_elf_context_deinit(ld_elf_context_t *ctx);

int ld_elf_load_input(ld_elf_context_t *ctx, const char *path);
int ld_elf_load_options_inputs(ld_elf_context_t *ctx);
int ld_elf_select_object(ld_elf_context_t *ctx, ld_elf_object_t *object);
int ld_link_elf(const ld_options_t *options);

int ld_elf_fail(ld_elf_context_t *ctx, int code, const char *format, ...);

static inline bool ld_elf_symbol_is_undefined(const ld_elf_symbol_t *symbol) {
    return symbol && symbol->entry.st_shndx == LD_ELF_SHN_UNDEF;
}

static inline bool ld_elf_symbol_is_global(const ld_elf_symbol_t *symbol) {
    return symbol && (symbol->binding == LD_ELF_STB_GLOBAL ||
                      symbol->binding == LD_ELF_STB_WEAK ||
                      symbol->binding == LD_ELF_STB_GNU_UNIQUE);
}

static inline bool ld_elf_symbol_is_definition(const ld_elf_symbol_t *symbol) {
    return ld_elf_symbol_is_global(symbol) && !ld_elf_symbol_is_undefined(symbol);
}

#endif
