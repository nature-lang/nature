#include "ld_coff_internal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

/*
 * COFF object, archive, short-import, and directive decoding follows the
 * ordinary Windows AMD64 paths in lld/COFF/InputFiles.cpp and Driver.cpp.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

static bool ld_coff_push_ptr(void ***items, size_t *count, size_t *capacity,
                             void *value) {
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2U : 16U;
        void **grown = ld_coff_grow(*items, *capacity, next, sizeof(*grown));
        if (!grown) return false;
        *items = grown;
        *capacity = next;
    }
    (*items)[(*count)++] = value;
    return true;
}

static bool ld_coff_push_string(char ***items, size_t *count,
                                size_t *capacity, const char *value,
                                bool unique_case_insensitive) {
    for (size_t i = 0; i < *count; i++) {
        int compare = unique_case_insensitive
                              ? strcasecmp((*items)[i], value)
                              : strcmp((*items)[i], value);
        if (compare == 0) return true;
    }
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2U : 8U;
        char **grown = ld_coff_grow(*items, *capacity, next, sizeof(*grown));
        if (!grown) return false;
        *items = grown;
        *capacity = next;
    }
    char *copy = ld_coff_strndup(value, strlen(value));
    if (!copy) return false;
    (*items)[(*count)++] = copy;
    return true;
}

static int ld_coff_push_file(ld_coff_context_t *ctx, ld_coff_file_t *file) {
    if (!ld_coff_push_ptr((void ***) &ctx->files, &ctx->file_count,
                          &ctx->file_capacity, file))
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "out of memory recording COFF input");
    return LD_OK;
}

static int ld_coff_push_object(ld_coff_context_t *ctx,
                               ld_coff_object_t *object) {
    if (!ld_coff_push_ptr((void ***) &ctx->objects, &ctx->object_count,
                          &ctx->object_capacity, object))
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "out of memory recording COFF object");
    return LD_OK;
}

static int ld_coff_read_file(ld_coff_context_t *ctx, const char *path,
                             ld_coff_file_t **result) {
    *result = NULL;
    int descriptor = open(path, O_RDONLY | O_BINARY);
    if (descriptor < 0)
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "cannot open COFF input '%s': %s", path,
                            strerror(errno));
    struct stat info;
    if (fstat(descriptor, &info) != 0 || info.st_size < 0) {
        int saved = errno;
        close(descriptor);
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "cannot stat COFF input '%s': %s", path,
                            strerror(saved));
    }
    if ((uintmax_t) info.st_size > SIZE_MAX) {
        close(descriptor);
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "COFF input '%s' is too large", path);
    }
    size_t size = (size_t) info.st_size;
    uint8_t *bytes = size ? malloc(size) : NULL;
    if (size && !bytes) {
        close(descriptor);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory reading '%s'",
                            path);
    }
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = read(descriptor, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            int saved = errno;
            free(bytes);
            close(descriptor);
            return ld_coff_fail(ctx, LD_IO_ERROR,
                                "cannot read COFF input '%s': %s", path,
                                count == 0 ? "unexpected end of file"
                                           : strerror(saved));
        }
        offset += (size_t) count;
    }
    close(descriptor);
    ld_coff_file_t *file = calloc(1, sizeof(*file));
    if (!file) {
        free(bytes);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    file->bytes = bytes;
    file->size = size;
    file->path = ld_coff_strndup(path, strlen(path));
    if (!file->path || ld_coff_push_file(ctx, file) != LD_OK) {
        free(file->path);
        free(file->bytes);
        free(file);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    *result = file;
    return LD_OK;
}

static bool ld_coff_has_anonymous_signature(ld_coff_view_t view) {
    uint16_t first, second;
    return ld_coff_read_u16(view, 0, &first) &&
           ld_coff_read_u16(view, 2, &second) && first == 0U &&
           second == 0xffffU;
}

static bool ld_coff_has_anonymous_magic(ld_coff_view_t view,
                                        const char magic[16]) {
    return ld_coff_has_anonymous_signature(view) &&
           ld_coff_range_ok(view, 12U, 16U) &&
           memcmp(view.bytes + 12U, magic, 16U) == 0;
}

static bool ld_coff_is_bigobj(ld_coff_view_t view) {
    return ld_coff_has_anonymous_magic(view, LD_COFF_BIGOBJ_MAGIC);
}

static bool ld_coff_is_msvc_ltcg(ld_coff_view_t view) {
    return ld_coff_has_anonymous_magic(view, LD_COFF_CL_GL_MAGIC);
}

static bool ld_coff_has_import_payload(ld_coff_view_t view) {
    uint32_t data_size;
    if (!ld_coff_read_u32(view, 12U, &data_size) || data_size < 3U ||
        !ld_coff_range_ok(view, LD_COFF_IMPORT_HEADER_SIZE, data_size))
        return false;
    const uint8_t *payload = view.bytes + LD_COFF_IMPORT_HEADER_SIZE;
    const uint8_t *first_end = memchr(payload, '\0', data_size);
    if (!first_end || first_end == payload) return false;
    size_t remaining = data_size - (size_t) (first_end - payload) - 1U;
    return remaining > 1U && memchr(first_end + 1U, '\0', remaining) != NULL;
}

static bool ld_coff_is_short_import(ld_coff_view_t view) {
    uint16_t version, machine;
    if (!ld_coff_has_anonymous_signature(view) ||
        !ld_coff_range_ok(view, 0U, LD_COFF_IMPORT_HEADER_SIZE) ||
        !ld_coff_read_u16(view, 4U, &version) ||
        !ld_coff_read_u16(view, 6U, &machine) ||
        machine != LD_COFF_MACHINE_AMD64 || ld_coff_is_bigobj(view) ||
        ld_coff_is_msvc_ltcg(view))
        return false;

    /* ImportHeader version zero is authoritative.  Recognize an otherwise
       well-formed import payload as well so a corrupt version gets a precise
       diagnostic instead of being mistaken for an anonymous COFF object. */
    return version == 0U || ld_coff_has_import_payload(view);
}

static bool ld_coff_is_bitcode(ld_coff_view_t view) {
    static const uint8_t raw[] = {'B', 'C', 0xc0, 0xde};
    static const uint8_t wrapper[] = {0xde, 0xc0, 0x17, 0x0b};
    return view.size >= 4U &&
           (memcmp(view.bytes, raw, sizeof(raw)) == 0 ||
            memcmp(view.bytes, wrapper, sizeof(wrapper)) == 0);
}

static bool ld_coff_decimal(const char *text, size_t length,
                            uint64_t *value) {
    size_t first = 0U;
    while (first < length && text[first] == ' ') first++;
    while (length > first && text[length - 1U] == ' ') length--;
    if (first == length) return false;
    uint64_t result = 0U;
    for (size_t i = first; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        unsigned digit = (unsigned) (text[i] - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

static char *ld_coff_name_from_table(ld_coff_context_t *ctx,
                                     ld_coff_view_t view,
                                     const uint8_t raw_name[8],
                                     uint64_t string_offset,
                                     uint32_t string_size,
                                     const char *display_name,
                                     bool section_name) {
    if (section_name && raw_name[0] == '/') {
        size_t length = 1U;
        while (length < 8U && raw_name[length] != '\0' &&
               raw_name[length] != ' ')
            length++;
        uint64_t offset;
        if (!ld_coff_decimal((const char *) raw_name + 1U, length - 1U,
                             &offset) ||
            offset < 4U || offset >= string_size) {
            ld_coff_fail(ctx, LD_INVALID_INPUT,
                         "%s: invalid long section-name offset",
                         display_name);
            return NULL;
        }
        const char *start = (const char *) view.bytes + string_offset + offset;
        size_t available = string_size - (size_t) offset;
        const char *end = memchr(start, '\0', available);
        if (!end) {
            ld_coff_fail(ctx, LD_INVALID_INPUT,
                         "%s: unterminated long section name", display_name);
            return NULL;
        }
        return ld_coff_strndup(start, (size_t) (end - start));
    }
    uint32_t zeroes = 1U;
    memcpy(&zeroes, raw_name, sizeof(zeroes));
    if (!section_name && zeroes == 0U) {
        uint32_t offset;
        memcpy(&offset, raw_name + 4U, sizeof(offset));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        offset = __builtin_bswap32(offset);
#endif
        if (offset < 4U || offset >= string_size) {
            ld_coff_fail(ctx, LD_INVALID_INPUT,
                         "%s: invalid long symbol-name offset", display_name);
            return NULL;
        }
        const char *start = (const char *) view.bytes + string_offset + offset;
        size_t available = string_size - offset;
        const char *end = memchr(start, '\0', available);
        if (!end) {
            ld_coff_fail(ctx, LD_INVALID_INPUT,
                         "%s: unterminated long symbol name", display_name);
            return NULL;
        }
        return ld_coff_strndup(start, (size_t) (end - start));
    }
    size_t length = 0U;
    while (length < 8U && raw_name[length] != '\0') length++;
    return ld_coff_strndup((const char *) raw_name, length);
}

static uint32_t ld_coff_section_alignment(uint32_t characteristics) {
    uint32_t encoded = (characteristics & LD_COFF_SCN_ALIGN_MASK) >> 20U;
    if (encoded == 0U) return 1U;
    if (encoded > 14U) return 1U;
    return 1U << (encoded - 1U);
}

static int ld_coff_parse_relocations(ld_coff_context_t *ctx,
                                     ld_coff_object_t *object,
                                     ld_coff_section_t *section,
                                     ld_coff_view_t view,
                                     uint32_t pointer, uint16_t count) {
    uint64_t records = count;
    uint64_t start = pointer;
    if ((section->characteristics & LD_COFF_SCN_LNK_NRELOC_OVFL) != 0U) {
        uint32_t actual;
        if (count != 0xffffU || !ld_coff_read_u32(view, pointer, &actual) ||
            actual == 0U)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s(%s): invalid relocation overflow record",
                                object->display_name, section->name);
        records = (uint64_t) actual - 1U;
        start += LD_COFF_RELOCATION_SIZE;
    }
    uint64_t byte_size;
    if (!ld_coff_mul_ok(records, LD_COFF_RELOCATION_SIZE, &byte_size) ||
        !ld_coff_range_ok(view, start, byte_size) || records > SIZE_MAX)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s(%s): relocation table is out of range",
                            object->display_name, section->name);
    if (records == 0U) return LD_OK;
    section->relocations = calloc((size_t) records,
                                  sizeof(*section->relocations));
    if (!section->relocations)
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    section->relocation_count = (size_t) records;
    for (size_t i = 0; i < section->relocation_count; i++) {
        uint64_t offset = start + i * LD_COFF_RELOCATION_SIZE;
        if (!ld_coff_read_u32(view, offset,
                              &section->relocations[i].offset) ||
            !ld_coff_read_u32(view, offset + 4U,
                              &section->relocations[i].symbol_index) ||
            !ld_coff_read_u16(view, offset + 8U,
                              &section->relocations[i].type))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s(%s): truncated relocation",
                                object->display_name, section->name);
    }
    return LD_OK;
}

static int ld_coff_parse_object(ld_coff_context_t *ctx, ld_coff_file_t *file,
                                const uint8_t *bytes, size_t size,
                                const char *display_name,
                                const char *member_name, bool lazy,
                                bool archive_member,
                                ld_coff_object_t **result) {
    *result = NULL;
    ld_coff_view_t view = {bytes, size};
    bool bigobj = ld_coff_is_bigobj(view);
    uint16_t machine = 0U, optional_size = 0U;
    uint32_t section_count = 0U, symbol_pointer = 0U, symbol_count = 0U;
    uint64_t section_table_offset;
    size_t symbol_size;
    if (bigobj) {
        uint16_t version;
        if (!ld_coff_range_ok(view, 0, LD_COFF_BIGOBJ_HEADER_SIZE) ||
            !ld_coff_read_u16(view, 4, &version) ||
            !ld_coff_read_u16(view, 6, &machine) ||
            !ld_coff_read_u32(view, 44, &section_count) ||
            !ld_coff_read_u32(view, 48, &symbol_pointer) ||
            !ld_coff_read_u32(view, 52, &symbol_count))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: truncated BigObj header", display_name);
        if (version != 2U)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: unsupported BigObj header version %u; expected version 2",
                                display_name, version);
        section_table_offset = LD_COFF_BIGOBJ_HEADER_SIZE;
        symbol_size = LD_COFF_BIGOBJ_SYMBOL_SIZE;
    } else {
        uint16_t section_count16;
        if (!ld_coff_range_ok(view, 0, LD_COFF_HEADER_SIZE) ||
            !ld_coff_read_u16(view, 0, &machine) ||
            !ld_coff_read_u16(view, 2, &section_count16) ||
            !ld_coff_read_u32(view, 8, &symbol_pointer) ||
            !ld_coff_read_u32(view, 12, &symbol_count) ||
            !ld_coff_read_u16(view, 16, &optional_size))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: truncated COFF header", display_name);
        section_count = section_count16;
        section_table_offset = LD_COFF_HEADER_SIZE + optional_size;
        symbol_size = LD_COFF_SYMBOL_SIZE;
    }
    if (machine != LD_COFF_MACHINE_AMD64)
        return ld_coff_fail(ctx, LD_UNSUPPORTED,
                            "%s: unsupported COFF machine 0x%04x; expected AMD64",
                            display_name, machine);
    if (optional_size != 0U)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: executable PE input is not accepted as an object",
                            display_name);
    uint64_t section_bytes;
    if (!ld_coff_mul_ok(section_count, LD_COFF_SECTION_HEADER_SIZE,
                        &section_bytes) ||
        !ld_coff_range_ok(view, section_table_offset, section_bytes))
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: section table is out of range", display_name);
    uint64_t symbol_bytes, string_offset;
    if (!ld_coff_mul_ok(symbol_count, symbol_size, &symbol_bytes) ||
        !ld_coff_add_ok(symbol_pointer, symbol_bytes, &string_offset) ||
        (symbol_count && symbol_pointer == 0U) ||
        ((symbol_pointer || symbol_count) &&
         !ld_coff_range_ok(view, symbol_pointer, symbol_bytes)))
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: symbol table is out of range", display_name);
    uint32_t string_size = 0U;
    if (symbol_pointer || symbol_count) {
        if (!ld_coff_read_u32(view, string_offset, &string_size) ||
            string_size < 4U ||
            !ld_coff_range_ok(view, string_offset, string_size))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: invalid COFF string table", display_name);
    }

    ld_coff_object_t *object = calloc(1, sizeof(*object));
    if (!object) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    object->file = file;
    object->bytes = bytes;
    object->size = size;
    object->display_name = ld_coff_strndup(display_name, strlen(display_name));
    object->member_name = member_name
                                  ? ld_coff_strndup(member_name,
                                                   strlen(member_name))
                                  : NULL;
    object->section_count = section_count;
    object->symbol_count = symbol_count;
    object->bigobj = bigobj;
    object->lazy = lazy;
    object->archive_member = archive_member;
    object->input_order = ctx->next_input_order++;
    object->sections = section_count
                               ? calloc(section_count, sizeof(*object->sections))
                               : NULL;
    object->symbols = symbol_count
                              ? calloc(symbol_count, sizeof(*object->symbols))
                              : NULL;
    if (!object->display_name || (member_name && !object->member_name) ||
        (section_count && !object->sections) ||
        (symbol_count && !object->symbols)) {
        free(object->display_name);
        free(object->member_name);
        free(object->sections);
        free(object->symbols);
        free(object);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    if (ld_coff_push_object(ctx, object) != LD_OK) return ctx->error;

    for (uint32_t i = 0; i < section_count; i++) {
        uint64_t offset = section_table_offset +
                          (uint64_t) i * LD_COFF_SECTION_HEADER_SIZE;
        ld_coff_section_t *section = &object->sections[i];
        section->object = object;
        section->index = i + 1U;
        /* qsort is not stable.  Give every contribution a unique order so
           repeated subsection names in one object retain COFF section-table
           order after the `$` suffix sort. */
        section->input_order = ctx->next_input_order++;
        section->name = ld_coff_name_from_table(
                ctx, view, view.bytes + offset, string_offset, string_size,
                display_name, true);
        uint32_t raw_pointer, reloc_pointer;
        uint16_t reloc_count;
        if (!section->name ||
            !ld_coff_read_u32(view, offset + 8U, &section->virtual_size) ||
            !ld_coff_read_u32(view, offset + 16U, &section->data_size) ||
            !ld_coff_read_u32(view, offset + 20U, &raw_pointer) ||
            !ld_coff_read_u32(view, offset + 24U, &reloc_pointer) ||
            !ld_coff_read_u16(view, offset + 32U, &reloc_count) ||
            !ld_coff_read_u32(view, offset + 36U,
                              &section->characteristics))
            return ctx->error ? ctx->error
                              : ld_coff_fail(ctx, LD_INVALID_INPUT,
                                             "%s: truncated section header",
                                             display_name);
        if (strcmp(section->name, ".llvm.lto") == 0 ||
            strncmp(section->name, ".llvm.lto$", 10U) == 0)
            return ld_coff_fail(
                    ctx, LD_UNSUPPORTED,
                    "%s(%s): LLVM bitcode/LTO sections are not supported; rebuild with -fno-lto",
                    display_name, section->name);
        if (section->virtual_size < section->data_size)
            section->virtual_size = section->data_size;
        section->alignment =
                ld_coff_section_alignment(section->characteristics);
        section->uninitialized =
                (section->characteristics &
                 LD_COFF_SCN_CNT_UNINITIALIZED_DATA) != 0U;
        if (section->uninitialized) {
            /* Relocatable COFF uses SizeOfRawData as the logical size of a
               BSS contribution while leaving PointerToRawData zero.  Keep
               that size in virtual_size, but do not attempt to read bytes
               that are intentionally absent from the object file. */
            section->data_size = 0U;
        } else if (section->data_size) {
            if (raw_pointer == 0U ||
                !ld_coff_range_ok(view, raw_pointer, section->data_size))
                return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                    "%s(%s): raw data is out of range",
                                    display_name, section->name);
            section->data = view.bytes + raw_pointer;
        }
        int status = ld_coff_parse_relocations(
                ctx, object, section, view, reloc_pointer, reloc_count);
        if (status != LD_OK) return status;
    }

    for (uint32_t i = 0; i < symbol_count;) {
        uint64_t offset = symbol_pointer + (uint64_t) i * symbol_size;
        ld_coff_symbol_t *symbol = &object->symbols[i];
        symbol->index = i;
        symbol->name = ld_coff_name_from_table(
                ctx, view, view.bytes + offset, string_offset, string_size,
                display_name, false);
        uint8_t aux_count;
        if (!symbol->name ||
            !ld_coff_read_u32(view, offset + 8U, &symbol->value) ||
            (bigobj
                     ? !ld_coff_read_i32(view, offset + 12U,
                                         &symbol->section_number)
                     : false))
            return ctx->error ? ctx->error
                              : ld_coff_fail(ctx, LD_INVALID_INPUT,
                                             "%s: malformed symbol table",
                                             display_name);
        if (!bigobj) {
            int16_t section16;
            if (!ld_coff_read_i16(view, offset + 12U, &section16))
                return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                    "%s: malformed symbol table",
                                    display_name);
            symbol->section_number = section16;
        }
        uint64_t type_offset = offset + (bigobj ? 16U : 14U);
        if (!ld_coff_read_u16(view, type_offset, &symbol->type) ||
            !ld_coff_range_ok(view, type_offset + 2U, 2U))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: malformed symbol table", display_name);
        symbol->storage_class = view.bytes[type_offset + 2U];
        aux_count = view.bytes[type_offset + 3U];
        symbol->aux_count = aux_count;
        if ((uint64_t) i + 1U + aux_count > symbol_count)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: auxiliary symbols extend past table",
                                display_name);
        if (symbol->section_number > 0 &&
            (uint32_t) symbol->section_number <= section_count)
            symbol->section =
                    &object->sections[(uint32_t) symbol->section_number - 1U];
        else if (symbol->section_number > 0)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: symbol '%s' has invalid section %d",
                                display_name, symbol->name,
                                symbol->section_number);

        if (aux_count && symbol->storage_class == LD_COFF_STORAGE_CLASS_STATIC &&
            symbol->section) {
            uint64_t aux = offset + symbol_size;
            uint16_t associated_low = 0U;
            uint16_t associated_high = 0U;
            ld_coff_read_u32(view, aux + 8U,
                             &symbol->section->comdat_checksum);
            /* IMAGE_AUX_SYMBOL_SECTION stores NumberLowPart at +12 and
               Selection at +14.  BigObj extends the section number with
               NumberHighPart at +16; standard COFF must ignore that field.
               See llvm/Object/COFF.h::coff_aux_section_definition. */
            ld_coff_read_u16(view, aux + 12U, &associated_low);
            if (bigobj)
                ld_coff_read_u16(view, aux + 16U, &associated_high);
            symbol->section->associative_section =
                    (uint32_t) associated_low |
                    ((uint32_t) associated_high << 16U);
            symbol->section->comdat_selection = view.bytes[aux + 14U];
        } else if (aux_count &&
                   symbol->storage_class ==
                           LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL) {
            uint64_t aux = offset + symbol_size;
            ld_coff_read_u32(view, aux, &symbol->weak_target_index);
            ld_coff_read_u32(view, aux + 4U,
                             &symbol->weak_characteristics);
        }
        for (uint32_t j = 1U; j <= aux_count; j++) {
            object->symbols[i + j].index = i + j;
            object->symbols[i + j].auxiliary = true;
        }
        i += 1U + aux_count;
    }

    for (size_t i = 0; i < object->symbol_count; i++) {
        ld_coff_symbol_t *symbol = &object->symbols[i];
        if (!symbol->auxiliary && symbol->section &&
            symbol->storage_class == LD_COFF_STORAGE_CLASS_EXTERNAL &&
            !symbol->section->comdat_key)
            symbol->section->comdat_key = symbol->name;
    }
    for (size_t i = 0; i < object->section_count; i++) {
        ld_coff_section_t *section = &object->sections[i];
        for (size_t j = 0; j < section->relocation_count; j++) {
            ld_coff_relocation_t *relocation = &section->relocations[j];
            if (relocation->symbol_index >= object->symbol_count) {
                char symbol_name[64];
                snprintf(symbol_name, sizeof(symbol_name), "<index %u>",
                         relocation->symbol_index);
                return ld_coff_relocation_fail(
                        ctx, LD_INVALID_INPUT, object, section, relocation,
                        symbol_name, "relocation references invalid symbol index");
            }
        }
    }
    *result = object;
    return LD_OK;
}

static char *ld_coff_transform_import_name(const char *public_name,
                                           uint8_t name_type,
                                           const char *export_as) {
    if (name_type == LD_COFF_IMPORT_NAME_EXPORTAS && export_as)
        return ld_coff_strndup(export_as, strlen(export_as));
    const char *start = public_name;
    if (name_type == LD_COFF_IMPORT_NAME_NOPREFIX ||
        name_type == LD_COFF_IMPORT_NAME_UNDECORATE) {
        if (*start == '_' || *start == '@' || *start == '?') start++;
    }
    size_t length = strlen(start);
    if (name_type == LD_COFF_IMPORT_NAME_UNDECORATE) {
        const char *suffix = strrchr(start, '@');
        if (suffix && suffix != start) {
            bool digits = true;
            for (const char *p = suffix + 1U; *p; p++)
                if (!isdigit((unsigned char) *p)) digits = false;
            if (digits) length = (size_t) (suffix - start);
        }
    }
    return ld_coff_strndup(start, length);
}

static int ld_coff_parse_import(ld_coff_context_t *ctx, ld_coff_file_t *file,
                                const uint8_t *bytes, size_t size,
                                const char *display_name,
                                const char *member_name, bool lazy,
                                bool archive_member,
                                ld_coff_object_t **result) {
    ld_coff_view_t view = {bytes, size};
    uint32_t data_size;
    uint16_t version, hint, type_info;
    if (!ld_coff_read_u16(view, 4U, &version) ||
        !ld_coff_read_u32(view, 12U, &data_size) ||
        !ld_coff_read_u16(view, 16U, &hint) ||
        !ld_coff_read_u16(view, 18U, &type_info) ||
        !ld_coff_range_ok(view, LD_COFF_IMPORT_HEADER_SIZE, data_size))
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: malformed short import object", display_name);
    if (version != 0U)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: unsupported short import version %u; expected version 0",
                            display_name, version);
    if ((type_info & 0xffe0U) != 0U)
        return ld_coff_fail(
                ctx, LD_INVALID_INPUT,
                "%s: unsupported short import TypeInfo bits 0x%04x; "
                "reserved mask 0xffe0 must be zero",
                display_name, type_info & 0xffe0U);
    const char *payload = (const char *) bytes + LD_COFF_IMPORT_HEADER_SIZE;
    size_t available = data_size;
    const char *first_end = memchr(payload, '\0', available);
    if (!first_end)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: missing import symbol name", display_name);
    size_t first_length = (size_t) (first_end - payload);
    const char *dll = first_end + 1U;
    available -= first_length + 1U;
    const char *dll_end = memchr(dll, '\0', available);
    if (!dll_end || first_length == 0U || dll_end == dll)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "%s: malformed import name payload", display_name);
    size_t dll_length = (size_t) (dll_end - dll);
    const char *export_as = NULL;
    if (((type_info >> 2U) & 7U) == LD_COFF_IMPORT_NAME_EXPORTAS) {
        const char *third = dll_end + 1U;
        size_t used = first_length + 1U + dll_length + 1U;
        if (used >= data_size ||
            !memchr(third, '\0', data_size - used))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: missing EXPORTAS name", display_name);
        export_as = third;
    }
    uint8_t type = (uint8_t) (type_info & 3U);
    uint8_t name_type = (uint8_t) ((type_info >> 2U) & 7U);
    if (type > LD_COFF_IMPORT_CONST || name_type > LD_COFF_IMPORT_NAME_EXPORTAS)
        return ld_coff_fail(ctx, LD_UNSUPPORTED,
                            "%s: unsupported short import type", display_name);

    ld_coff_object_t *object = calloc(1, sizeof(*object));
    ld_coff_import_t *import = calloc(1, sizeof(*import));
    if (!object || !import) {
        free(object);
        free(import);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    object->file = file;
    object->bytes = bytes;
    object->size = size;
    object->display_name = ld_coff_strndup(display_name, strlen(display_name));
    object->member_name = member_name
                                  ? ld_coff_strndup(member_name,
                                                   strlen(member_name))
                                  : NULL;
    object->input_order = ctx->next_input_order++;
    object->lazy = lazy;
    object->archive_member = archive_member;
    object->import_object = true;
    object->import = import;
    import->object = object;
    import->public_name = ld_coff_strndup(payload, first_length);
    import->dll_name = ld_coff_strndup(dll, dll_length);
    import->import_name = ld_coff_transform_import_name(
            import->public_name, name_type, export_as);
    import->ordinal_hint = hint;
    import->type = type;
    import->name_type = name_type;
    if (!object->display_name || (member_name && !object->member_name) ||
        !import->public_name || !import->dll_name || !import->import_name ||
        ld_coff_push_object(ctx, object) != LD_OK)
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    *result = object;
    return LD_OK;
}

static int ld_coff_register_object_lazy_symbols(ld_coff_context_t *ctx,
                                                ld_coff_object_t *object) {
    if (object->import_object) {
        int status = ld_coff_register_lazy(ctx, object->import->public_name,
                                           object);
        if (status != LD_OK) return status;
        size_t length = strlen(object->import->public_name);
        char *iat = malloc(length + 7U);
        if (!iat) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        memcpy(iat, "__imp_", 6U);
        memcpy(iat + 6U, object->import->public_name, length + 1U);
        status = ld_coff_register_lazy(ctx, iat, object);
        free(iat);
        return status;
    }
    for (size_t i = 0; i < object->symbol_count; i++) {
        ld_coff_symbol_t *symbol = &object->symbols[i];
        if (symbol->auxiliary || !symbol->name || !*symbol->name ||
            (symbol->storage_class != LD_COFF_STORAGE_CLASS_EXTERNAL &&
             symbol->storage_class !=
                     LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL))
            continue;
        if (symbol->storage_class == LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL ||
            symbol->section_number > 0 ||
            (symbol->section_number == LD_COFF_SYM_UNDEFINED &&
             symbol->value != 0U) ||
            symbol->section_number == LD_COFF_SYM_ABSOLUTE) {
            int status = ld_coff_register_lazy(ctx, symbol->name, object);
            if (status != LD_OK) return status;
        }
    }
    return LD_OK;
}

static int ld_coff_parse_member(ld_coff_context_t *ctx, ld_coff_file_t *file,
                                const uint8_t *bytes, size_t size,
                                const char *display_name,
                                const char *member_name, bool lazy,
                                bool archive_member) {
    ld_coff_view_t view = {bytes, size};
    if (ld_coff_is_bitcode(view))
        return ld_coff_fail(ctx, LD_UNSUPPORTED,
                            "%s: LLVM bitcode/LTO is not supported; rebuild the library with -fno-lto",
                            display_name);
    if (ld_coff_is_msvc_ltcg(view))
        return ld_coff_fail(
                ctx, LD_UNSUPPORTED,
                "%s: MSVC LTCG object is not native COFF; recompile with /GL- or without /GL",
                display_name);
    ld_coff_object_t *object = NULL;
    int status;
    if (ld_coff_is_short_import(view)) {
        status = ld_coff_parse_import(ctx, file, bytes, size, display_name,
                                      member_name, lazy, archive_member,
                                      &object);
    } else if (ld_coff_is_bigobj(view) ||
               !ld_coff_has_anonymous_signature(view)) {
        status = ld_coff_parse_object(ctx, file, bytes, size, display_name,
                                      member_name, lazy, archive_member,
                                      &object);
    } else {
        return ld_coff_fail(
                ctx, LD_UNSUPPORTED,
                "%s: unsupported anonymous COFF object; expected BigObj "
                "version 2 or short import version 0",
                display_name);
    }
    if (status != LD_OK) return status;
    if (lazy) return ld_coff_register_object_lazy_symbols(ctx, object);
    return ld_coff_select_object(ctx, object);
}

static size_t ld_coff_trim_archive_field(const uint8_t *field,
                                         size_t length) {
    while (length && field[length - 1U] == ' ') length--;
    return length;
}

static bool ld_coff_archive_special(const uint8_t *name, size_t length) {
    return (length == 1U && name[0] == '/') ||
           (length == 2U && name[0] == '/' && name[1] == '/') ||
           (length == 7U && memcmp(name, "/SYM64/", 7U) == 0) ||
           (length >= 9U && memcmp(name, "__.SYMDEF", 9U) == 0);
}

static int ld_coff_parse_archive(ld_coff_context_t *ctx, ld_coff_file_t *file) {
    ld_coff_view_t view = {file->bytes, file->size};
    const char *long_names = NULL;
    size_t long_names_size = 0U;
    size_t offset = LD_COFF_ARCHIVE_MAGIC_SIZE;
    while (offset < file->size) {
        if (!ld_coff_range_ok(view, offset, LD_COFF_ARCHIVE_HEADER_SIZE))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: truncated archive header", file->path);
        const uint8_t *header = file->bytes + offset;
        if (header[58] != '`' || header[59] != '\n')
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: invalid archive header delimiter",
                                file->path);
        uint64_t size64;
        if (!ld_coff_decimal((const char *) header + 48U, 10U, &size64) ||
            size64 > SIZE_MAX)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: invalid archive member size", file->path);
        size_t payload_offset = offset + LD_COFF_ARCHIVE_HEADER_SIZE;
        size_t payload_size = (size_t) size64;
        if (!ld_coff_range_ok(view, payload_offset, payload_size))
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: archive member exceeds file", file->path);
        size_t name_length = ld_coff_trim_archive_field(header, 16U);
        if (name_length == 2U && header[0] == '/' && header[1] == '/') {
            long_names = (const char *) file->bytes + payload_offset;
            long_names_size = payload_size;
        } else if (!ld_coff_archive_special(header, name_length)) {
            char *name = NULL;
            size_t object_offset = payload_offset;
            size_t object_size = payload_size;
            if (name_length >= 3U && header[0] == '#' && header[1] == '1' &&
                header[2] == '/') {
                uint64_t embedded;
                if (!ld_coff_decimal((const char *) header + 3U,
                                     name_length - 3U, &embedded) ||
                    embedded > object_size)
                    return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                        "%s: invalid BSD archive name",
                                        file->path);
                name = ld_coff_strndup((const char *) file->bytes + object_offset,
                                       (size_t) embedded);
                object_offset += (size_t) embedded;
                object_size -= (size_t) embedded;
            } else if (name_length >= 2U && header[0] == '/' &&
                       isdigit(header[1])) {
                uint64_t name_offset;
                if (!long_names ||
                    !ld_coff_decimal((const char *) header + 1U,
                                     name_length - 1U, &name_offset) ||
                    name_offset >= long_names_size)
                    return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                        "%s: invalid archive long name",
                                        file->path);
                const char *start = long_names + name_offset;
                size_t available = long_names_size - (size_t) name_offset;
                const char *end = memchr(start, '\n', available);
                if (!end) end = memchr(start, '\0', available);
                if (!end)
                    return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                        "%s: unterminated archive long name",
                                        file->path);
                size_t length = (size_t) (end - start);
                if (length && start[length - 1U] == '/') length--;
                name = ld_coff_strndup(start, length);
            } else {
                size_t length = name_length;
                if (length && header[length - 1U] == '/') length--;
                name = ld_coff_strndup((const char *) header, length);
            }
            if (!name) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            if (strncmp(name, "__.SYMDEF", 9U) == 0 ||
                strcmp(name, "/") == 0 || strcmp(name, "//") == 0 ||
                strcmp(name, "/SYM64/") == 0) {
                free(name);
                goto next_archive_member;
            }
            size_t display_length = strlen(file->path) + strlen(name) + 3U;
            char *display = malloc(display_length);
            if (!display) {
                free(name);
                return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            }
            snprintf(display, display_length, "%s(%s)", file->path, name);
            int status = ld_coff_parse_member(
                    ctx, file, file->bytes + object_offset, object_size,
                    display, name, true, true);
            free(display);
            free(name);
            if (status != LD_OK) return status;
        }
    next_archive_member:;
        uint64_t next = (uint64_t) payload_offset + payload_size;
        if (next & 1U) next++;
        if (next > file->size)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: missing archive alignment byte",
                                file->path);
        offset = (size_t) next;
    }
    return LD_OK;
}

static bool ld_coff_path_loaded(const ld_coff_context_t *ctx,
                                const char *path) {
    for (size_t i = 0; i < ctx->loaded_path_count; i++)
        if (strcmp(ctx->loaded_paths[i], path) == 0) return true;
    return false;
}

int ld_coff_load_input(ld_coff_context_t *ctx, const char *path) {
    if (!ctx || !path || !*path) return LD_INVALID_ARGUMENT;
    if (ld_coff_path_loaded(ctx, path)) return LD_OK;
    ld_coff_file_t *file;
    int status = ld_coff_read_file(ctx, path, &file);
    if (status != LD_OK) return status;
    if (!ld_coff_push_string(&ctx->loaded_paths, &ctx->loaded_path_count,
                             &ctx->loaded_path_capacity, path, false))
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    ld_coff_view_t view = {file->bytes, file->size};
    if (file->size >= LD_COFF_ARCHIVE_MAGIC_SIZE &&
        memcmp(file->bytes, LD_COFF_ARCHIVE_MAGIC,
               LD_COFF_ARCHIVE_MAGIC_SIZE) == 0)
        return ld_coff_parse_archive(ctx, file);
    if (ld_coff_is_bitcode(view))
        return ld_coff_fail(ctx, LD_UNSUPPORTED,
                            "%s: LLVM bitcode/LTO is not supported; rebuild with -fno-lto",
                            path);
    if (file->size >= 2U && file->bytes[0] == 'M' && file->bytes[1] == 'Z')
        return ld_coff_fail(ctx, LD_UNSUPPORTED,
                            "%s: PE images/DLLs are not linker inputs; use an import library",
                            path);
    return ld_coff_parse_member(ctx, file, file->bytes, file->size, path,
                                NULL, false, false);
}

static bool ld_coff_file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int ld_coff_search_library(ld_coff_context_t *ctx, const char *name,
                                  bool required) {
    if (!name || !*name) return LD_INVALID_ARGUMENT;
    bool explicit_name = strchr(name, '/') || strchr(name, '\\') ||
                         strstr(name, ".lib") || strstr(name, ".a");
    const char *formats[] = {"%s", "%s.lib", "lib%s.a", "lib%s.lib",
                             "%s.a"};
    size_t format_count = explicit_name ? 1U : sizeof(formats) / sizeof(*formats);
    size_t path_count = ctx->options->library_paths.count +
                        (ctx->options->sysroot && *ctx->options->sysroot ? 1U
                                                                         : 0U);
    for (size_t p = 0; p <= path_count; p++) {
        const char *directory = NULL;
        if (p < ctx->options->library_paths.count)
            directory = ctx->options->library_paths.items[p];
        else if (p == ctx->options->library_paths.count &&
                 ctx->options->sysroot && *ctx->options->sysroot)
            directory = ctx->options->sysroot;
        else if (p == path_count)
            directory = ".";
        else
            continue;
        for (size_t f = 0; f < format_count; f++) {
            int base_size = snprintf(NULL, 0, formats[f], name);
            if (base_size < 0) continue;
            size_t length = strlen(directory) + (size_t) base_size + 2U;
            char *candidate = malloc(length);
            if (!candidate)
                return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            size_t used = strlen(directory);
            memcpy(candidate, directory, used);
            if (used && candidate[used - 1U] != '/' &&
                candidate[used - 1U] != '\\')
                candidate[used++] = '/';
            snprintf(candidate + used, length - used, formats[f], name);
            if (ld_coff_file_exists(candidate)) {
                int status = ld_coff_load_input(ctx, candidate);
                free(candidate);
                return status;
            }
            free(candidate);
        }
    }
    if (!required) return LD_OK;
    return ld_coff_fail(ctx, LD_IO_ERROR,
                        "cannot find Windows library '%s'", name);
}

int ld_coff_load_options(ld_coff_context_t *ctx) {
    for (size_t i = 0; i < ctx->options->inputs.count; i++) {
        int status = ld_coff_load_input(ctx, ctx->options->inputs.items[i]);
        if (status != LD_OK) return status;
    }
    for (size_t i = 0; i < ctx->options->libraries.count; i++) {
        int status =
                ld_coff_search_library(ctx, ctx->options->libraries.items[i],
                                       true);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static void ld_coff_library_identity(const char *library, const char **start,
                                     size_t *length) {
    const char *base = library;
    for (const char *cursor = library; *cursor; cursor++)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1U;
    size_t size = strlen(base);
    if (size >= 4U && strcasecmp(base + size - 4U, ".lib") == 0)
        size -= 4U;
    else if (size >= 2U && strcasecmp(base + size - 2U, ".a") == 0)
        size -= 2U;
    *start = base;
    *length = size;
}

static bool ld_coff_library_equal(const char *left, const char *right) {
    const char *left_start, *right_start;
    size_t left_length, right_length;
    ld_coff_library_identity(left, &left_start, &left_length);
    ld_coff_library_identity(right, &right_start, &right_length);
    return left_length == right_length &&
           strncasecmp(left_start, right_start, left_length) == 0;
}

static bool ld_coff_nodefault(const ld_coff_context_t *ctx,
                              const char *library) {
    for (size_t i = 0; i < ctx->nodefault_library_count; i++) {
        if (strcmp(ctx->nodefault_libraries[i], "*") == 0 ||
            ld_coff_library_equal(ctx->nodefault_libraries[i], library))
            return true;
    }
    return false;
}

int ld_coff_load_default_libraries(ld_coff_context_t *ctx) {
    for (size_t i = 0; i < ctx->default_library_count; i++) {
        const char *library = ctx->default_libraries[i];
        if (ld_coff_nodefault(ctx, library)) continue;
        int status = ld_coff_search_library(ctx, library, true);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static char *ld_coff_unquote_value(const char *value) {
    size_t length = strlen(value);
    if (length >= 2U && value[0] == '"' && value[length - 1U] == '"')
        return ld_coff_strndup(value + 1U, length - 2U);
    return ld_coff_strndup(value, length);
}

static int ld_coff_add_alias(ld_coff_context_t *ctx, const char *value) {
    const char *equals = strchr(value, '=');
    if (!equals || equals == value || !equals[1])
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "malformed /ALTERNATENAME directive");
    if (ctx->alias_count == ctx->alias_capacity) {
        size_t next = ctx->alias_capacity ? ctx->alias_capacity * 2U : 8U;
        ld_coff_alias_t *grown = ld_coff_grow(
                ctx->aliases, ctx->alias_capacity, next, sizeof(*grown));
        if (!grown) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ctx->aliases = grown;
        ctx->alias_capacity = next;
    }
    ld_coff_alias_t *alias = &ctx->aliases[ctx->alias_count++];
    alias->source = ld_coff_strndup(value, (size_t) (equals - value));
    alias->target = ld_coff_strndup(equals + 1U, strlen(equals + 1U));
    if (!alias->source || !alias->target)
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    return LD_OK;
}

static int ld_coff_add_fail_if_mismatch(ld_coff_context_t *ctx,
                                        const char *argument,
                                        const char *source) {
    const char *equals = strchr(argument, '=');
    if (!equals || equals == argument || !equals[1])
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            "/FAILIFMISMATCH: invalid argument '%s' in %s",
                            argument, source);

    size_t key_length = (size_t) (equals - argument);
    const char *value = equals + 1U;
    for (size_t i = 0; i < ctx->mismatch_count; i++) {
        ld_coff_mismatch_t *mismatch = &ctx->mismatches[i];
        if (strlen(mismatch->key) != key_length ||
            memcmp(mismatch->key, argument, key_length) != 0)
            continue;
        if (strcmp(mismatch->value, value) != 0)
            return ld_coff_fail(
                    ctx, LD_INVALID_INPUT,
                    "/FAILIFMISMATCH: mismatch detected for '%s':\n"
                    ">>> %s has value %s\n>>> %s has value %s",
                    mismatch->key, mismatch->source, mismatch->value, source,
                    value);

        char *new_source = ld_coff_strndup(source, strlen(source));
        if (!new_source)
            return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        free(mismatch->source);
        mismatch->source = new_source;
        return LD_OK;
    }

    if (ctx->mismatch_count == ctx->mismatch_capacity) {
        size_t next = ctx->mismatch_capacity ? ctx->mismatch_capacity * 2U
                                             : 8U;
        ld_coff_mismatch_t *grown = ld_coff_grow(
                ctx->mismatches, ctx->mismatch_capacity, next,
                sizeof(*grown));
        if (!grown) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ctx->mismatches = grown;
        ctx->mismatch_capacity = next;
    }
    ld_coff_mismatch_t *mismatch = &ctx->mismatches[ctx->mismatch_count];
    mismatch->key = ld_coff_strndup(argument, key_length);
    mismatch->value = ld_coff_strndup(value, strlen(value));
    mismatch->source = ld_coff_strndup(source, strlen(source));
    if (!mismatch->key || !mismatch->value || !mismatch->source) {
        free(mismatch->key);
        free(mismatch->value);
        free(mismatch->source);
        memset(mismatch, 0, sizeof(*mismatch));
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    ctx->mismatch_count++;
    return LD_OK;
}

static int ld_coff_handle_directive(ld_coff_context_t *ctx, char *token,
                                    const char *source) {
    if (*token == '/' || *token == '-') token++;
    char *colon = strchr(token, ':');
    char *value = colon ? colon + 1U : NULL;
    size_t key_length = colon ? (size_t) (colon - token) : strlen(token);
    char *unquoted = value ? ld_coff_unquote_value(value) : NULL;
    if (value && !unquoted) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    int status = LD_OK;
    if (key_length == 10U && strncasecmp(token, "DEFAULTLIB", 10U) == 0) {
        if (!unquoted || !*unquoted ||
            !ld_coff_push_string(&ctx->default_libraries,
                                 &ctx->default_library_count,
                                 &ctx->default_library_capacity, unquoted,
                                 true))
            status = ld_coff_fail(ctx, unquoted ? LD_IO_ERROR : LD_INVALID_INPUT,
                                  "invalid /DEFAULTLIB directive");
    } else if (key_length == 12U &&
               strncasecmp(token, "NODEFAULTLIB", 12U) == 0) {
        const char *name = unquoted && *unquoted ? unquoted : "*";
        if (!ld_coff_push_string(&ctx->nodefault_libraries,
                                 &ctx->nodefault_library_count,
                                 &ctx->nodefault_library_capacity, name, true))
            status = ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    } else if (key_length == 13U &&
               strncasecmp(token, "ALTERNATENAME", 13U) == 0) {
        status = unquoted ? ld_coff_add_alias(ctx, unquoted)
                          : ld_coff_fail(ctx, LD_INVALID_INPUT,
                                         "invalid /ALTERNATENAME directive");
    } else if (key_length == 7U &&
               strncasecmp(token, "INCLUDE", 7U) == 0) {
        ld_coff_global_t *global = unquoted
                                           ? ld_coff_get_global(ctx, unquoted, true)
                                           : NULL;
        if (!global)
            status = ld_coff_fail(ctx, unquoted ? LD_IO_ERROR : LD_INVALID_INPUT,
                                  "invalid /INCLUDE directive");
        else
            global->referenced = true;
    } else if (key_length == 14U &&
               strncasecmp(token, "FAILIFMISMATCH", 14U) == 0) {
        status = unquoted
                         ? ld_coff_add_fail_if_mismatch(ctx, unquoted, source)
                         : ld_coff_fail(
                                   ctx, LD_INVALID_INPUT,
                                   "/FAILIFMISMATCH: missing argument in %s",
                                   source);
    } else if (key_length == 15U &&
               strncasecmp(token, "EXCLUDE-SYMBOLS", 15U) == 0) {
        /* Clang emits this MinGW directive for hidden definitions. It only
           controls automatic DLL exports, which do not exist for Nature's
           executable-only first phase, but accepting it is required to
           consume the controlled static libuv sysroot. */
        if (!unquoted || !*unquoted)
            status = ld_coff_fail(ctx, LD_INVALID_INPUT,
                                  "invalid -exclude-symbols directive");
    } else {
        status = ld_coff_fail(ctx, LD_UNSUPPORTED,
                              "unsupported COFF directive '%s'", token);
    }
    free(unquoted);
    return status;
}

int ld_coff_parse_directives(ld_coff_context_t *ctx,
                             const ld_coff_section_t *section) {
    size_t offset = 0U;
    while (offset < section->data_size) {
        while (offset < section->data_size &&
               isspace((unsigned char) section->data[offset]))
            offset++;
        if (offset == section->data_size) break;
        size_t start = offset;
        bool quoted = false;
        while (offset < section->data_size) {
            char character = (char) section->data[offset];
            if (character == '"') quoted = !quoted;
            if (!quoted && isspace((unsigned char) character)) break;
            offset++;
        }
        if (quoted)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                "%s: unterminated quote in .drectve",
                                section->object->display_name);
        char *token = ld_coff_strndup((const char *) section->data + start,
                                      offset - start);
        if (!token) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        const char *source = section->object &&
                                     section->object->display_name
                                     ? section->object->display_name
                                     : "<unknown COFF input>";
        int status = ld_coff_handle_directive(ctx, token, source);
        free(token);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}
