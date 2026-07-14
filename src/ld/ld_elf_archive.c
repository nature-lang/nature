#include "ld_elf_archive.h"

#include "elf_format.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Ordinary archive traversal and GNU name-table lookup follow Zig's
 * src/link/Elf/Archive.zig at commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224.  That revision does not decode
 * GNU thin archives, so the thin payload/offset rules here follow GNU ar 2.42
 * output and the GNU thin archive format instead.  See ZIG-LICENSE.txt.
 */

static bool ld_elf_archive_decimal(const char *field, size_t length,
                                   uint64_t *value) {
    if (!field || !value) return false;
    size_t first = 0U;
    while (first < length && field[first] == ' ') first++;
    size_t last = length;
    while (last > first && field[last - 1U] == ' ') last--;
    if (first == last) return false;

    uint64_t result = 0U;
    for (size_t i = first; i < last; i++) {
        unsigned char character = (unsigned char) field[i];
        if (character < '0' || character > '9') return false;
        uint64_t digit = (uint64_t) (character - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static size_t ld_elf_archive_field_length(const char *field, size_t length) {
    if (!field) return 0U;
    while (length && field[length - 1U] == ' ') length--;
    return length;
}

static bool ld_elf_archive_name_equals(const char *field, size_t length,
                                       const char *name) {
    if (!field || !name) return false;
    size_t field_length = ld_elf_archive_field_length(field, length);
    size_t name_length = strlen(name);
    return field_length == name_length &&
           memcmp(field, name, name_length) == 0;
}

static bool ld_elf_archive_has_prefix(const char *field, size_t length,
                                      const char *prefix) {
    if (!field || !prefix) return false;
    size_t field_length = ld_elf_archive_field_length(field, length);
    size_t prefix_length = strlen(prefix);
    return field_length >= prefix_length &&
           memcmp(field, prefix, prefix_length) == 0;
}

static ld_elf_archive_member_kind_t ld_elf_archive_member_kind(
        const uint8_t *name_field) {
    const char *name = (const char *) name_field;
    if (ld_elf_archive_name_equals(name, LD_ELF_AR_NAME_SIZE, "//")) {
        return LD_ELF_ARCHIVE_MEMBER_NAME_TABLE;
    }
    if (ld_elf_archive_name_equals(name, LD_ELF_AR_NAME_SIZE, "/") ||
        ld_elf_archive_name_equals(name, LD_ELF_AR_NAME_SIZE, "/SYM64/") ||
        ld_elf_archive_has_prefix(name, LD_ELF_AR_NAME_SIZE, "__.SYMDEF")) {
        return LD_ELF_ARCHIVE_MEMBER_SYMBOL_TABLE;
    }
    return LD_ELF_ARCHIVE_MEMBER_REGULAR;
}

static bool ld_elf_archive_range_ok(size_t size, size_t offset,
                                    size_t length) {
    return offset <= size && length <= size - offset;
}

ld_elf_archive_result_t ld_elf_archive_record_at(
        const uint8_t *bytes, size_t size, size_t header_offset, bool thin,
        ld_elf_archive_record_t *record) {
    if ((!bytes && size != 0U) || !record) {
        return LD_ELF_ARCHIVE_INVALID_ARGUMENT;
    }
    memset(record, 0, sizeof(*record));
    if (header_offset == size) return LD_ELF_ARCHIVE_END;
    if (!ld_elf_archive_range_ok(size, header_offset,
                                 LD_ELF_AR_HEADER_SIZE)) {
        return LD_ELF_ARCHIVE_TRUNCATED_HEADER;
    }

    const uint8_t *header = bytes + header_offset;
    if (header[58] != '`' || header[59] != '\n') {
        return LD_ELF_ARCHIVE_INVALID_DELIMITER;
    }
    uint64_t member_size_u64;
    if (!ld_elf_archive_decimal((const char *) header + 48U, 10U,
                                &member_size_u64) ||
        member_size_u64 > SIZE_MAX) {
        return LD_ELF_ARCHIVE_INVALID_SIZE;
    }

    const size_t payload_offset = header_offset + LD_ELF_AR_HEADER_SIZE;
    const size_t member_size = (size_t) member_size_u64;
    const ld_elf_archive_member_kind_t kind =
            ld_elf_archive_member_kind(header);
    const size_t field_length = ld_elf_archive_field_length(
            (const char *) header, LD_ELF_AR_NAME_SIZE);
    if (thin && kind == LD_ELF_ARCHIVE_MEMBER_REGULAR &&
        field_length >= 3U && header[0] == '#' && header[1] == '1' &&
        header[2] == '/') {
        return LD_ELF_ARCHIVE_THIN_BSD_EXTENDED_NAME;
    }

    const bool payload_embedded = !thin ||
                                  kind != LD_ELF_ARCHIVE_MEMBER_REGULAR;
    size_t next_offset = payload_offset;
    if (payload_embedded) {
        if (!ld_elf_archive_range_ok(size, payload_offset, member_size)) {
            return LD_ELF_ARCHIVE_MEMBER_OUT_OF_RANGE;
        }
        next_offset += member_size;
        if ((member_size & 1U) != 0U) {
            if (next_offset == size) {
                return LD_ELF_ARCHIVE_MISSING_ALIGNMENT_BYTE;
            }
            next_offset++;
        }
    }

    record->name_field = header;
    record->header_offset = header_offset;
    record->payload_offset = payload_offset;
    record->payload_size = member_size;
    record->next_offset = next_offset;
    record->kind = kind;
    record->payload_embedded = payload_embedded;
    return LD_ELF_ARCHIVE_OK;
}

static ld_elf_archive_result_t ld_elf_archive_copy_name(
        const char *value, size_t length, char **name) {
    if (length == 0U) return LD_ELF_ARCHIVE_EMPTY_NAME;
    if (memchr(value, '\0', length)) {
        return LD_ELF_ARCHIVE_NAME_CONTAINS_NUL;
    }
    if (length == SIZE_MAX) return LD_ELF_ARCHIVE_OUT_OF_MEMORY;
    char *copy = malloc(length + 1U);
    if (!copy) return LD_ELF_ARCHIVE_OUT_OF_MEMORY;
    memcpy(copy, value, length);
    copy[length] = '\0';
    *name = copy;
    return LD_ELF_ARCHIVE_OK;
}

static ld_elf_archive_result_t ld_elf_archive_long_name(
        const char *offset_field, size_t offset_field_length,
        const char *gnu_name_table, size_t gnu_name_table_size, char **name) {
    uint64_t offset_u64;
    if (!gnu_name_table || gnu_name_table_size == 0U) {
        return LD_ELF_ARCHIVE_NAME_TABLE_REQUIRED;
    }
    if (!ld_elf_archive_decimal(offset_field, offset_field_length,
                                &offset_u64) ||
        offset_u64 >= gnu_name_table_size) {
        return LD_ELF_ARCHIVE_INVALID_NAME_OFFSET;
    }
    size_t offset = (size_t) offset_u64;
    if (offset != 0U && gnu_name_table[offset - 1U] != '\n' &&
        gnu_name_table[offset - 1U] != '\0') {
        return LD_ELF_ARCHIVE_NAME_OFFSET_IN_ENTRY;
    }

    const char *value = gnu_name_table + offset;
    size_t available = gnu_name_table_size - offset;
    const char *newline = memchr(value, '\n', available);
    const char *nul = memchr(value, '\0', available);
    const char *end = NULL;
    if (newline && nul) {
        end = newline < nul ? newline : nul;
    } else {
        end = newline ? newline : nul;
    }
    if (!end) return LD_ELF_ARCHIVE_UNTERMINATED_NAME;
    size_t length = (size_t) (end - value);
    if (length && value[length - 1U] == '/') length--;
    return ld_elf_archive_copy_name(value, length, name);
}

ld_elf_archive_result_t ld_elf_archive_member_name(
        const uint8_t *archive_bytes, size_t archive_size,
        const ld_elf_archive_record_t *record, const char *gnu_name_table,
        size_t gnu_name_table_size, bool thin, char **name,
        size_t *object_offset, size_t *object_size) {
    if ((!archive_bytes && archive_size != 0U) || !record || !name ||
        !object_offset || !object_size || !record->name_field) {
        return LD_ELF_ARCHIVE_INVALID_ARGUMENT;
    }
    *name = NULL;
    *object_offset = record->payload_offset;
    *object_size = record->payload_size;
    if (record->kind != LD_ELF_ARCHIVE_MEMBER_REGULAR) {
        return LD_ELF_ARCHIVE_OK;
    }

    const uint8_t *name_field = record->name_field;
    size_t field_length = ld_elf_archive_field_length(
            (const char *) name_field, LD_ELF_AR_NAME_SIZE);
    if (field_length >= 3U && name_field[0] == '#' && name_field[1] == '1' &&
        name_field[2] == '/') {
        if (thin) return LD_ELF_ARCHIVE_THIN_BSD_EXTENDED_NAME;
        uint64_t name_size_u64;
        if (!ld_elf_archive_decimal((const char *) name_field + 3U,
                                    field_length - 3U, &name_size_u64) ||
            name_size_u64 > record->payload_size) {
            return LD_ELF_ARCHIVE_INVALID_SIZE;
        }
        size_t name_size = (size_t) name_size_u64;
        if (!ld_elf_archive_range_ok(archive_size, record->payload_offset,
                                     name_size)) {
            return LD_ELF_ARCHIVE_MEMBER_OUT_OF_RANGE;
        }
        const char *value =
                (const char *) archive_bytes + record->payload_offset;
        size_t length = name_size;
        while (length && value[length - 1U] == '\0') length--;
        ld_elf_archive_result_t result =
                ld_elf_archive_copy_name(value, length, name);
        if (result != LD_ELF_ARCHIVE_OK) return result;
        *object_offset += name_size;
        *object_size -= name_size;
        return LD_ELF_ARCHIVE_OK;
    }

    if (field_length >= 2U && name_field[0] == '/' &&
        name_field[1] >= '0' && name_field[1] <= '9') {
        const char *offset_field = (const char *) name_field + 1U;
        size_t offset_field_length = field_length - 1U;
        if (offset_field[offset_field_length - 1U] == '/') {
            offset_field_length--;
            while (offset_field_length &&
                   offset_field[offset_field_length - 1U] == ' ') {
                offset_field_length--;
            }
        }
        return ld_elf_archive_long_name(
                offset_field, offset_field_length, gnu_name_table,
                gnu_name_table_size, name);
    }

    size_t length = field_length;
    if (length && name_field[length - 1U] == '/') length--;
    return ld_elf_archive_copy_name((const char *) name_field, length, name);
}

static bool ld_elf_archive_path_is_absolute(const char *path) {
    if (!path || !path[0]) return false;
    if (path[0] == '/' || path[0] == '\\') return true;
    return ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

ld_elf_archive_result_t ld_elf_archive_resolve_member_path(
        const char *archive_path, const char *member_name, char **path) {
    if (!archive_path || !archive_path[0] || !member_name ||
        !member_name[0] || !path) {
        return LD_ELF_ARCHIVE_INVALID_ARGUMENT;
    }
    *path = NULL;
    size_t member_length = strlen(member_name);
    if (ld_elf_archive_path_is_absolute(member_name)) {
        return ld_elf_archive_copy_name(member_name, member_length, path);
    }

    const char *last_slash = strrchr(archive_path, '/');
    const char *last_backslash = strrchr(archive_path, '\\');
    const char *separator = last_slash;
    if (!separator || (last_backslash && last_backslash > separator)) {
        separator = last_backslash;
    }
    if (!separator) {
        return ld_elf_archive_copy_name(member_name, member_length, path);
    }

    size_t directory_length = (size_t) (separator - archive_path) + 1U;
    if (directory_length > SIZE_MAX - member_length - 1U) {
        return LD_ELF_ARCHIVE_PATH_OVERFLOW;
    }
    size_t result_length = directory_length + member_length;
    char *result = malloc(result_length + 1U);
    if (!result) return LD_ELF_ARCHIVE_OUT_OF_MEMORY;
    memcpy(result, archive_path, directory_length);
    memcpy(result + directory_length, member_name, member_length);
    result[result_length] = '\0';
    *path = result;
    return LD_ELF_ARCHIVE_OK;
}

const char *ld_elf_archive_result_string(ld_elf_archive_result_t result) {
    switch (result) {
        case LD_ELF_ARCHIVE_OK:
            return "success";
        case LD_ELF_ARCHIVE_END:
            return "end of archive";
        case LD_ELF_ARCHIVE_INVALID_ARGUMENT:
            return "invalid archive decoder argument";
        case LD_ELF_ARCHIVE_TRUNCATED_HEADER:
            return "truncated archive header";
        case LD_ELF_ARCHIVE_INVALID_DELIMITER:
            return "invalid archive header delimiter";
        case LD_ELF_ARCHIVE_INVALID_SIZE:
            return "invalid archive member size";
        case LD_ELF_ARCHIVE_MEMBER_OUT_OF_RANGE:
            return "archive member extends past end of file";
        case LD_ELF_ARCHIVE_MISSING_ALIGNMENT_BYTE:
            return "archive member is missing its alignment byte";
        case LD_ELF_ARCHIVE_THIN_BSD_EXTENDED_NAME:
            return "BSD extended names are not supported in a thin archive";
        case LD_ELF_ARCHIVE_EMPTY_NAME:
            return "empty archive member name";
        case LD_ELF_ARCHIVE_NAME_TABLE_REQUIRED:
            return "GNU archive long name has no // name table";
        case LD_ELF_ARCHIVE_INVALID_NAME_OFFSET:
            return "invalid GNU archive long-name offset";
        case LD_ELF_ARCHIVE_NAME_OFFSET_IN_ENTRY:
            return "GNU archive long-name offset points into an entry";
        case LD_ELF_ARCHIVE_UNTERMINATED_NAME:
            return "unterminated GNU archive long name";
        case LD_ELF_ARCHIVE_NAME_CONTAINS_NUL:
            return "archive member name contains an embedded NUL";
        case LD_ELF_ARCHIVE_OUT_OF_MEMORY:
            return "out of memory decoding archive";
        case LD_ELF_ARCHIVE_PATH_OVERFLOW:
            return "archive member path is too long";
    }
    return "unknown archive decoder error";
}
