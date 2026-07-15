#ifndef NATURE_LD_ELF_ARCHIVE_H
#define NATURE_LD_ELF_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Checked GNU/BSD archive wire decoding shared by ordinary and GNU thin
 * archives.  The lazy-object policy remains in ld_elf_input.c; this module is
 * deliberately independent of the linker context and diagnostics layer.
 */

typedef enum {
    LD_ELF_ARCHIVE_OK = 0,
    LD_ELF_ARCHIVE_END,
    LD_ELF_ARCHIVE_INVALID_ARGUMENT,
    LD_ELF_ARCHIVE_TRUNCATED_HEADER,
    LD_ELF_ARCHIVE_INVALID_DELIMITER,
    LD_ELF_ARCHIVE_INVALID_SIZE,
    LD_ELF_ARCHIVE_MEMBER_OUT_OF_RANGE,
    LD_ELF_ARCHIVE_MISSING_ALIGNMENT_BYTE,
    LD_ELF_ARCHIVE_THIN_BSD_EXTENDED_NAME,
    LD_ELF_ARCHIVE_EMPTY_NAME,
    LD_ELF_ARCHIVE_NAME_TABLE_REQUIRED,
    LD_ELF_ARCHIVE_INVALID_NAME_OFFSET,
    LD_ELF_ARCHIVE_NAME_OFFSET_IN_ENTRY,
    LD_ELF_ARCHIVE_UNTERMINATED_NAME,
    LD_ELF_ARCHIVE_NAME_CONTAINS_NUL,
    LD_ELF_ARCHIVE_OUT_OF_MEMORY,
    LD_ELF_ARCHIVE_PATH_OVERFLOW,
} ld_elf_archive_result_t;

typedef enum {
    LD_ELF_ARCHIVE_MEMBER_REGULAR = 0,
    LD_ELF_ARCHIVE_MEMBER_SYMBOL_TABLE,
    LD_ELF_ARCHIVE_MEMBER_NAME_TABLE,
} ld_elf_archive_member_kind_t;

typedef struct {
    const uint8_t *name_field;
    size_t header_offset;
    size_t payload_offset;
    size_t payload_size;
    size_t next_offset;
    ld_elf_archive_member_kind_t kind;
    bool payload_embedded;
} ld_elf_archive_record_t;

ld_elf_archive_result_t ld_elf_archive_record_at(
        const uint8_t *bytes, size_t size, size_t header_offset, bool thin,
        ld_elf_archive_record_t *record);

ld_elf_archive_result_t ld_elf_archive_member_name(
        const uint8_t *archive_bytes, size_t archive_size,
        const ld_elf_archive_record_t *record, const char *gnu_name_table,
        size_t gnu_name_table_size, bool thin, char **name,
        size_t *object_offset, size_t *object_size);

ld_elf_archive_result_t ld_elf_archive_resolve_member_path(
        const char *archive_path, const char *member_name, char **path);

const char *ld_elf_archive_result_string(ld_elf_archive_result_t result);

#endif
