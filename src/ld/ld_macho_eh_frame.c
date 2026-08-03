#include "ld_macho_eh_frame.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define LD_DW_EH_PE_OMIT 0xffU
#define LD_DW_EH_PE_ABSPTR 0x00U
#define LD_DW_EH_PE_ULEB128 0x01U
#define LD_DW_EH_PE_UDATA2 0x02U
#define LD_DW_EH_PE_UDATA4 0x03U
#define LD_DW_EH_PE_UDATA8 0x04U
#define LD_DW_EH_PE_SLEB128 0x09U
#define LD_DW_EH_PE_SDATA2 0x0aU
#define LD_DW_EH_PE_SDATA4 0x0bU
#define LD_DW_EH_PE_SDATA8 0x0cU
#define LD_DW_EH_PE_PCREL 0x10U
#define LD_DW_EH_PE_INDIRECT 0x80U
#define LD_DW_EH_PE_FORMAT_MASK 0x0fU
#define LD_DW_EH_PE_APPLICATION_MASK 0x70U

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint8_t fde_encoding;
    uint8_t lsda_encoding;
    uint32_t personality_symbol_index;
    bool has_z_augmentation;
    bool has_personality;
} ld_macho_cie_t;

typedef struct {
    ld_macho_cie_t *items;
    size_t count;
    size_t capacity;
} ld_macho_cie_list_t;

static uint16_t ld_eh_read_u16(const uint8_t *bytes) {
    uint16_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint32_t ld_eh_read_u32(const uint8_t *bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint64_t ld_eh_read_u64(const uint8_t *bytes) {
    uint64_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static bool ld_eh_name_is(const char name[16], const char *expected) {
    size_t length = strlen(expected);
    return length <= 16U && memcmp(name, expected, length) == 0 &&
           (length == 16U || name[length] == '\0');
}

static int ld_eh_cie_push(ld_context_t *ctx, ld_macho_cie_list_t *list,
                          ld_macho_cie_t cie) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 8U;
        if (next < list->capacity) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "too many Mach-O __eh_frame CIE records");
        }
        ld_macho_cie_t *items = ld_realloc_array(
                list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "out of memory parsing Mach-O __eh_frame CIEs");
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = cie;
    return LD_OK;
}

static int ld_eh_fde_push(ld_context_t *ctx, ld_macho_fde_list_t *list,
                          ld_macho_fde_t fde) {
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 16U;
        if (next < list->capacity) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "too many Mach-O __eh_frame FDE records");
        }
        ld_macho_fde_t *items = ld_realloc_array(
                list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "out of memory parsing Mach-O __eh_frame FDEs");
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = fde;
    return LD_OK;
}

static int ld_eh_uleb(ld_context_t *ctx, ld_object_t *object,
                      const uint8_t *data, size_t size, size_t *cursor,
                      uint64_t *value) {
    uint64_t result = 0;
    unsigned shift = 0;
    for (unsigned count = 0; count < 10U; count++) {
        if (*cursor >= size) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "truncated ULEB128 in __eh_frame from '%s'%s%s",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        uint8_t byte = data[(*cursor)++];
        uint64_t payload = byte & 0x7fU;
        if (shift >= 64U || payload > (UINT64_MAX >> shift)) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "ULEB128 overflow in __eh_frame from '%s'%s%s",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        result |= payload << shift;
        if ((byte & 0x80U) == 0) {
            *value = result;
            return LD_OK;
        }
        shift += 7U;
    }
    return ld_fail(ctx, LD_INVALID_INPUT,
                   "overlong ULEB128 in __eh_frame from '%s'%s%s",
                   object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static int ld_eh_sleb(ld_context_t *ctx, ld_object_t *object,
                      const uint8_t *data, size_t size, size_t *cursor,
                      int64_t *value) {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    for (unsigned count = 0; count < 10U; count++) {
        if (*cursor >= size) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "truncated SLEB128 in __eh_frame from '%s'%s%s",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        byte = data[(*cursor)++];
        uint64_t payload = byte & 0x7fU;
        if (shift < 64U) result |= payload << shift;
        shift += 7U;
        if ((byte & 0x80U) == 0) {
            if (shift < 64U && (byte & 0x40U) != 0) {
                result |= UINT64_MAX << shift;
            } else if (shift >= 64U) {
                bool negative = (byte & 0x40U) != 0;
                uint8_t required = negative ? 0x7fU : 0U;
                if (payload != required && count == 9U) {
                    return ld_fail(ctx, LD_INVALID_INPUT,
                                   "SLEB128 overflow in __eh_frame from '%s'%s%s",
                                   object->file->path,
                                   object->member_name ? " member " : "",
                                   object->member_name ? object->member_name : "");
                }
            }
            *value = (int64_t) result;
            return LD_OK;
        }
    }
    return ld_fail(ctx, LD_INVALID_INPUT,
                   "overlong SLEB128 in __eh_frame from '%s'%s%s",
                   object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static bool ld_eh_decode_relocation(const uint8_t *raw,
                                    ld_relocation_t *relocation) {
    uint32_t address = ld_eh_read_u32(raw);
    uint32_t word = ld_eh_read_u32(raw + 4U);
    if ((address & 0x80000000U) != 0) return false;
    relocation->address = address;
    relocation->symbolnum = word & 0x00ffffffU;
    relocation->pcrel = (uint8_t) ((word >> 24U) & 1U);
    relocation->length = (uint8_t) ((word >> 25U) & 3U);
    relocation->external = (uint8_t) ((word >> 27U) & 1U);
    relocation->type = (uint8_t) ((word >> 28U) & 0xfU);
    return true;
}

static int ld_eh_relocation_input_value(ld_context_t *ctx,
                                        ld_object_t *object,
                                        const ld_relocation_t *relocation,
                                        uint64_t *value) {
    if (!relocation->external) {
        if (relocation->symbolnum == 0 ||
            relocation->symbolnum > object->section_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "__eh_frame relocation has invalid section %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        *value = object->sections[relocation->symbolnum - 1U].header.addr;
        return LD_OK;
    }
    if (relocation->symbolnum >= object->symbol_count) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
                       "__eh_frame relocation has invalid symbol %u in '%s'%s%s",
                       relocation->symbolnum, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    const ld_input_symbol_t *symbol = &object->symbols[relocation->symbolnum];
    uint8_t type = symbol->entry.n_type & LD_N_TYPE;
    if (type != LD_N_SECT && type != LD_N_ABS) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
                       "__eh_frame relocation references unresolved symbol '%s' in '%s'%s%s",
                       symbol->name ? symbol->name : "<unnamed>",
                       object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    *value = symbol->entry.n_value;
    return LD_OK;
}

static bool ld_eh_read_fixed(const uint8_t *data, size_t size, size_t offset,
                             uint8_t encoding, uint64_t *unsigned_value,
                             int64_t *signed_value, size_t *width) {
    uint8_t format = encoding & LD_DW_EH_PE_FORMAT_MASK;
    switch (format) {
        case LD_DW_EH_PE_ABSPTR:
        case LD_DW_EH_PE_UDATA8:
        case LD_DW_EH_PE_SDATA8:
            *width = 8U;
            break;
        case LD_DW_EH_PE_UDATA4:
        case LD_DW_EH_PE_SDATA4:
            *width = 4U;
            break;
        case LD_DW_EH_PE_UDATA2:
        case LD_DW_EH_PE_SDATA2:
            *width = 2U;
            break;
        default:
            return false;
    }
    if (offset > size || *width > size - offset) return false;
    uint64_t raw;
    if (*width == 8U) {
        raw = ld_eh_read_u64(data + offset);
    } else if (*width == 4U) {
        raw = ld_eh_read_u32(data + offset);
    } else {
        raw = ld_eh_read_u16(data + offset);
    }
    *unsigned_value = raw;
    if (format == LD_DW_EH_PE_SDATA8) {
        *signed_value = (int64_t) raw;
    } else if (format == LD_DW_EH_PE_SDATA4) {
        *signed_value = (int32_t) raw;
    } else if (format == LD_DW_EH_PE_SDATA2) {
        *signed_value = (int16_t) raw;
    } else if (*width == 8U) {
        *signed_value = (int64_t) raw;
    } else {
        *signed_value = (int64_t) raw;
    }
    return true;
}

/* Resolve an encoded pcrel field in input-address space.  Apple arm64
 * objects generally express FDE PC/LSDA fields as SUBTRACTOR+UNSIGNED pairs;
 * the fallback also accepts an already-resolved field without relocations. */
static int ld_eh_resolve_pointer(ld_context_t *ctx, ld_object_t *object,
                                 const ld_input_section_t *section,
                                 uint32_t field_offset, uint8_t encoding,
                                 uint64_t *target, size_t *field_width) {
    uint64_t raw_unsigned;
    int64_t raw_signed;
    if (!ld_eh_read_fixed(section->data, (size_t) section->header.size,
                          field_offset, encoding, &raw_unsigned, &raw_signed,
                          field_width)) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "unsupported or truncated pointer encoding 0x%x at __eh_frame offset 0x%x in '%s'%s%s",
                       encoding, field_offset, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }

    uint8_t application = encoding & LD_DW_EH_PE_APPLICATION_MASK;
    uint8_t format = encoding & LD_DW_EH_PE_FORMAT_MASK;
    bool signed_format = format == LD_DW_EH_PE_SDATA2 ||
                         format == LD_DW_EH_PE_SDATA4 ||
                         format == LD_DW_EH_PE_SDATA8;
    __int128 relocated =
            application == LD_DW_EH_PE_PCREL || signed_format
                    ? (__int128) raw_signed
                    : (__int128) raw_unsigned;
    uint8_t expected_length = *field_width == 8U
                                      ? 3U
                              : *field_width == 4U ? 2U
                                                   : 1U;
    for (uint32_t index = 0; index < section->header.nreloc; index++) {
        ld_relocation_t relocation;
        if (!ld_eh_decode_relocation(
                    section->relocations + (size_t) index * 8U,
                    &relocation)) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "scattered __eh_frame relocation in '%s'%s%s is unsupported",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        if (relocation.address != field_offset) continue;
        if (relocation.type == LD_ARM64_RELOC_SUBTRACTOR) {
            if (index + 1U >= section->header.nreloc) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "unpaired __eh_frame subtractor relocation at offset 0x%x in '%s'%s%s",
                               field_offset, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            ld_relocation_t minuend;
            if (!ld_eh_decode_relocation(
                        section->relocations + (size_t) (index + 1U) * 8U,
                        &minuend) ||
                minuend.address != field_offset ||
                minuend.type != LD_ARM64_RELOC_UNSIGNED ||
                minuend.length != relocation.length ||
                minuend.external != relocation.external || minuend.pcrel ||
                relocation.pcrel || relocation.length != expected_length) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "invalid __eh_frame subtractor pair at offset 0x%x in '%s'%s%s",
                               field_offset, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            uint64_t subtrahend_value;
            uint64_t minuend_value;
            int status = ld_eh_relocation_input_value(
                    ctx, object, &relocation, &subtrahend_value);
            if (status != LD_OK) return status;
            status = ld_eh_relocation_input_value(
                    ctx, object, &minuend, &minuend_value);
            if (status != LD_OK) return status;
            relocated += (__int128) minuend_value - subtrahend_value;
            break;
        }
        if (relocation.type == LD_ARM64_RELOC_UNSIGNED) {
            if (relocation.length != expected_length || relocation.pcrel) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "__eh_frame relocation width does not match its encoded pointer at offset 0x%x in '%s'%s%s",
                               field_offset, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            uint64_t symbol_value;
            int status = ld_eh_relocation_input_value(
                    ctx, object, &relocation, &symbol_value);
            if (status != LD_OK) return status;
            relocated += symbol_value;
            break;
        }
    }

    if (application == LD_DW_EH_PE_PCREL) {
        __int128 place = (__int128) section->header.addr + field_offset;
        relocated += place;
    } else if (application != 0U) {
        return ld_fail(ctx, LD_UNSUPPORTED,
                       "unsupported __eh_frame pointer application 0x%x in '%s'%s%s",
                       application, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    if ((encoding & LD_DW_EH_PE_INDIRECT) != 0U) {
        return ld_fail(ctx, LD_UNSUPPORTED,
                       "indirect __eh_frame PC/LSDA pointer in '%s'%s%s is unsupported",
                       object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    if (relocated < 0 || relocated > UINT64_MAX) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
                       "__eh_frame pointer at offset 0x%x overflows in '%s'%s%s",
                       field_offset, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    *target = (uint64_t) relocated;
    return LD_OK;
}

static int ld_eh_personality_symbol(ld_context_t *ctx, ld_object_t *object,
                                    const ld_input_section_t *section,
                                    uint32_t field_offset,
                                    uint32_t *symbol_index) {
    for (uint32_t index = 0; index < section->header.nreloc; index++) {
        ld_relocation_t relocation;
        if (!ld_eh_decode_relocation(
                    section->relocations + (size_t) index * 8U,
                    &relocation)) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "scattered __eh_frame relocation in '%s'%s%s is unsupported",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        if (relocation.address != field_offset) continue;
        if (relocation.type != LD_ARM64_RELOC_POINTER_TO_GOT ||
            relocation.length != 2U || !relocation.pcrel ||
            !relocation.external ||
            relocation.symbolnum >= object->symbol_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "invalid __eh_frame personality relocation at offset 0x%x in '%s'%s%s",
                           field_offset, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        *symbol_index = relocation.symbolnum;
        return LD_OK;
    }
    return ld_fail(ctx, LD_RELOCATION_ERROR,
                   "missing __eh_frame personality relocation at offset 0x%x in '%s'%s%s",
                   field_offset, object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static int ld_eh_skip_encoded(ld_context_t *ctx, ld_object_t *object,
                              const uint8_t *data, size_t size, size_t *cursor,
                              uint8_t encoding, uint32_t *field_offset) {
    if (encoding == LD_DW_EH_PE_OMIT) return LD_OK;
    if (field_offset) {
        if (*cursor > UINT32_MAX) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "__eh_frame encoded pointer offset is too large in '%s'%s%s",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        *field_offset = (uint32_t) *cursor;
    }
    uint8_t format = encoding & LD_DW_EH_PE_FORMAT_MASK;
    uint64_t ignored_unsigned;
    int64_t ignored_signed;
    if (format == LD_DW_EH_PE_ULEB128) {
        return ld_eh_uleb(ctx, object, data, size, cursor,
                          &ignored_unsigned);
    }
    if (format == LD_DW_EH_PE_SLEB128) {
        return ld_eh_sleb(ctx, object, data, size, cursor, &ignored_signed);
    }
    size_t width;
    if (!ld_eh_read_fixed(data, size, *cursor, encoding, &ignored_unsigned,
                          &ignored_signed, &width)) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "unsupported or truncated __eh_frame pointer encoding 0x%x in '%s'%s%s",
                       encoding, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    *cursor += width;
    return LD_OK;
}

static int ld_eh_parse_cie(ld_context_t *ctx, ld_object_t *object,
                           const ld_input_section_t *section, uint32_t offset,
                           uint32_t total_size, ld_macho_cie_t *cie) {
    const uint8_t *data = section->data + offset;
    size_t cursor = 8U;
    if (total_size < 10U) goto malformed;
    uint8_t version = data[cursor++];
    /* Apple arm64 Mach-O uses DWARF CFI version 1.  Later CIE versions have
       additional header fields and must not be misparsed as version 1. */
    if (version != 1U) goto malformed;
    size_t augmentation_start = cursor;
    while (cursor < total_size && data[cursor] != 0) cursor++;
    if (cursor == total_size) goto malformed;
    size_t augmentation_size = cursor - augmentation_start;
    cursor++;
    uint64_t ignored_u;
    int64_t ignored_s;
    int status = ld_eh_uleb(ctx, object, data, total_size, &cursor,
                            &ignored_u);
    if (status != LD_OK) return status;
    status = ld_eh_sleb(ctx, object, data, total_size, &cursor, &ignored_s);
    if (status != LD_OK) return status;
    status = ld_eh_uleb(ctx, object, data, total_size, &cursor,
                        &ignored_u);
    if (status != LD_OK) return status;

    memset(cie, 0, sizeof(*cie));
    cie->offset = offset;
    cie->size = total_size;
    cie->fde_encoding = LD_DW_EH_PE_ABSPTR;
    cie->lsda_encoding = LD_DW_EH_PE_OMIT;
    if (augmentation_size == 0 || data[augmentation_start] != 'z') {
        return LD_OK;
    }
    cie->has_z_augmentation = true;
    uint64_t augmentation_length;
    status = ld_eh_uleb(ctx, object, data, total_size, &cursor,
                        &augmentation_length);
    if (status != LD_OK) return status;
    if (augmentation_length > total_size - cursor) goto malformed;
    size_t augmentation_end = cursor + (size_t) augmentation_length;
    for (size_t index = 1U; index < augmentation_size; index++) {
        uint8_t tag = data[augmentation_start + index];
        if (tag == 'R') {
            if (cursor >= augmentation_end) goto malformed;
            cie->fde_encoding = data[cursor++];
        } else if (tag == 'L') {
            if (cursor >= augmentation_end) goto malformed;
            cie->lsda_encoding = data[cursor++];
        } else if (tag == 'P') {
            if (cursor >= augmentation_end) goto malformed;
            uint8_t encoding = data[cursor++];
            if (encoding != (LD_DW_EH_PE_PCREL | LD_DW_EH_PE_SDATA4 |
                             LD_DW_EH_PE_INDIRECT)) {
                return ld_fail(
                        ctx, LD_UNSUPPORTED,
                        "unsupported __eh_frame personality encoding 0x%x in '%s'%s%s",
                        encoding, object->file->path,
                        object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
            }
            uint32_t pointer_offset;
            status = ld_eh_skip_encoded(ctx, object, data, augmentation_end,
                                        &cursor, encoding, &pointer_offset);
            if (status != LD_OK) return status;
            if (pointer_offset > UINT32_MAX - offset) goto malformed;
            status = ld_eh_personality_symbol(
                    ctx, object, section, offset + pointer_offset,
                    &cie->personality_symbol_index);
            if (status != LD_OK) return status;
            cie->has_personality = true;
        } else if (tag == 'S') {
            continue;
        } else {
            return ld_fail(ctx, LD_UNSUPPORTED,
                           "unsupported __eh_frame CIE augmentation '%c' in '%s'%s%s",
                           tag, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
    }
    if (cursor > augmentation_end) goto malformed;
    return LD_OK;

malformed:
    return ld_fail(ctx, LD_INVALID_INPUT,
                   "malformed __eh_frame CIE at offset 0x%x in '%s'%s%s",
                   offset, object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static const ld_macho_cie_t *ld_eh_find_cie(
        const ld_macho_cie_list_t *cies, uint32_t offset) {
    for (size_t index = 0; index < cies->count; index++) {
        if (cies->items[index].offset == offset) return &cies->items[index];
    }
    return NULL;
}

static int ld_eh_parse_fde(ld_context_t *ctx, ld_object_t *object,
                           uint32_t section_index,
                           const ld_input_section_t *section, uint32_t offset,
                           uint32_t total_size, const ld_macho_cie_list_t *cies,
                           ld_macho_fde_list_t *fdes) {
    if (total_size < 12U) goto malformed;
    const uint8_t *data = section->data + offset;
    uint32_t cie_pointer = ld_eh_read_u32(data + 4U);
    if (cie_pointer > offset + 4U) goto malformed;
    uint32_t cie_offset = offset + 4U - cie_pointer;
    const ld_macho_cie_t *cie = ld_eh_find_cie(cies, cie_offset);
    if (!cie) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "no matching CIE for __eh_frame FDE at offset 0x%x in '%s'%s%s",
                       offset, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }

    size_t cursor = 8U;
    size_t pointer_width;
    uint64_t function_address;
    int status = ld_eh_resolve_pointer(
            ctx, object, section, offset + (uint32_t) cursor,
            cie->fde_encoding, &function_address, &pointer_width);
    if (status != LD_OK) return status;
    cursor += pointer_width;

    uint8_t range_encoding = cie->fde_encoding & LD_DW_EH_PE_FORMAT_MASK;
    uint64_t function_size;
    int64_t ignored_signed;
    size_t range_width;
    if (!ld_eh_read_fixed(data, total_size, cursor, range_encoding,
                          &function_size, &ignored_signed, &range_width)) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "unsupported or truncated __eh_frame FDE range at offset 0x%x in '%s'%s%s",
                       offset, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    cursor += range_width;
    if (function_size == 0) goto malformed;

    ld_macho_fde_t fde = {
            .object = object,
            .section_index = section_index,
            .record_offset = offset,
            .record_size = total_size,
            .output_offset = section->output_offset + offset,
            .function_address = function_address,
            .function_size = function_size,
            .personality_symbol_index = cie->personality_symbol_index,
            .has_personality = cie->has_personality,
    };

    if (cie->has_z_augmentation) {
        uint64_t augmentation_length;
        status = ld_eh_uleb(ctx, object, data, total_size, &cursor,
                            &augmentation_length);
        if (status != LD_OK) return status;
        if (augmentation_length > total_size - cursor) goto malformed;
        size_t augmentation_end = cursor + (size_t) augmentation_length;
        if (cie->lsda_encoding != LD_DW_EH_PE_OMIT) {
            if (cursor > UINT32_MAX - offset) goto malformed;
            status = ld_eh_resolve_pointer(
                    ctx, object, section, offset + (uint32_t) cursor,
                    cie->lsda_encoding, &fde.lsda_address, &pointer_width);
            if (status != LD_OK) return status;
            cursor += pointer_width;
            fde.has_lsda = true;
        }
        if (cursor > augmentation_end) goto malformed;
    }
    return ld_eh_fde_push(ctx, fdes, fde);

malformed:
    return ld_fail(ctx, LD_INVALID_INPUT,
                   "malformed __eh_frame FDE at offset 0x%x in '%s'%s%s",
                   offset, object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static int ld_eh_collect_section(ld_context_t *ctx, ld_object_t *object,
                                 uint32_t section_index,
                                 const ld_input_section_t *section,
                                 ld_macho_fde_list_t *fdes) {
    if ((section->header.size && !section->data) ||
        (section->header.nreloc && !section->relocations) ||
        section->header.size > UINT32_MAX) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "malformed __TEXT,__eh_frame section in '%s'%s%s",
                       object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    ld_macho_cie_list_t cies = {0};
    uint32_t offset = 0;
    int status = LD_OK;
    while (offset < section->header.size) {
        if (section->header.size - offset < 4U) {
            status = ld_fail(ctx, LD_INVALID_INPUT,
                             "truncated __eh_frame record header at offset 0x%x in '%s'%s%s",
                             offset, object->file->path,
                             object->member_name ? " member " : "",
                             object->member_name ? object->member_name : "");
            goto done;
        }
        uint32_t payload_size = ld_eh_read_u32(section->data + offset);
        if (payload_size == 0U) {
            offset += 4U;
            while (offset < section->header.size) {
                if (section->data[offset++] != 0U) {
                    status = ld_fail(ctx, LD_INVALID_INPUT,
                                     "nonzero bytes after __eh_frame terminator in '%s'%s%s",
                                     object->file->path,
                                     object->member_name ? " member " : "",
                                     object->member_name ? object->member_name : "");
                    goto done;
                }
            }
            break;
        }
        if (payload_size == UINT32_MAX ||
            payload_size > section->header.size - offset - 4U) {
            status = ld_fail(ctx, LD_INVALID_INPUT,
                             "invalid __eh_frame record size at offset 0x%x in '%s'%s%s",
                             offset, object->file->path,
                             object->member_name ? " member " : "",
                             object->member_name ? object->member_name : "");
            goto done;
        }
        uint32_t total_size = payload_size + 4U;
        if (payload_size < 4U) {
            status = ld_fail(ctx, LD_INVALID_INPUT,
                             "short __eh_frame record at offset 0x%x in '%s'%s%s",
                             offset, object->file->path,
                             object->member_name ? " member " : "",
                             object->member_name ? object->member_name : "");
            goto done;
        }
        uint32_t id = ld_eh_read_u32(section->data + offset + 4U);
        if (id == 0U) {
            ld_macho_cie_t cie;
            status = ld_eh_parse_cie(ctx, object, section, offset, total_size,
                                     &cie);
            if (status != LD_OK) goto done;
            status = ld_eh_cie_push(ctx, &cies, cie);
            if (status != LD_OK) goto done;
        }
        offset += total_size;
    }

    offset = 0;
    while (offset < section->header.size) {
        uint32_t payload_size = ld_eh_read_u32(section->data + offset);
        if (payload_size == 0U) break;
        uint32_t total_size = payload_size + 4U;
        uint32_t id = ld_eh_read_u32(section->data + offset + 4U);
        if (id != 0U) {
            status = ld_eh_parse_fde(ctx, object, section_index, section,
                                     offset, total_size, &cies, fdes);
            if (status != LD_OK) goto done;
        }
        offset += total_size;
    }

done:
    free(cies.items);
    return status;
}

int ld_macho_eh_frame_collect(ld_context_t *ctx, ld_macho_fde_list_t *list) {
    if (!ctx || !list) return LD_INVALID_ARGUMENT;
    for (size_t object_index = 0; object_index < ctx->objects.count;
         object_index++) {
        ld_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t section_index = 0;
             section_index < object->section_count; section_index++) {
            const ld_input_section_t *section =
                    &object->sections[section_index];
            if (!ld_eh_name_is(section->header.segname, "__TEXT") ||
                !ld_eh_name_is(section->header.sectname, "__eh_frame")) {
                continue;
            }
            if (!section->output) continue;
            int status = ld_eh_collect_section(
                    ctx, object, (uint32_t) section_index, section, list);
            if (status != LD_OK) return status;
        }
    }
    return LD_OK;
}

void ld_macho_eh_frame_deinit(ld_macho_fde_list_t *list) {
    if (!list) return;
    free(list->items);
    memset(list, 0, sizeof(*list));
}
