#include "ldd_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* The record normalization and two-level page layout follow Zig's Mach-O
   UnwindInfo implementation at commit 738d2be9.  Nature currently emits the
   official regular second-level representation; compressed pages are an
   equivalent size optimization and can be added without changing this API. */
#define LDD_UNWIND_PAGE_SIZE 0x1000U
#define LDD_UNWIND_REGULAR_PAGE_ENTRIES                                       \
    ((LDD_UNWIND_PAGE_SIZE - sizeof(ldd_unwind_info_regular_page_header_t)) / \
     sizeof(ldd_unwind_info_regular_entry_t))

static uint32_t ldd_unwind_read_u32(const uint8_t *bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint64_t ldd_unwind_read_u64(const uint8_t *bytes) {
    uint64_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static void ldd_unwind_write_u32(uint8_t *bytes, uint32_t value) {
    memcpy(bytes, &value, sizeof(value));
}

static bool ldd_unwind_name_is(const char name[16], const char *expected) {
    size_t length = strlen(expected);
    return length <= 16U && memcmp(name, expected, length) == 0 &&
           (length == 16U || name[length] == '\0');
}

static int ldd_unwind_record_push(ldd_context_t *ctx, const ldd_unwind_record_t *record) {
    ldd_unwind_state_t *state = &ctx->unwind;
    if (state->count == state->capacity) {
        size_t next = state->capacity ? state->capacity * 2U : 256U;
        if (next < state->capacity) {
            return ldd_fail(ctx, LDD_IO_ERROR, "too many compact unwind records");
        }
        ldd_unwind_record_t *records =
                ldd_realloc_array(state->records, state->capacity, next, sizeof(*records));
        if (!records) {
            return ldd_fail(ctx, LDD_IO_ERROR, "out of memory collecting compact unwind records");
        }
        state->records = records;
        state->capacity = next;
    }
    state->records[state->count++] = *record;
    return LDD_OK;
}

static int ldd_unwind_output_push(ldd_context_t *ctx, ldd_output_section_t *section) {
    ldd_output_list_t *outputs = &ctx->outputs;
    if (outputs->count == outputs->capacity) {
        size_t next = outputs->capacity ? outputs->capacity * 2U : 16U;
        if (next < outputs->capacity) {
            return LDD_IO_ERROR;
        }
        ldd_output_section_t **items =
                ldd_realloc_array(outputs->items, outputs->capacity, next, sizeof(*items));
        if (!items) {
            return LDD_IO_ERROR;
        }
        outputs->items = items;
        outputs->capacity = next;
    }
    outputs->items[outputs->count++] = section;
    return LDD_OK;
}

static ldd_symbol_t *ldd_unwind_symbol(ldd_context_t *ctx, ldd_object_t *object,
                                       uint32_t symbol_index) {
    if (symbol_index >= object->symbol_count) {
        return NULL;
    }
    const char *name = object->symbols[symbol_index].name;
    if (!name || !*name) {
        return NULL;
    }
    ldd_symbol_t *symbol = NULL;
    HASH_FIND_STR(ctx->symbols, name, symbol);
    return symbol;
}

static int ldd_unwind_add_personality(ldd_context_t *ctx, ldd_unwind_record_t *record,
                                      const ldd_relocation_t *relocation) {
    ldd_object_t *object = record->object;
    if (!relocation->external) {
        return ldd_fail(ctx, LDD_UNSUPPORTED,
                        "unsupported compact unwind personality relocation in '%s'%s%s",
                        object->file->path, object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }
    ldd_symbol_t *symbol = ldd_unwind_symbol(ctx, object, relocation->symbolnum);
    if (!symbol) {
        const char *name = relocation->symbolnum < object->symbol_count
                                   ? object->symbols[relocation->symbolnum].name
                                   : NULL;
        return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                        "compact unwind personality references unknown symbol '%s' in '%s'%s%s",
                        name ? name : "<invalid>", object->file->path,
                        object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }

    size_t personality_index = 0;
    while (personality_index < ctx->unwind.personality_count &&
           ctx->unwind.personalities[personality_index] != symbol) {
        personality_index++;
    }
    if (personality_index == ctx->unwind.personality_count) {
        if (personality_index >= sizeof(ctx->unwind.personalities) /
                                         sizeof(ctx->unwind.personalities[0])) {
            return ldd_fail(ctx, LDD_UNSUPPORTED,
                            "more than three compact unwind personalities are required by '%s'%s%s",
                            object->file->path, object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        ctx->unwind.personalities[ctx->unwind.personality_count++] = symbol;
    }
    if (symbol->got_index == UINT32_MAX) {
        if (ctx->got_count == UINT32_MAX) {
            return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many GOT entries for unwind personalities");
        }
        symbol->got_index = ctx->got_count++;
    }
    record->personality_index = (uint8_t) (personality_index + 1U);
    return LDD_OK;
}

static int ldd_unwind_decode_relocation(ldd_context_t *ctx, ldd_object_t *object,
                                        const uint8_t *raw, ldd_relocation_t *relocation) {
    uint32_t address = ldd_unwind_read_u32(raw);
    uint32_t word = ldd_unwind_read_u32(raw + 4U);
    if ((address & 0x80000000U) != 0) {
        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                        "scattered compact unwind relocation in '%s'%s%s is unsupported",
                        object->file->path, object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }
    relocation->address = address;
    relocation->symbolnum = word & 0x00ffffffU;
    relocation->pcrel = (uint8_t) ((word >> 24U) & 1U);
    relocation->length = (uint8_t) ((word >> 25U) & 3U);
    relocation->external = (uint8_t) ((word >> 27U) & 1U);
    relocation->type = (uint8_t) ((word >> 28U) & 0xfU);
    if (relocation->type != LDD_ARM64_RELOC_UNSIGNED || relocation->length != 3U ||
        relocation->pcrel) {
        return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                        "invalid compact unwind relocation at offset 0x%x in '%s'%s%s",
                        relocation->address, object->file->path,
                        object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }
    return LDD_OK;
}

static int ldd_unwind_collect_section(ldd_context_t *ctx, ldd_object_t *object,
                                      const ldd_input_section_t *section) {
    if (section->header.size % sizeof(ldd_compact_unwind_entry_t) != 0 ||
        (section->header.size && !section->data) ||
        (section->header.nreloc && !section->relocations)) {
        return ldd_fail(ctx, LDD_INVALID_INPUT,
                        "malformed __LD,__compact_unwind section in '%s'%s%s",
                        object->file->path, object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }
    uint64_t input_count = section->header.size / sizeof(ldd_compact_unwind_entry_t);
    if (input_count > SIZE_MAX - ctx->unwind.count) {
        return ldd_fail(ctx, LDD_INVALID_INPUT,
                        "too many compact unwind records in '%s'%s%s", object->file->path,
                        object->member_name ? " member " : "",
                        object->member_name ? object->member_name : "");
    }
    size_t first_record = ctx->unwind.count;
    for (uint64_t i = 0; i < input_count; i++) {
        const uint8_t *raw = section->data + i * sizeof(ldd_compact_unwind_entry_t);
        ldd_unwind_record_t record = {0};
        record.object = object;
        record.start_addend = ldd_unwind_read_u64(raw);
        record.length = ldd_unwind_read_u32(raw + 8U);
        record.encoding = ldd_unwind_read_u32(raw + 12U);
        record.lsda_addend = ldd_unwind_read_u64(raw + 24U);
        if (ldd_unwind_read_u64(raw + 16U) != 0) {
            return ldd_fail(ctx, LDD_INVALID_INPUT,
                            "unrelocated compact unwind personality at record %llu in '%s'%s%s",
                            (unsigned long long) i, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        int result = ldd_unwind_record_push(ctx, &record);
        if (result != LDD_OK) {
            return result;
        }
    }

    for (uint32_t i = 0; i < section->header.nreloc; i++) {
        ldd_relocation_t relocation;
        int result = ldd_unwind_decode_relocation(
                ctx, object, section->relocations + (size_t) i * 8U, &relocation);
        if (result != LDD_OK) {
            return result;
        }
        if (relocation.address >= section->header.size) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "compact unwind relocation offset 0x%x is outside '%s'%s%s",
                            relocation.address, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        size_t record_index = first_record +
                              relocation.address / sizeof(ldd_compact_unwind_entry_t);
        uint32_t field_offset = relocation.address % sizeof(ldd_compact_unwind_entry_t);
        ldd_unwind_record_t *record = &ctx->unwind.records[record_index];
        if (field_offset == 0U) {
            if (record->has_start_relocation) {
                return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                "duplicate compact unwind function relocation at offset 0x%x in '%s'%s%s",
                                relocation.address, object->file->path,
                                object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            record->start_relocation = relocation;
            record->has_start_relocation = true;
        } else if (field_offset == 16U) {
            if (record->personality_index != 0) {
                return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                "duplicate compact unwind personality relocation at offset 0x%x in '%s'%s%s",
                                relocation.address, object->file->path,
                                object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            result = ldd_unwind_add_personality(ctx, record, &relocation);
            if (result != LDD_OK) {
                return result;
            }
        } else if (field_offset == 24U) {
            if (record->has_lsda_relocation) {
                return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                "duplicate compact unwind LSDA relocation at offset 0x%x in '%s'%s%s",
                                relocation.address, object->file->path,
                                object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            record->lsda_relocation = relocation;
            record->has_lsda_relocation = true;
        } else {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "compact unwind relocation at invalid field offset 0x%x in '%s'%s%s",
                            relocation.address, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
    }

    for (size_t i = first_record; i < ctx->unwind.count; i++) {
        ldd_unwind_record_t *record = &ctx->unwind.records[i];
        if (!record->has_start_relocation || record->length == 0) {
            return ldd_fail(ctx, LDD_INVALID_INPUT,
                            "compact unwind record %zu has no function relocation or an empty range in '%s'%s%s",
                            i - first_record, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        record->encoding &= ~LDD_UNWIND_PERSONALITY_MASK;
        record->encoding |= (uint32_t) record->personality_index << 28U;
        if (record->has_lsda_relocation || record->lsda_addend != 0) {
            record->encoding |= LDD_UNWIND_HAS_LSDA;
            ctx->unwind.lsda_count++;
        } else if ((record->encoding & LDD_UNWIND_HAS_LSDA) != 0) {
            return ldd_fail(ctx, LDD_INVALID_INPUT,
                            "compact unwind record %zu has the LSDA flag but no LSDA in '%s'%s%s",
                            i - first_record, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        if ((record->encoding & LDD_UNWIND_ARM64_MODE_MASK) ==
            LDD_UNWIND_ARM64_MODE_DWARF) {
            /* An object-relative FDE hint is not valid after __eh_frame
               sections are merged.  Zero is a legal hint and asks the
               unwinder to scan from the first CFI record, as Zig does when
               an exact output offset cannot be represented. */
            record->encoding &= ~0x00ffffffU;
        }
    }
    return LDD_OK;
}

int ldd_unwind_prepare(ldd_context_t *ctx) {
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ldd_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) {
            continue;
        }
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            const ldd_input_section_t *section = &object->sections[section_index];
            if (!ldd_unwind_name_is(section->header.segname, "__LD") ||
                !ldd_unwind_name_is(section->header.sectname, "__compact_unwind")) {
                continue;
            }
            int result = ldd_unwind_collect_section(ctx, object, section);
            if (result != LDD_OK) {
                return result;
            }
        }
    }
    if (ctx->unwind.count == 0) {
        return LDD_OK;
    }
    if (ctx->unwind.count > UINT32_MAX || ctx->unwind.lsda_count > UINT32_MAX) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "too many records for __TEXT,__unwind_info");
    }

    uint64_t page_count = (ctx->unwind.count + LDD_UNWIND_REGULAR_PAGE_ENTRIES - 1U) /
                          LDD_UNWIND_REGULAR_PAGE_ENTRIES;
    uint64_t index_count = page_count + 1U;
    uint64_t prefix_size = sizeof(ldd_unwind_info_header_t);
    if (ctx->unwind.personality_count > (UINT64_MAX - prefix_size) / sizeof(uint32_t)) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "unwind personality table is too large");
    }
    prefix_size += ctx->unwind.personality_count * sizeof(uint32_t);
    if (index_count > (UINT64_MAX - prefix_size) / sizeof(ldd_unwind_info_index_entry_t)) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "unwind index is too large");
    }
    prefix_size += index_count * sizeof(ldd_unwind_info_index_entry_t);
    if (ctx->unwind.lsda_count >
        (UINT64_MAX - prefix_size) / sizeof(ldd_unwind_info_lsda_entry_t)) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "unwind LSDA index is too large");
    }
    prefix_size += ctx->unwind.lsda_count * sizeof(ldd_unwind_info_lsda_entry_t);
    if (page_count > (UINT64_MAX - prefix_size) / LDD_UNWIND_PAGE_SIZE) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "unwind second-level pages are too large");
    }
    uint64_t output_size = prefix_size + page_count * LDD_UNWIND_PAGE_SIZE;
    if (output_size > SIZE_MAX || output_size > UINT32_MAX) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR, "__TEXT,__unwind_info is too large");
    }

    ldd_output_section_t *output = calloc(1, sizeof(*output));
    if (!output) {
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating __TEXT,__unwind_info");
    }
    memcpy(output->segname, "__TEXT", sizeof("__TEXT"));
    memcpy(output->sectname, "__unwind_info", sizeof("__unwind_info"));
    output->flags = LDD_S_REGULAR;
    output->align = 2;
    output->size = output_size;
    output->data = calloc(1, (size_t) output_size);
    if (!output->data) {
        free(output);
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory creating __TEXT,__unwind_info");
    }
    output->data_capacity = (size_t) output_size;
    if (ldd_unwind_output_push(ctx, output) != LDD_OK) {
        free(output->data);
        free(output);
        return ldd_fail(ctx, LDD_IO_ERROR, "out of memory recording __TEXT,__unwind_info");
    }
    ctx->unwind.output = output;
    return LDD_OK;
}

static uint64_t ldd_unwind_symbol_address(const ldd_symbol_t *symbol) {
    if (symbol->kind == LDD_SYMBOL_ABSOLUTE) {
        return symbol->value;
    }
    if (symbol->output) {
        return symbol->output->addr + symbol->output_offset;
    }
    return symbol->value;
}

static bool ldd_unwind_add_u64(uint64_t left, uint64_t right, uint64_t *result) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static int ldd_unwind_relocation_target(ldd_context_t *ctx, ldd_unwind_record_t *record,
                                        const ldd_relocation_t *relocation, uint64_t addend,
                                        uint64_t *target) {
    ldd_object_t *object = record->object;
    uint64_t base = 0;
    if (!relocation->external) {
        if (relocation->symbolnum == 0 || relocation->symbolnum > object->section_count) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "compact unwind relocation has invalid section %u in '%s'%s%s",
                            relocation->symbolnum, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        const ldd_input_section_t *section = &object->sections[relocation->symbolnum - 1U];
        if (!section->output) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "compact unwind relocation targets discarded section %u in '%s'%s%s",
                            relocation->symbolnum, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        if (!ldd_unwind_add_u64(section->output->addr, section->output_offset, &base)) {
            goto overflow;
        }
    } else {
        if (relocation->symbolnum >= object->symbol_count) {
            return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                            "compact unwind relocation has invalid symbol %u in '%s'%s%s",
                            relocation->symbolnum, object->file->path,
                            object->member_name ? " member " : "",
                            object->member_name ? object->member_name : "");
        }
        const ldd_input_symbol_t *input = &object->symbols[relocation->symbolnum];
        uint8_t type = input->entry.n_type & LDD_N_TYPE;
        if (type == LDD_N_SECT && input->entry.n_sect != 0 &&
            input->entry.n_sect <= object->section_count) {
            const ldd_input_section_t *section = &object->sections[input->entry.n_sect - 1U];
            if (!section->output) {
                return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                                "compact unwind relocation targets discarded symbol '%s' in '%s'%s%s",
                                input->name ? input->name : "<unnamed>", object->file->path,
                                object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            uint64_t relative = input->entry.n_value >= section->header.addr
                                        ? input->entry.n_value - section->header.addr
                                        : input->entry.n_value;
            if (!ldd_unwind_add_u64(section->output->addr, section->output_offset, &base) ||
                !ldd_unwind_add_u64(base, relative, &base)) {
                goto overflow;
            }
        } else if (type == LDD_N_ABS) {
            base = input->entry.n_value;
        } else {
            ldd_symbol_t *symbol = ldd_unwind_symbol(ctx, object, relocation->symbolnum);
            if (!symbol || symbol->kind == LDD_SYMBOL_IMPORT ||
                symbol->kind == LDD_SYMBOL_UNDEFINED) {
                return ldd_fail(ctx, LDD_SYMBOL_ERROR,
                                "compact unwind relocation references unresolved symbol '%s' in '%s'%s%s",
                                input->name ? input->name : "<unnamed>", object->file->path,
                                object->member_name ? " member " : "",
                                object->member_name ? object->member_name : "");
            }
            base = ldd_unwind_symbol_address(symbol);
        }
    }
    if (!ldd_unwind_add_u64(base, addend, target)) {
        goto overflow;
    }
    return LDD_OK;

overflow:
    return ldd_fail(ctx, LDD_RELOCATION_ERROR,
                    "compact unwind relocation overflows in '%s'%s%s", object->file->path,
                    object->member_name ? " member " : "",
                    object->member_name ? object->member_name : "");
}

static int ldd_unwind_record_compare(const void *left, const void *right) {
    const ldd_unwind_record_t *a = left;
    const ldd_unwind_record_t *b = right;
    if (a->function_offset < b->function_offset) return -1;
    if (a->function_offset > b->function_offset) return 1;
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    return 0;
}

int ldd_unwind_emit(ldd_context_t *ctx) {
    ldd_unwind_state_t *state = &ctx->unwind;
    if (!state->output) {
        return LDD_OK;
    }
    for (size_t i = 0; i < state->count; i++) {
        ldd_unwind_record_t *record = &state->records[i];
        uint64_t address;
        int result = ldd_unwind_relocation_target(ctx, record, &record->start_relocation,
                                                  record->start_addend, &address);
        if (result != LDD_OK) {
            return result;
        }
        if (address < LDD_IMAGE_BASE || address - LDD_IMAGE_BASE > UINT32_MAX) {
            return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                            "compact unwind function address 0x%llx is outside the 32-bit __TEXT range",
                            (unsigned long long) address);
        }
        record->function_offset = (uint32_t) (address - LDD_IMAGE_BASE);
        if (record->has_lsda_relocation || record->lsda_addend != 0) {
            if (!record->has_lsda_relocation) {
                return ldd_fail(ctx, LDD_INVALID_INPUT,
                                "compact unwind LSDA has no relocation in '%s'%s%s",
                                record->object->file->path,
                                record->object->member_name ? " member " : "",
                                record->object->member_name ? record->object->member_name : "");
            }
            result = ldd_unwind_relocation_target(ctx, record, &record->lsda_relocation,
                                                  record->lsda_addend, &address);
            if (result != LDD_OK) {
                return result;
            }
            if (address < LDD_IMAGE_BASE || address - LDD_IMAGE_BASE > UINT32_MAX) {
                return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                                "compact unwind LSDA address 0x%llx is outside the 32-bit image range",
                                (unsigned long long) address);
            }
            record->lsda_offset = (uint32_t) (address - LDD_IMAGE_BASE);
        }
    }
    qsort(state->records, state->count, sizeof(state->records[0]),
          ldd_unwind_record_compare);
    for (size_t i = 1; i < state->count; i++) {
        if (state->records[i - 1U].function_offset == state->records[i].function_offset) {
            return ldd_fail(ctx, LDD_INVALID_INPUT,
                            "duplicate compact unwind function offset 0x%x in '%s'%s%s",
                            state->records[i].function_offset,
                            state->records[i].object->file->path,
                            state->records[i].object->member_name ? " member " : "",
                            state->records[i].object->member_name
                                    ? state->records[i].object->member_name
                                    : "");
        }
    }

    uint32_t page_count =
            (uint32_t) ((state->count + LDD_UNWIND_REGULAR_PAGE_ENTRIES - 1U) /
                        LDD_UNWIND_REGULAR_PAGE_ENTRIES);
    uint32_t personalities_offset = sizeof(ldd_unwind_info_header_t);
    uint32_t index_offset = personalities_offset +
                            (uint32_t) state->personality_count * sizeof(uint32_t);
    uint32_t index_count = page_count + 1U;
    uint32_t lsda_offset = index_offset +
                           index_count * sizeof(ldd_unwind_info_index_entry_t);
    uint32_t pages_offset = lsda_offset +
                            (uint32_t) state->lsda_count *
                                    sizeof(ldd_unwind_info_lsda_entry_t);
    uint8_t *output = state->output->data;
    ldd_unwind_info_header_t header = {
            .version = LDD_UNWIND_SECTION_VERSION,
            .common_encodings_offset = sizeof(ldd_unwind_info_header_t),
            .common_encodings_count = 0,
            .personalities_offset = personalities_offset,
            .personalities_count = (uint32_t) state->personality_count,
            .index_offset = index_offset,
            .index_count = index_count,
    };
    memcpy(output, &header, sizeof(header));

    for (size_t i = 0; i < state->personality_count; i++) {
        ldd_symbol_t *symbol = state->personalities[i];
        if (!ctx->got || symbol->got_index == UINT32_MAX) {
            return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                            "missing GOT entry for compact unwind personality '%s'", symbol->name);
        }
        uint64_t address = ctx->got->addr + (uint64_t) symbol->got_index * 8U;
        if (address < LDD_IMAGE_BASE || address - LDD_IMAGE_BASE > UINT32_MAX) {
            return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                            "compact unwind personality GOT address is outside the 32-bit image range");
        }
        ldd_unwind_write_u32(output + personalities_offset + i * sizeof(uint32_t),
                             (uint32_t) (address - LDD_IMAGE_BASE));
    }

    size_t lsda_written = 0;
    for (uint32_t page_index = 0; page_index < page_count; page_index++) {
        size_t first = (size_t) page_index * LDD_UNWIND_REGULAR_PAGE_ENTRIES;
        size_t count = state->count - first;
        if (count > LDD_UNWIND_REGULAR_PAGE_ENTRIES) {
            count = LDD_UNWIND_REGULAR_PAGE_ENTRIES;
        }
        ldd_unwind_info_index_entry_t index = {
                .function_offset = state->records[first].function_offset,
                .second_level_page_offset = pages_offset + page_index * LDD_UNWIND_PAGE_SIZE,
                .lsda_index_offset = lsda_offset +
                                     (uint32_t) lsda_written *
                                             sizeof(ldd_unwind_info_lsda_entry_t),
        };
        memcpy(output + index_offset + page_index * sizeof(index), &index, sizeof(index));

        uint8_t *page = output + pages_offset + page_index * LDD_UNWIND_PAGE_SIZE;
        ldd_unwind_info_regular_page_header_t page_header = {
                .kind = LDD_UNWIND_SECOND_LEVEL_REGULAR,
                .entry_page_offset = sizeof(ldd_unwind_info_regular_page_header_t),
                .entry_count = (uint16_t) count,
        };
        memcpy(page, &page_header, sizeof(page_header));
        for (size_t i = 0; i < count; i++) {
            const ldd_unwind_record_t *record = &state->records[first + i];
            ldd_unwind_info_regular_entry_t entry = {
                    .function_offset = record->function_offset,
                    .encoding = record->encoding,
            };
            memcpy(page + sizeof(page_header) + i * sizeof(entry), &entry, sizeof(entry));
            if ((record->encoding & LDD_UNWIND_HAS_LSDA) != 0) {
                ldd_unwind_info_lsda_entry_t lsda = {
                        .function_offset = record->function_offset,
                        .lsda_offset = record->lsda_offset,
                };
                memcpy(output + lsda_offset + lsda_written * sizeof(lsda), &lsda,
                       sizeof(lsda));
                lsda_written++;
            }
        }
    }

    const ldd_unwind_record_t *last = &state->records[state->count - 1U];
    if (last->length > UINT32_MAX - last->function_offset) {
        return ldd_fail(ctx, LDD_OUTPUT_ERROR,
                        "compact unwind sentinel offset overflows for '%s'%s%s",
                        last->object->file->path,
                        last->object->member_name ? " member " : "",
                        last->object->member_name ? last->object->member_name : "");
    }
    ldd_unwind_info_index_entry_t sentinel = {
            .function_offset = last->function_offset + last->length,
            .second_level_page_offset = 0,
            .lsda_index_offset = lsda_offset +
                                 (uint32_t) lsda_written *
                                         sizeof(ldd_unwind_info_lsda_entry_t),
    };
    memcpy(output + index_offset + page_count * sizeof(sentinel), &sentinel,
           sizeof(sentinel));
    return LDD_OK;
}
