#ifndef NATURE_BINARY_COFF_WRITER_H
#define NATURE_BINARY_COFF_WRITER_H

/*
 * Portable AMD64 COFF relocatable-object builder.
 *
 * The wire layout and auxiliary-record encodings follow LLVM's
 * WinCOFFObjectWriter and llvm/Object/COFF definitions.
 *
 * Sources:
 *   llvm/lib/MC/WinCOFFObjectWriter.cpp
 *   llvm/include/llvm/Object/COFF.h
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "src/ld/coff_format.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct coff_object coff_object_t;
typedef struct coff_section coff_section_t;

typedef enum {
    COFF_WRITER_OK = 0,
    COFF_WRITER_INVALID_ARGUMENT,
    COFF_WRITER_OUT_OF_MEMORY,
    COFF_WRITER_OVERFLOW,
    COFF_WRITER_DUPLICATE,
    COFF_WRITER_INVALID_STATE,
    COFF_WRITER_UNSUPPORTED,
    COFF_WRITER_IO_ERROR,
} coff_writer_status_t;

enum {
    /* IMAGE_SYM_DTYPE_FUNCTION << 4. */
    COFF_SYMBOL_TYPE_FUNCTION = 0x20,
    COFF_SYMBOL_INDEX_NONE = UINT32_MAX,
};

typedef struct {
    const char *name;
    uint32_t value;
    int32_t section_number;
    uint16_t type;
    uint8_t storage_class;
    /* aux_records points to aux_count consecutive 18-byte records. */
    const uint8_t *aux_records;
    uint8_t aux_count;
} coff_symbol_desc_t;

/*
 * Creates an empty standard (18-byte symbol) AMD64 COFF object builder.
 * source_name is copied for diagnostics and may be NULL.
 */
coff_object_t *coff_object_create(const char *source_name);

/*
 * Creates an AMD64 object with .text, .rdata, .data, and .bss in that order.
 * If source_name is non-empty, a standard .file symbol is also emitted.
 */
coff_object_t *coff_object_create_amd64(const char *source_name);

void coff_object_destroy(coff_object_t *object);

const char *coff_object_last_error(const coff_object_t *object);
const char *coff_writer_status_string(coff_writer_status_t status);

coff_writer_status_t coff_object_add_standard_sections(
        coff_object_t *object);

coff_writer_status_t coff_object_add_section(
        coff_object_t *object, const char *name, uint32_t characteristics,
        uint32_t alignment, coff_section_t **result);

coff_section_t *coff_object_find_section(const coff_object_t *object,
                                         const char *name);
coff_section_t *coff_object_text(const coff_object_t *object);
coff_section_t *coff_object_rdata(const coff_object_t *object);
coff_section_t *coff_object_data(const coff_object_t *object);
coff_section_t *coff_object_bss(const coff_object_t *object);

uint32_t coff_section_index(const coff_section_t *section);
const char *coff_section_name(const coff_section_t *section);
uint32_t coff_section_characteristics(const coff_section_t *section);
uint32_t coff_section_alignment(const coff_section_t *section);
uint32_t coff_section_size(const coff_section_t *section);
size_t coff_section_relocation_count(const coff_section_t *section);

/*
 * Appends bytes after aligning the section cursor. For an uninitialized-data
 * section, data must be NULL and only the virtual size is increased.
 */
coff_writer_status_t coff_section_append(
        coff_section_t *section, const void *data, size_t size,
        uint32_t alignment, uint32_t *offset);

coff_writer_status_t coff_section_append_zeros(
        coff_section_t *section, size_t size, uint32_t alignment,
        uint32_t *offset);

coff_writer_status_t coff_section_write(
        coff_section_t *section, uint32_t offset, const void *data,
        size_t size);

/*
 * Adds one raw symbol and its already-encoded auxiliary records. The returned
 * index is the raw COFF symbol-table record index, including preceding aux
 * records, and is therefore directly usable by a relocation.
 */
coff_writer_status_t coff_object_add_symbol(
        coff_object_t *object, const coff_symbol_desc_t *symbol,
        uint32_t *symbol_index);

coff_writer_status_t coff_object_add_defined_symbol(
        coff_object_t *object, const char *name, coff_section_t *section,
        uint32_t value, uint16_t type, uint8_t storage_class,
        uint32_t *symbol_index);

/*
 * Defines an interned symbol. If a matching undefined symbol reference was
 * created earlier, its record is updated in place so existing relocation
 * indices remain valid.
 */
coff_writer_status_t coff_object_define_symbol(
        coff_object_t *object, const char *name, coff_section_t *section,
        uint32_t value, uint16_t type, uint8_t storage_class,
        uint32_t *symbol_index);

/*
 * Returns an existing symbol with the same name or creates an undefined
 * reference. external=false creates a static placeholder suitable for a
 * forward local label; it must be defined before serialization.
 */
coff_writer_status_t coff_object_get_or_add_symbol_reference(
        coff_object_t *object, const char *name, bool external, uint16_t type,
        uint32_t *symbol_index);

bool coff_object_find_symbol(const coff_object_t *object, const char *name,
                             uint32_t *symbol_index);
size_t coff_object_symbol_record_count(const coff_object_t *object);

/* Emits the COFF .file symbol and as many 18-byte aux records as necessary. */
coff_writer_status_t coff_object_add_file_symbol(coff_object_t *object,
                                                 const char *file_name,
                                                 uint32_t *symbol_index);

/* Emits an undefined weak external plus its fallback aux record. */
coff_writer_status_t coff_object_add_weak_external(
        coff_object_t *object, const char *name,
        uint32_t fallback_symbol_index, uint32_t search_characteristics,
        uint32_t *symbol_index);

/*
 * Marks section as COMDAT and emits its static section-definition symbol.
 * Non-associative COMDATs require a non-empty leader_name and receive an
 * external leader symbol immediately after the section symbol. Associative
 * COMDATs require associative_parent and do not require a leader.
 */
coff_writer_status_t coff_object_mark_comdat(
        coff_object_t *object, coff_section_t *section, uint8_t selection,
        coff_section_t *associative_parent, const char *leader_name,
        uint32_t checksum, uint32_t *section_symbol_index,
        uint32_t *leader_symbol_index);

/*
 * Adds a relocation without changing the implicit addend already encoded in
 * the relocated field.
 */
coff_writer_status_t coff_section_add_relocation(
        coff_section_t *section, uint32_t offset, uint32_t symbol_index,
        uint16_t type);

/*
 * Encodes implicit_addend into the relocation field in little-endian order,
 * then adds the relocation. This is the preferred API for pointer metadata
 * and freshly-zeroed AMD64 displacement fields.
 */
coff_writer_status_t coff_section_add_relocation_with_addend(
        coff_section_t *section, uint32_t offset, uint32_t symbol_index,
        uint16_t type, int64_t implicit_addend);

/* Allocates a deterministic in-memory COFF image. Caller owns *image. */
coff_writer_status_t coff_object_serialize(const coff_object_t *object,
                                           uint8_t **image,
                                           size_t *image_size);

coff_writer_status_t coff_object_write_file(const coff_object_t *object,
                                            const char *path);

#ifdef __cplusplus
}
#endif

#endif
