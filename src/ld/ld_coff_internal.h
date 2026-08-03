#ifndef NATURE_LD_COFF_INTERNAL_H
#define NATURE_LD_COFF_INTERNAL_H

#include "coff_format.h"
#include "ld.h"
#include "ld_coff_reader.h"

#include "utils/uthash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Normalized C model corresponding to the ordinary-link portions of
 * lld/COFF/InputFiles, Symbols, SymbolTable, Chunks, and Writer.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

typedef struct ld_coff_context ld_coff_context_t;
typedef struct ld_coff_file ld_coff_file_t;
typedef struct ld_coff_object ld_coff_object_t;
typedef struct ld_coff_section ld_coff_section_t;
typedef struct ld_coff_symbol ld_coff_symbol_t;
typedef struct ld_coff_global ld_coff_global_t;
typedef struct ld_coff_output_section ld_coff_output_section_t;
typedef struct ld_coff_import ld_coff_import_t;

typedef struct {
    uint32_t offset;
    uint32_t symbol_index;
    uint16_t type;
} ld_coff_relocation_t;

struct ld_coff_file {
    uint8_t *bytes;
    size_t size;
    char *path;
};

struct ld_coff_section {
    ld_coff_object_t *object;
    char *name;
    uint32_t index;
    uint32_t characteristics;
    uint32_t alignment;
    const uint8_t *data;
    uint8_t *owned_data;
    uint32_t data_size;
    uint32_t virtual_size;
    ld_coff_relocation_t *relocations;
    size_t relocation_count;
    uint8_t comdat_selection;
    uint32_t comdat_checksum;
    uint32_t associative_section;
    const char *comdat_key;
    ld_coff_output_section_t *output;
    uint32_t output_offset;
    uint32_t input_order;
    bool uninitialized;
    bool discarded;
    bool synthetic;
};

struct ld_coff_symbol {
    char *name;
    uint32_t index;
    uint32_t value;
    int32_t section_number;
    uint16_t type;
    uint8_t storage_class;
    uint8_t aux_count;
    uint32_t weak_target_index;
    uint32_t weak_characteristics;
    ld_coff_section_t *section;
    ld_coff_global_t *global;
    bool auxiliary;
};

struct ld_coff_object {
    ld_coff_file_t *file;
    const uint8_t *bytes;
    size_t size;
    char *display_name;
    char *member_name;
    ld_coff_section_t *sections;
    size_t section_count;
    ld_coff_symbol_t *symbols;
    size_t symbol_count;
    uint32_t input_order;
    bool bigobj;
    bool lazy;
    bool selected;
    bool archive_member;
    bool import_object;
    ld_coff_import_t *import;
};

typedef enum {
    LD_COFF_GLOBAL_UNDEFINED = 0,
    LD_COFF_GLOBAL_DEFINED,
    LD_COFF_GLOBAL_COMMON,
    LD_COFF_GLOBAL_ABSOLUTE,
    LD_COFF_GLOBAL_IMPORT_IAT,
    LD_COFF_GLOBAL_IMPORT_THUNK,
} ld_coff_global_kind_t;

struct ld_coff_global {
    char *name;
    UT_hash_handle hh;
    ld_coff_global_kind_t kind;
    ld_coff_object_t *object;
    ld_coff_symbol_t *symbol;
    ld_coff_section_t *section;
    ld_coff_import_t *import;
    uint64_t value;
    uint64_t common_size;
    uint32_t common_alignment;
    char *fallback_name;
    uint32_t fallback_characteristics;
    /* Object that introduced an IMAGE_SYM_CLASS_WEAK_EXTERNAL fallback.
       LLD lets an already-registered lazy provider override that weak
       external, but ignores providers from archives encountered later. */
    ld_coff_object_t *fallback_object;
    ld_coff_object_t **lazy_objects;
    size_t lazy_count;
    size_t lazy_capacity;
    uint32_t insertion_order;
    bool referenced;
};

struct ld_coff_import {
    ld_coff_object_t *object;
    char *public_name;
    char *import_name;
    char *dll_name;
    uint16_t ordinal_hint;
    uint8_t type;
    uint8_t name_type;
    uint32_t descriptor_index;
    uint32_t ilt_offset;
    uint32_t iat_offset;
    uint32_t hint_name_offset;
    uint32_t dll_name_offset;
    uint32_t thunk_offset;
    bool selected;
};

struct ld_coff_output_section {
    char *name;
    uint32_t characteristics;
    uint32_t alignment;
    ld_coff_section_t **inputs;
    size_t input_count;
    size_t input_capacity;
    uint8_t *data;
    uint32_t rva;
    uint32_t file_offset;
    uint32_t virtual_size;
    uint32_t raw_size;
    uint32_t index;
};

typedef struct {
    char *source;
    char *target;
} ld_coff_alias_t;

typedef struct {
    char *key;
    char *value;
    char *source;
} ld_coff_mismatch_t;

struct ld_coff_context {
    const ld_options_t *options;
    ld_coff_file_t **files;
    size_t file_count;
    size_t file_capacity;
    ld_coff_object_t **objects;
    size_t object_count;
    size_t object_capacity;
    ld_coff_section_t **synthetic_sections;
    size_t synthetic_count;
    size_t synthetic_capacity;
    ld_coff_global_t *globals;
    ld_coff_global_t **global_order;
    size_t global_count;
    size_t global_capacity;
    ld_coff_import_t **imports;
    size_t import_count;
    size_t import_capacity;
    ld_coff_output_section_t **outputs;
    size_t output_count;
    size_t output_capacity;
    ld_coff_alias_t *aliases;
    size_t alias_count;
    size_t alias_capacity;
    ld_coff_mismatch_t *mismatches;
    size_t mismatch_count;
    size_t mismatch_capacity;
    char **default_libraries;
    size_t default_library_count;
    size_t default_library_capacity;
    char **nodefault_libraries;
    size_t nodefault_library_count;
    size_t nodefault_library_capacity;
    char **loaded_paths;
    size_t loaded_path_count;
    size_t loaded_path_capacity;
    uint32_t *base_relocations;
    size_t base_relocation_count;
    size_t base_relocation_capacity;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint32_t size_of_headers;
    uint32_t size_of_image;
    uint32_t entry_rva;
    uint32_t next_input_order;
    int error;
};

void ld_coff_context_init(ld_coff_context_t *ctx,
                          const ld_options_t *options);
void ld_coff_context_deinit(ld_coff_context_t *ctx);
int ld_coff_fail(ld_coff_context_t *ctx, int code, const char *format, ...);
const char *ld_coff_relocation_name(uint16_t type);
int ld_coff_relocation_fail(ld_coff_context_t *ctx, int code,
                            const ld_coff_object_t *object,
                            const ld_coff_section_t *section,
                            const ld_coff_relocation_t *relocation,
                            const char *symbol_name, const char *format, ...);

void *ld_coff_grow(void *items, size_t old_count, size_t new_count,
                   size_t item_size);
char *ld_coff_strndup(const char *text, size_t length);

ld_coff_global_t *ld_coff_get_global(ld_coff_context_t *ctx,
                                     const char *name, bool create);
int ld_coff_register_lazy(ld_coff_context_t *ctx, const char *name,
                          ld_coff_object_t *object);
int ld_coff_select_object(ld_coff_context_t *ctx, ld_coff_object_t *object);
int ld_coff_resolve_archives(ld_coff_context_t *ctx);

int ld_coff_load_input(ld_coff_context_t *ctx, const char *path);
int ld_coff_load_options(ld_coff_context_t *ctx);
int ld_coff_load_default_libraries(ld_coff_context_t *ctx);
int ld_coff_parse_directives(ld_coff_context_t *ctx,
                             const ld_coff_section_t *section);

int ld_coff_build_image(ld_coff_context_t *ctx, uint8_t **image,
                        size_t *image_size);
int ld_coff_write_map(ld_coff_context_t *ctx);

#endif
