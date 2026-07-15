#include "ld_internal.h"
#include "ld_macho_eh_frame.h"
#include "ld_macho_synthetic.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* The record normalization and two-level page layout follow Zig's Mach-O
   UnwindInfo implementation at commit 738d2be9.  Nature currently emits the
   official regular second-level representation; compressed pages are an
   equivalent size optimization and can be added without changing this API. */
#define LD_UNWIND_PAGE_SIZE 0x1000U
#define LD_UNWIND_REGULAR_PAGE_ENTRIES                                      \
    ((LD_UNWIND_PAGE_SIZE - sizeof(ld_unwind_info_regular_page_header_t)) / \
     sizeof(ld_unwind_info_regular_entry_t))

static uint32_t ld_unwind_read_u32(const uint8_t *bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static uint64_t ld_unwind_read_u64(const uint8_t *bytes) {
    uint64_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static void ld_unwind_write_u32(uint8_t *bytes, uint32_t value) {
    memcpy(bytes, &value, sizeof(value));
}

static bool ld_unwind_name_is(const char name[16], const char *expected) {
    size_t length = strlen(expected);
    return length <= 16U && memcmp(name, expected, length) == 0 &&
           (length == 16U || name[length] == '\0');
}

static int ld_unwind_record_push(ld_context_t *ctx, const ld_unwind_record_t *record) {
    ld_unwind_state_t *state = &ctx->unwind;
    if (state->count == state->capacity) {
        size_t next = state->capacity ? state->capacity * 2U : 256U;
        if (next < state->capacity) {
            return ld_fail(ctx, LD_IO_ERROR, "too many compact unwind records");
        }
        ld_unwind_record_t *records =
                ld_realloc_array(state->records, state->capacity, next, sizeof(*records));
        if (!records) {
            return ld_fail(ctx, LD_IO_ERROR, "out of memory collecting compact unwind records");
        }
        state->records = records;
        state->capacity = next;
    }
    state->records[state->count++] = *record;
    return LD_OK;
}

static int ld_unwind_output_push(ld_context_t *ctx, ld_output_section_t *section) {
    ld_output_list_t *outputs = &ctx->outputs;
    if (outputs->count == outputs->capacity) {
        size_t next = outputs->capacity ? outputs->capacity * 2U : 16U;
        if (next < outputs->capacity) {
            return LD_IO_ERROR;
        }
        ld_output_section_t **items =
                ld_realloc_array(outputs->items, outputs->capacity, next, sizeof(*items));
        if (!items) {
            return LD_IO_ERROR;
        }
        outputs->items = items;
        outputs->capacity = next;
    }
    outputs->items[outputs->count++] = section;
    return LD_OK;
}

static ld_symbol_t *ld_unwind_symbol(ld_context_t *ctx, ld_object_t *object,
                                     uint32_t symbol_index) {
    if (symbol_index >= object->symbol_count) {
        return NULL;
    }
    const char *name = object->symbols[symbol_index].name;
    if (!name || !*name) {
        return NULL;
    }
    ld_symbol_t *symbol = NULL;
    HASH_FIND_STR(ctx->symbols, name, symbol);
    return symbol;
}

static bool ld_unwind_ref_for_symbol(ld_context_t *ctx, ld_object_t *object,
                                     uint32_t symbol_index,
                                     ld_macho_ref_t *ref) {
    if (symbol_index >= object->symbol_count) return false;
    ld_input_symbol_t *input = &object->symbols[symbol_index];
    if ((input->entry.n_type & LD_N_EXT) != 0) {
        ld_symbol_t *global = ld_unwind_symbol(ctx, object, symbol_index);
        if (global) {
            *ref = ld_macho_global_ref(global);
            return true;
        }
    }
    uint8_t type = input->entry.n_type & LD_N_TYPE;
    if (type == LD_N_SECT || type == LD_N_ABS) {
        *ref = ld_macho_local_ref(object, symbol_index);
        return true;
    }
    return false;
}

static bool ld_unwind_ref_at_input_address(ld_context_t *ctx,
                                           ld_object_t *object,
                                           uint64_t address,
                                           ld_macho_ref_t *ref) {
    /* Zig's local compact-unwind personality path looks up the nlist whose
       input value equals the raw personality field.  Prefer an external
       candidate (which preserves coalescing/import semantics), then accept a
       true local definition through Nature's shared Mach-O reference model. */
    for (unsigned pass = 0; pass < 2U; pass++) {
        for (size_t index = 0; index < object->symbol_count; index++) {
            ld_input_symbol_t *input = &object->symbols[index];
            if ((input->entry.n_type & LD_N_STAB) != 0 ||
                input->entry.n_value != address) {
                continue;
            }
            uint8_t type = input->entry.n_type & LD_N_TYPE;
            if (type != LD_N_SECT && type != LD_N_ABS) continue;
            bool external = (input->entry.n_type & LD_N_EXT) != 0;
            if ((pass == 0U) != external) continue;
            if (ld_unwind_ref_for_symbol(ctx, object, (uint32_t) index,
                                         ref)) {
                return true;
            }
        }
    }
    return false;
}

static int ld_unwind_add_personality_ref(ld_context_t *ctx,
                                         ld_unwind_record_t *record,
                                         ld_macho_ref_t ref) {
    if (!ld_macho_ref_is_valid(&ref)) {
        return ld_fail(ctx, LD_SYMBOL_ERROR,
                       "compact unwind personality has an invalid symbol reference in '%s'%s%s",
                       record->object->file->path,
                       record->object->member_name ? " member " : "",
                       record->object->member_name
                               ? record->object->member_name
                               : "");
    }

    size_t personality_index = 0;
    while (personality_index < ctx->unwind.personality_count &&
           !ld_macho_ref_equal(
                   &ctx->unwind.personalities[personality_index], &ref)) {
        personality_index++;
    }
    if (personality_index == ctx->unwind.personality_count) {
        if (personality_index >= sizeof(ctx->unwind.personalities) /
                                         sizeof(ctx->unwind.personalities[0])) {
            return ld_fail(ctx, LD_UNSUPPORTED,
                           "more than three compact unwind personalities are required by '%s'%s%s",
                           record->object->file->path,
                           record->object->member_name ? " member " : "",
                           record->object->member_name
                                   ? record->object->member_name
                                   : "");
        }
        ctx->unwind.personalities[ctx->unwind.personality_count++] = ref;
    }
    uint32_t got_index;
    int result = ld_macho_got_add(ctx, ref, &got_index);
    if (result != LD_OK) {
        return result;
    }
    record->personality_index = (uint8_t) (personality_index + 1U);
    return LD_OK;
}

static int ld_unwind_add_personality(ld_context_t *ctx,
                                     ld_unwind_record_t *record,
                                     const ld_relocation_t *relocation,
                                     uint64_t raw_value) {
    ld_object_t *object = record->object;
    ld_macho_ref_t ref = {0};
    bool found = relocation->external
                         ? ld_unwind_ref_for_symbol(
                                   ctx, object, relocation->symbolnum, &ref)
                         : ld_unwind_ref_at_input_address(
                                   ctx, object, raw_value, &ref);
    if (!found) {
        const char *name =
                relocation->external &&
                                relocation->symbolnum < object->symbol_count
                        ? object->symbols[relocation->symbolnum].name
                        : NULL;
        return ld_fail(
                ctx, LD_SYMBOL_ERROR,
                "compact unwind personality references unknown symbol '%s' at input address 0x%llx in '%s'%s%s",
                name ? name : "<local>", (unsigned long long) raw_value,
                object->file->path,
                object->member_name ? " member " : "",
                object->member_name ? object->member_name : "");
    }
    return ld_unwind_add_personality_ref(ctx, record, ref);
}

static int ld_unwind_decode_relocation(ld_context_t *ctx, ld_object_t *object,
                                       const uint8_t *raw, ld_relocation_t *relocation) {
    uint32_t address = ld_unwind_read_u32(raw);
    uint32_t word = ld_unwind_read_u32(raw + 4U);
    if ((address & 0x80000000U) != 0) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
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
    if (relocation->type != LD_ARM64_RELOC_UNSIGNED || relocation->length != 3U ||
        relocation->pcrel) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
                       "invalid compact unwind relocation at offset 0x%x in '%s'%s%s",
                       relocation->address, object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    return LD_OK;
}

static int ld_unwind_collect_section(ld_context_t *ctx, ld_object_t *object,
                                     const ld_input_section_t *section) {
    if (section->header.size % sizeof(ld_compact_unwind_entry_t) != 0 ||
        (section->header.size && !section->data) ||
        (section->header.nreloc && !section->relocations)) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "malformed __LD,__compact_unwind section in '%s'%s%s",
                       object->file->path, object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    uint64_t input_count = section->header.size / sizeof(ld_compact_unwind_entry_t);
    if (input_count > SIZE_MAX - ctx->unwind.count) {
        return ld_fail(ctx, LD_INVALID_INPUT,
                       "too many compact unwind records in '%s'%s%s", object->file->path,
                       object->member_name ? " member " : "",
                       object->member_name ? object->member_name : "");
    }
    size_t first_record = ctx->unwind.count;
    for (uint64_t i = 0; i < input_count; i++) {
        const uint8_t *raw = section->data + i * sizeof(ld_compact_unwind_entry_t);
        ld_unwind_record_t record = {0};
        record.object = object;
        record.start_addend = ld_unwind_read_u64(raw);
        record.length = ld_unwind_read_u32(raw + 8U);
        record.encoding = ld_unwind_read_u32(raw + 12U);
        record.lsda_addend = ld_unwind_read_u64(raw + 24U);
        int result = ld_unwind_record_push(ctx, &record);
        if (result != LD_OK) {
            return result;
        }
    }

    for (uint32_t i = 0; i < section->header.nreloc; i++) {
        ld_relocation_t relocation;
        int result = ld_unwind_decode_relocation(
                ctx, object, section->relocations + (size_t) i * 8U, &relocation);
        if (result != LD_OK) {
            return result;
        }
        if (relocation.address >= section->header.size) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind relocation offset 0x%x is outside '%s'%s%s",
                           relocation.address, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        size_t record_index = first_record +
                              relocation.address / sizeof(ld_compact_unwind_entry_t);
        uint32_t field_offset = relocation.address % sizeof(ld_compact_unwind_entry_t);
        ld_unwind_record_t *record = &ctx->unwind.records[record_index];
        if (field_offset == 0U) {
            if (record->has_start_relocation) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "duplicate compact unwind function relocation at offset 0x%x in '%s'%s%s",
                               relocation.address, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            record->start_relocation = relocation;
            record->has_start_relocation = true;
        } else if (field_offset == 16U) {
            if (record->personality_index != 0) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "duplicate compact unwind personality relocation at offset 0x%x in '%s'%s%s",
                               relocation.address, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            const uint8_t *record_raw =
                    section->data +
                    (record_index - first_record) *
                            sizeof(ld_compact_unwind_entry_t);
            /* For a local personality relocation this raw field is the
               input nlist address used to identify the personality.  Apple
               clang does not require it to be zero. */
            uint64_t personality_addend =
                    ld_unwind_read_u64(record_raw + 16U);
            result = ld_unwind_add_personality(
                    ctx, record, &relocation, personality_addend);
            if (result != LD_OK) {
                return result;
            }
        } else if (field_offset == 24U) {
            if (record->has_lsda_relocation) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "duplicate compact unwind LSDA relocation at offset 0x%x in '%s'%s%s",
                               relocation.address, object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            record->lsda_relocation = relocation;
            record->has_lsda_relocation = true;
        } else {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind relocation at invalid field offset 0x%x in '%s'%s%s",
                           relocation.address, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
    }

    for (size_t i = first_record; i < ctx->unwind.count; i++) {
        ld_unwind_record_t *record = &ctx->unwind.records[i];
        if (!record->has_start_relocation || record->length == 0) {
            return ld_fail(ctx, LD_INVALID_INPUT,
                           "compact unwind record %zu has no function relocation or an empty range in '%s'%s%s",
                           i - first_record, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        /* Personality bits, LSDA consistency, and DWARF FDE hints are
           finalized after __eh_frame/compact-unwind superposition. */
    }
    return LD_OK;
}

typedef struct {
    ld_object_t *object;
    uint32_t section_index;
    uint64_t address;
    uint64_t size;
    size_t compact_index;
    size_t fde_index;
} ld_unwind_function_t;

typedef struct {
    ld_unwind_function_t *items;
    size_t count;
    size_t capacity;
} ld_unwind_function_list_t;

static bool ld_unwind_section_is_code(const ld_input_section_t *section) {
    return section && section->output && section->header.size != 0 &&
           (section->header.flags &
            (LD_S_ATTR_PURE_INSTRUCTIONS | LD_S_ATTR_SOME_INSTRUCTIONS)) !=
                   0;
}

static bool ld_unwind_address_in_section(const ld_input_section_t *section,
                                         uint64_t address) {
    return section->header.size != 0 && address >= section->header.addr &&
           address - section->header.addr < section->header.size;
}

static bool ld_unwind_find_code_section(const ld_object_t *object,
                                        uint64_t address,
                                        uint32_t *section_index) {
    for (size_t index = 0; index < object->section_count; index++) {
        const ld_input_section_t *section = &object->sections[index];
        if (ld_unwind_section_is_code(section) &&
            ld_unwind_address_in_section(section, address)) {
            *section_index = (uint32_t) index;
            return true;
        }
    }
    return false;
}

static ld_unwind_function_t *ld_unwind_function_find(
        ld_unwind_function_list_t *list, ld_object_t *object,
        uint64_t address) {
    for (size_t index = 0; index < list->count; index++) {
        ld_unwind_function_t *function = &list->items[index];
        if (function->object == object && function->address == address) {
            return function;
        }
    }
    return NULL;
}

static int ld_unwind_function_get_or_add(
        ld_context_t *ctx, ld_unwind_function_list_t *list,
        ld_object_t *object, uint32_t section_index, uint64_t address,
        ld_unwind_function_t **result) {
    ld_unwind_function_t *existing =
            ld_unwind_function_find(list, object, address);
    if (existing) {
        if (existing->section_index != section_index) {
            return ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "function input address 0x%llx is ambiguous between Mach-O sections in '%s'%s%s",
                    (unsigned long long) address, object->file->path,
                    object->member_name ? " member " : "",
                    object->member_name ? object->member_name : "");
        }
        *result = existing;
        return LD_OK;
    }
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 64U;
        if (next < list->capacity) {
            return ld_fail(ctx, LD_OUTPUT_ERROR,
                           "too many Mach-O code symbols for unwind info");
        }
        ld_unwind_function_t *items = ld_realloc_array(
                list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "out of memory collecting Mach-O functions for unwind info");
        }
        list->items = items;
        list->capacity = next;
    }
    ld_unwind_function_t function = {
            .object = object,
            .section_index = section_index,
            .address = address,
            .compact_index = SIZE_MAX,
            .fde_index = SIZE_MAX,
    };
    list->items[list->count] = function;
    *result = &list->items[list->count++];
    return LD_OK;
}

static int ld_unwind_record_input_address(ld_context_t *ctx,
                                          const ld_unwind_record_t *record,
                                          uint64_t *address,
                                          uint32_t *section_index) {
    const ld_relocation_t *relocation = &record->start_relocation;
    ld_object_t *object = record->object;
    if (!relocation->external) {
        if (relocation->symbolnum == 0 ||
            relocation->symbolnum > object->section_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind function relocation has invalid section %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        *section_index = relocation->symbolnum - 1U;
        *address = record->start_addend;
    } else {
        if (relocation->symbolnum >= object->symbol_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind function relocation has invalid symbol %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        const ld_input_symbol_t *input =
                &object->symbols[relocation->symbolnum];
        if ((input->entry.n_type & LD_N_TYPE) != LD_N_SECT ||
            input->entry.n_sect == 0 ||
            input->entry.n_sect > object->section_count ||
            record->start_addend > UINT64_MAX - input->entry.n_value) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind function references invalid symbol '%s' in '%s'%s%s",
                           input->name ? input->name : "<unnamed>",
                           object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        *section_index = input->entry.n_sect - 1U;
        *address = input->entry.n_value + record->start_addend;
    }
    const ld_input_section_t *section = &object->sections[*section_index];
    if (!ld_unwind_section_is_code(section) ||
        !ld_unwind_address_in_section(section, *address)) {
        return ld_fail(
                ctx, LD_INVALID_INPUT,
                "compact unwind function address 0x%llx is outside a code section in '%s'%s%s",
                (unsigned long long) *address, object->file->path,
                object->member_name ? " member " : "",
                object->member_name ? object->member_name : "");
    }
    if (record->length >
        section->header.size - (*address - section->header.addr)) {
        return ld_fail(
                ctx, LD_INVALID_INPUT,
                "compact unwind function range [0x%llx, +0x%x) exceeds its code section in '%s'%s%s",
                (unsigned long long) *address, record->length,
                object->file->path,
                object->member_name ? " member " : "",
                object->member_name ? object->member_name : "");
    }
    return LD_OK;
}

static int ld_unwind_collect_functions(ld_context_t *ctx,
                                       ld_unwind_function_list_t *functions) {
    for (size_t object_index = 0; object_index < ctx->objects.count;
         object_index++) {
        ld_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) continue;
        for (size_t symbol_index = 0; symbol_index < object->symbol_count;
             symbol_index++) {
            const ld_input_symbol_t *symbol = &object->symbols[symbol_index];
            if ((symbol->entry.n_type & LD_N_STAB) != 0 ||
                (symbol->entry.n_type & LD_N_TYPE) != LD_N_SECT ||
                symbol->entry.n_sect == 0 ||
                symbol->entry.n_sect > object->section_count) {
                continue;
            }
            uint32_t section_index = symbol->entry.n_sect - 1U;
            const ld_input_section_t *section =
                    &object->sections[section_index];
            if (!ld_unwind_section_is_code(section) ||
                !ld_unwind_address_in_section(section,
                                              symbol->entry.n_value)) {
                continue;
            }
            ld_unwind_function_t *function;
            int status = ld_unwind_function_get_or_add(
                    ctx, functions, object, section_index,
                    symbol->entry.n_value, &function);
            if (status != LD_OK) return status;
        }
    }
    return LD_OK;
}

static void ld_unwind_infer_function_sizes(
        ld_unwind_function_list_t *functions) {
    for (size_t index = 0; index < functions->count; index++) {
        ld_unwind_function_t *function = &functions->items[index];
        uint64_t end = function->object->sections[function->section_index]
                               .header.addr +
                       function->object->sections[function->section_index]
                               .header.size;
        for (size_t next_index = 0; next_index < functions->count;
             next_index++) {
            const ld_unwind_function_t *next = &functions->items[next_index];
            if (next->object == function->object &&
                next->section_index == function->section_index &&
                next->address > function->address && next->address < end) {
                end = next->address;
            }
        }
        function->size = end > function->address
                                 ? end - function->address
                                 : 0;
    }
}

static int ld_unwind_set_local_target(ld_context_t *ctx,
                                      ld_unwind_record_t *record,
                                      uint64_t address,
                                      ld_relocation_t *relocation,
                                      uint64_t *addend) {
    uint32_t section_index;
    if (!ld_unwind_find_code_section(record->object, address,
                                     &section_index)) {
        /* LSDA normally resides in a non-code section, so broaden the search
           after the fast code lookup used by function targets. */
        bool found = false;
        for (size_t index = 0; index < record->object->section_count;
             index++) {
            const ld_input_section_t *section =
                    &record->object->sections[index];
            if (section->output &&
                ld_unwind_address_in_section(section, address)) {
                section_index = (uint32_t) index;
                found = true;
                break;
            }
        }
        if (!found) {
            return ld_fail(
                    ctx, LD_RELOCATION_ERROR,
                    "unwind target input address 0x%llx is outside all sections in '%s'%s%s",
                    (unsigned long long) address,
                    record->object->file->path,
                    record->object->member_name ? " member " : "",
                    record->object->member_name
                            ? record->object->member_name
                            : "");
        }
    }
    *relocation = (ld_relocation_t) {
            .symbolnum = section_index + 1U,
            .length = 3U,
            .type = LD_ARM64_RELOC_UNSIGNED,
    };
    *addend = address;
    return LD_OK;
}

static int ld_unwind_attach_fde(ld_context_t *ctx,
                                ld_unwind_record_t *record,
                                const ld_macho_fde_t *fde) {
    uint32_t hint = fde->output_offset <= 0x00ffffffU
                            ? (uint32_t) fde->output_offset
                            : 0U;
    record->encoding &= ~0x00ffffffU;
    record->encoding |= hint;
    if (fde->has_personality) {
        ld_macho_ref_t ref;
        if (!ld_unwind_ref_for_symbol(ctx, fde->object,
                                      fde->personality_symbol_index, &ref)) {
            const ld_input_symbol_t *input =
                    fde->personality_symbol_index < fde->object->symbol_count
                            ? &fde->object
                                       ->symbols[fde->personality_symbol_index]
                            : NULL;
            return ld_fail(
                    ctx, LD_SYMBOL_ERROR,
                    "__eh_frame personality references unresolved symbol '%s' in '%s'%s%s",
                    input && input->name ? input->name : "<invalid>",
                    fde->object->file->path,
                    fde->object->member_name ? " member " : "",
                    fde->object->member_name ? fde->object->member_name : "");
        }
        if (record->personality_index != 0) {
            size_t index = record->personality_index - 1U;
            if (index >= ctx->unwind.personality_count ||
                !ld_macho_ref_equal(&ctx->unwind.personalities[index],
                                    &ref)) {
                return ld_fail(
                        ctx, LD_INVALID_INPUT,
                        "compact unwind and __eh_frame disagree on the personality for function 0x%llx in '%s'%s%s",
                        (unsigned long long) fde->function_address,
                        fde->object->file->path,
                        fde->object->member_name ? " member " : "",
                        fde->object->member_name
                                ? fde->object->member_name
                                : "");
            }
        } else {
            int status = ld_unwind_add_personality_ref(ctx, record, ref);
            if (status != LD_OK) return status;
        }
    }
    if (fde->has_lsda) {
        /* For a DWARF compact-unwind record the FDE is authoritative.  This
           mirrors Zig's UnwindInfo.generate path and avoids retaining stale
           compact-unwind LSDA metadata when both representations are present. */
        int status = ld_unwind_set_local_target(
                ctx, record, fde->lsda_address, &record->lsda_relocation,
                &record->lsda_addend);
        if (status != LD_OK) return status;
        record->has_lsda_relocation = true;
    }
    return LD_OK;
}

static int ld_unwind_superimpose(ld_context_t *ctx,
                                 const ld_macho_fde_list_t *fdes) {
    ld_unwind_function_list_t functions = {0};
    int status = ld_unwind_collect_functions(ctx, &functions);
    if (status != LD_OK) goto done;

    for (size_t index = 0; index < ctx->unwind.count; index++) {
        ld_unwind_record_t *record = &ctx->unwind.records[index];
        uint64_t address;
        uint32_t section_index;
        status = ld_unwind_record_input_address(
                ctx, record, &address, &section_index);
        if (status != LD_OK) goto done;
        ld_unwind_function_t *function;
        status = ld_unwind_function_get_or_add(
                ctx, &functions, record->object, section_index, address,
                &function);
        if (status != LD_OK) goto done;
        if (function->compact_index != SIZE_MAX) {
            status = ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "duplicate compact unwind function input address 0x%llx in '%s'%s%s",
                    (unsigned long long) address,
                    record->object->file->path,
                    record->object->member_name ? " member " : "",
                    record->object->member_name
                            ? record->object->member_name
                            : "");
            goto done;
        }
        function->compact_index = index;
        if (function->size == 0) function->size = record->length;
        if ((record->encoding & LD_UNWIND_ARM64_MODE_MASK) ==
            LD_UNWIND_ARM64_MODE_DWARF) {
            record->encoding &= ~0x00ffffffU;
        }
    }

    for (size_t index = 0; index < fdes->count; index++) {
        const ld_macho_fde_t *fde = &fdes->items[index];
        uint32_t section_index;
        if (!ld_unwind_find_code_section(fde->object,
                                         fde->function_address,
                                         &section_index)) {
            status = ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "__eh_frame FDE function address 0x%llx is outside a code section in '%s'%s%s",
                    (unsigned long long) fde->function_address,
                    fde->object->file->path,
                    fde->object->member_name ? " member " : "",
                    fde->object->member_name
                            ? fde->object->member_name
                            : "");
            goto done;
        }
        const ld_input_section_t *code_section =
                &fde->object->sections[section_index];
        if (fde->function_size >
            code_section->header.size -
                    (fde->function_address - code_section->header.addr)) {
            status = ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "__eh_frame FDE range [0x%llx, +0x%llx) exceeds its code section in '%s'%s%s",
                    (unsigned long long) fde->function_address,
                    (unsigned long long) fde->function_size,
                    fde->object->file->path,
                    fde->object->member_name ? " member " : "",
                    fde->object->member_name ? fde->object->member_name : "");
            goto done;
        }
        ld_unwind_function_t *function;
        status = ld_unwind_function_get_or_add(
                ctx, &functions, fde->object, section_index,
                fde->function_address, &function);
        if (status != LD_OK) goto done;
        if (function->fde_index != SIZE_MAX) {
            status = ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "duplicate __eh_frame FDE for function input address 0x%llx in '%s'%s%s",
                    (unsigned long long) fde->function_address,
                    fde->object->file->path,
                    fde->object->member_name ? " member " : "",
                    fde->object->member_name
                            ? fde->object->member_name
                            : "");
            goto done;
        }
        function->fde_index = index;
        if (function->size == 0) function->size = fde->function_size;
    }

    ld_unwind_infer_function_sizes(&functions);
    for (size_t index = 0; index < functions.count; index++) {
        ld_unwind_function_t *function = &functions.items[index];
        if (function->compact_index != SIZE_MAX) {
            ld_unwind_record_t *record =
                    &ctx->unwind.records[function->compact_index];
            if (function->fde_index != SIZE_MAX &&
                (record->encoding & LD_UNWIND_ARM64_MODE_MASK) ==
                        LD_UNWIND_ARM64_MODE_DWARF) {
                status = ld_unwind_attach_fde(
                        ctx, record, &fdes->items[function->fde_index]);
                if (status != LD_OK) goto done;
            }
            continue;
        }
        if (function->size == 0 || function->size > UINT32_MAX) {
            status = ld_fail(
                    ctx, LD_OUTPUT_ERROR,
                    "cannot represent unwind range for function input address 0x%llx in '%s'%s%s",
                    (unsigned long long) function->address,
                    function->object->file->path,
                    function->object->member_name ? " member " : "",
                    function->object->member_name
                            ? function->object->member_name
                            : "");
            goto done;
        }
        ld_unwind_record_t record = {
                .object = function->object,
                .start_relocation =
                        {
                                .symbolnum = function->section_index + 1U,
                                .length = 3U,
                                .type = LD_ARM64_RELOC_UNSIGNED,
                        },
                .start_addend = function->address,
                .length = (uint32_t) function->size,
                .has_start_relocation = true,
        };
        if (function->fde_index != SIZE_MAX) {
            record.length =
                    fdes->items[function->fde_index].function_size <=
                                    UINT32_MAX
                            ? (uint32_t) fdes->items[function->fde_index]
                                      .function_size
                            : 0U;
            if (record.length == 0) {
                status = ld_fail(
                        ctx, LD_OUTPUT_ERROR,
                        "cannot represent __eh_frame FDE range for function input address 0x%llx in '%s'%s%s",
                        (unsigned long long) function->address,
                        function->object->file->path,
                        function->object->member_name ? " member " : "",
                        function->object->member_name
                                ? function->object->member_name
                                : "");
                goto done;
            }
            record.encoding = LD_UNWIND_ARM64_MODE_DWARF;
            status = ld_unwind_attach_fde(
                    ctx, &record, &fdes->items[function->fde_index]);
            if (status != LD_OK) goto done;
        }
        status = ld_unwind_record_push(ctx, &record);
        if (status != LD_OK) goto done;
    }

    ctx->unwind.lsda_count = 0;
    for (size_t index = 0; index < ctx->unwind.count; index++) {
        ld_unwind_record_t *record = &ctx->unwind.records[index];
        record->encoding &= ~LD_UNWIND_PERSONALITY_MASK;
        record->encoding |= (uint32_t) record->personality_index << 28U;
        if (record->has_lsda_relocation || record->lsda_addend != 0) {
            if (!record->has_lsda_relocation) {
                status = ld_fail(
                        ctx, LD_INVALID_INPUT,
                        "unwind LSDA has no relocation in '%s'%s%s",
                        record->object->file->path,
                        record->object->member_name ? " member " : "",
                        record->object->member_name
                                ? record->object->member_name
                                : "");
                goto done;
            }
            record->encoding |= LD_UNWIND_HAS_LSDA;
            ctx->unwind.lsda_count++;
        } else if ((record->encoding & LD_UNWIND_HAS_LSDA) != 0) {
            status = ld_fail(
                    ctx, LD_INVALID_INPUT,
                    "unwind record has the LSDA flag but no matching LSDA in '%s'%s%s",
                    record->object->file->path,
                    record->object->member_name ? " member " : "",
                    record->object->member_name
                            ? record->object->member_name
                            : "");
            goto done;
        }
    }

done:
    free(functions.items);
    return status;
}

int ld_unwind_prepare(ld_context_t *ctx) {
    for (size_t object_index = 0; object_index < ctx->objects.count; object_index++) {
        ld_object_t *object = ctx->objects.items[object_index];
        if (!object->selected) {
            continue;
        }
        for (size_t section_index = 0; section_index < object->section_count; section_index++) {
            const ld_input_section_t *section = &object->sections[section_index];
            if (!ld_unwind_name_is(section->header.segname, "__LD") ||
                !ld_unwind_name_is(section->header.sectname, "__compact_unwind")) {
                continue;
            }
            int result = ld_unwind_collect_section(ctx, object, section);
            if (result != LD_OK) {
                return result;
            }
        }
    }
    ld_macho_fde_list_t fdes = {0};
    int result = ld_macho_eh_frame_collect(ctx, &fdes);
    if (result == LD_OK) result = ld_unwind_superimpose(ctx, &fdes);
    ld_macho_eh_frame_deinit(&fdes);
    if (result != LD_OK) return result;

    if (ctx->unwind.count == 0) {
        return LD_OK;
    }
    if (ctx->unwind.count > UINT32_MAX || ctx->unwind.lsda_count > UINT32_MAX) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "too many records for __TEXT,__unwind_info");
    }

    uint64_t page_count = (ctx->unwind.count + LD_UNWIND_REGULAR_PAGE_ENTRIES - 1U) /
                          LD_UNWIND_REGULAR_PAGE_ENTRIES;
    uint64_t index_count = page_count + 1U;
    uint64_t prefix_size = sizeof(ld_unwind_info_header_t);
    if (ctx->unwind.personality_count > (UINT64_MAX - prefix_size) / sizeof(uint32_t)) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "unwind personality table is too large");
    }
    prefix_size += ctx->unwind.personality_count * sizeof(uint32_t);
    if (index_count > (UINT64_MAX - prefix_size) / sizeof(ld_unwind_info_index_entry_t)) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "unwind index is too large");
    }
    prefix_size += index_count * sizeof(ld_unwind_info_index_entry_t);
    if (ctx->unwind.lsda_count >
        (UINT64_MAX - prefix_size) / sizeof(ld_unwind_info_lsda_entry_t)) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "unwind LSDA index is too large");
    }
    prefix_size += ctx->unwind.lsda_count * sizeof(ld_unwind_info_lsda_entry_t);
    if (page_count > (UINT64_MAX - prefix_size) / LD_UNWIND_PAGE_SIZE) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "unwind second-level pages are too large");
    }
    uint64_t output_size = prefix_size + page_count * LD_UNWIND_PAGE_SIZE;
    if (output_size > SIZE_MAX || output_size > UINT32_MAX) {
        return ld_fail(ctx, LD_OUTPUT_ERROR, "__TEXT,__unwind_info is too large");
    }

    ld_output_section_t *output = calloc(1, sizeof(*output));
    if (!output) {
        return ld_fail(ctx, LD_IO_ERROR, "out of memory creating __TEXT,__unwind_info");
    }
    memcpy(output->segname, "__TEXT", sizeof("__TEXT"));
    memcpy(output->sectname, "__unwind_info", sizeof("__unwind_info"));
    output->flags = LD_S_REGULAR;
    output->align = 2;
    output->size = output_size;
    output->data = calloc(1, (size_t) output_size);
    if (!output->data) {
        free(output);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory creating __TEXT,__unwind_info");
    }
    output->data_capacity = (size_t) output_size;
    if (ld_unwind_output_push(ctx, output) != LD_OK) {
        free(output->data);
        free(output);
        return ld_fail(ctx, LD_IO_ERROR, "out of memory recording __TEXT,__unwind_info");
    }
    ctx->unwind.output = output;
    return LD_OK;
}

static uint64_t ld_unwind_symbol_address(const ld_symbol_t *symbol) {
    if (symbol->kind == LD_SYMBOL_ABSOLUTE) {
        return symbol->value;
    }
    if (symbol->output) {
        return symbol->output->addr + symbol->output_offset;
    }
    return symbol->value;
}

static bool ld_unwind_add_u64(uint64_t left, uint64_t right, uint64_t *result) {
    if (right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static int ld_unwind_relocation_target(ld_context_t *ctx, ld_unwind_record_t *record,
                                       const ld_relocation_t *relocation, uint64_t addend,
                                       uint64_t *target) {
    ld_object_t *object = record->object;
    uint64_t base = 0;
    if (!relocation->external) {
        if (relocation->symbolnum == 0 || relocation->symbolnum > object->section_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind relocation has invalid section %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        const ld_input_section_t *section = &object->sections[relocation->symbolnum - 1U];
        if (!section->output) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind relocation targets discarded section %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        if (!ld_unwind_add_u64(section->output->addr, section->output_offset, &base)) {
            goto overflow;
        }
        /* Local Mach-O UNSIGNED relocations store an input VM address in the
           field.  Convert it to an offset within the referenced input
           section before applying the final output base.  This matters for
           code/data sections whose MH_OBJECT address is nonzero. */
        if (addend >= section->header.addr) {
            addend -= section->header.addr;
        }
    } else {
        if (relocation->symbolnum >= object->symbol_count) {
            return ld_fail(ctx, LD_RELOCATION_ERROR,
                           "compact unwind relocation has invalid symbol %u in '%s'%s%s",
                           relocation->symbolnum, object->file->path,
                           object->member_name ? " member " : "",
                           object->member_name ? object->member_name : "");
        }
        const ld_input_symbol_t *input = &object->symbols[relocation->symbolnum];
        uint8_t type = input->entry.n_type & LD_N_TYPE;
        if (type == LD_N_SECT && input->entry.n_sect != 0 &&
            input->entry.n_sect <= object->section_count) {
            const ld_input_section_t *section = &object->sections[input->entry.n_sect - 1U];
            if (!section->output) {
                return ld_fail(ctx, LD_RELOCATION_ERROR,
                               "compact unwind relocation targets discarded symbol '%s' in '%s'%s%s",
                               input->name ? input->name : "<unnamed>", object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            uint64_t relative = input->entry.n_value >= section->header.addr
                                        ? input->entry.n_value - section->header.addr
                                        : input->entry.n_value;
            if (!ld_unwind_add_u64(section->output->addr, section->output_offset, &base) ||
                !ld_unwind_add_u64(base, relative, &base)) {
                goto overflow;
            }
        } else if (type == LD_N_ABS) {
            base = input->entry.n_value;
        } else {
            ld_symbol_t *symbol = ld_unwind_symbol(ctx, object, relocation->symbolnum);
            if (!symbol || symbol->kind == LD_SYMBOL_IMPORT ||
                symbol->kind == LD_SYMBOL_UNDEFINED) {
                return ld_fail(ctx, LD_SYMBOL_ERROR,
                               "compact unwind relocation references unresolved symbol '%s' in '%s'%s%s",
                               input->name ? input->name : "<unnamed>", object->file->path,
                               object->member_name ? " member " : "",
                               object->member_name ? object->member_name : "");
            }
            base = ld_unwind_symbol_address(symbol);
        }
    }
    if (!ld_unwind_add_u64(base, addend, target)) {
        goto overflow;
    }
    return LD_OK;

overflow:
    return ld_fail(ctx, LD_RELOCATION_ERROR,
                   "compact unwind relocation overflows in '%s'%s%s", object->file->path,
                   object->member_name ? " member " : "",
                   object->member_name ? object->member_name : "");
}

static int ld_unwind_record_compare(const void *left, const void *right) {
    const ld_unwind_record_t *a = left;
    const ld_unwind_record_t *b = right;
    if (a->function_offset < b->function_offset) return -1;
    if (a->function_offset > b->function_offset) return 1;
    if (a->length < b->length) return -1;
    if (a->length > b->length) return 1;
    return 0;
}

int ld_unwind_emit(ld_context_t *ctx) {
    ld_unwind_state_t *state = &ctx->unwind;
    if (!state->output) {
        return LD_OK;
    }
    for (size_t i = 0; i < state->count; i++) {
        ld_unwind_record_t *record = &state->records[i];
        uint64_t address;
        int result = ld_unwind_relocation_target(ctx, record, &record->start_relocation,
                                                 record->start_addend, &address);
        if (result != LD_OK) {
            return result;
        }
        if (address < LD_IMAGE_BASE || address - LD_IMAGE_BASE > UINT32_MAX) {
            return ld_fail(ctx, LD_OUTPUT_ERROR,
                           "compact unwind function address 0x%llx is outside the 32-bit __TEXT range",
                           (unsigned long long) address);
        }
        record->function_offset = (uint32_t) (address - LD_IMAGE_BASE);
        if (record->has_lsda_relocation || record->lsda_addend != 0) {
            if (!record->has_lsda_relocation) {
                return ld_fail(ctx, LD_INVALID_INPUT,
                               "compact unwind LSDA has no relocation in '%s'%s%s",
                               record->object->file->path,
                               record->object->member_name ? " member " : "",
                               record->object->member_name ? record->object->member_name : "");
            }
            result = ld_unwind_relocation_target(ctx, record, &record->lsda_relocation,
                                                 record->lsda_addend, &address);
            if (result != LD_OK) {
                return result;
            }
            if (address < LD_IMAGE_BASE || address - LD_IMAGE_BASE > UINT32_MAX) {
                return ld_fail(ctx, LD_OUTPUT_ERROR,
                               "compact unwind LSDA address 0x%llx is outside the 32-bit image range",
                               (unsigned long long) address);
            }
            record->lsda_offset = (uint32_t) (address - LD_IMAGE_BASE);
        }
    }
    qsort(state->records, state->count, sizeof(state->records[0]),
          ld_unwind_record_compare);
    for (size_t i = 1; i < state->count; i++) {
        if (state->records[i - 1U].function_offset == state->records[i].function_offset) {
            return ld_fail(ctx, LD_INVALID_INPUT,
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
            (uint32_t) ((state->count + LD_UNWIND_REGULAR_PAGE_ENTRIES - 1U) /
                        LD_UNWIND_REGULAR_PAGE_ENTRIES);
    uint32_t personalities_offset = sizeof(ld_unwind_info_header_t);
    uint32_t index_offset = personalities_offset +
                            (uint32_t) state->personality_count * sizeof(uint32_t);
    uint32_t index_count = page_count + 1U;
    uint32_t lsda_offset = index_offset +
                           index_count * sizeof(ld_unwind_info_index_entry_t);
    uint32_t pages_offset = lsda_offset +
                            (uint32_t) state->lsda_count *
                                    sizeof(ld_unwind_info_lsda_entry_t);
    uint8_t *output = state->output->data;
    ld_unwind_info_header_t header = {
            .version = LD_UNWIND_SECTION_VERSION,
            .common_encodings_offset = sizeof(ld_unwind_info_header_t),
            .common_encodings_count = 0,
            .personalities_offset = personalities_offset,
            .personalities_count = (uint32_t) state->personality_count,
            .index_offset = index_offset,
            .index_count = index_count,
    };
    memcpy(output, &header, sizeof(header));

    for (size_t i = 0; i < state->personality_count; i++) {
        const ld_macho_ref_t *ref = &state->personalities[i];
        uint64_t address;
        if (!ld_macho_got_address(ctx, ref, &address)) {
            return ld_fail(ctx, LD_OUTPUT_ERROR,
                           "missing GOT entry for compact unwind personality '%s'",
                           ld_macho_ref_name(ref));
        }
        if (address < LD_IMAGE_BASE || address - LD_IMAGE_BASE > UINT32_MAX) {
            return ld_fail(ctx, LD_OUTPUT_ERROR,
                           "compact unwind personality GOT address is outside the 32-bit image range");
        }
        ld_unwind_write_u32(output + personalities_offset + i * sizeof(uint32_t),
                            (uint32_t) (address - LD_IMAGE_BASE));
    }

    size_t lsda_written = 0;
    for (uint32_t page_index = 0; page_index < page_count; page_index++) {
        size_t first = (size_t) page_index * LD_UNWIND_REGULAR_PAGE_ENTRIES;
        size_t count = state->count - first;
        if (count > LD_UNWIND_REGULAR_PAGE_ENTRIES) {
            count = LD_UNWIND_REGULAR_PAGE_ENTRIES;
        }
        ld_unwind_info_index_entry_t index = {
                .function_offset = state->records[first].function_offset,
                .second_level_page_offset = pages_offset + page_index * LD_UNWIND_PAGE_SIZE,
                .lsda_index_offset = lsda_offset +
                                     (uint32_t) lsda_written *
                                             sizeof(ld_unwind_info_lsda_entry_t),
        };
        memcpy(output + index_offset + page_index * sizeof(index), &index, sizeof(index));

        uint8_t *page = output + pages_offset + page_index * LD_UNWIND_PAGE_SIZE;
        ld_unwind_info_regular_page_header_t page_header = {
                .kind = LD_UNWIND_SECOND_LEVEL_REGULAR,
                .entry_page_offset = sizeof(ld_unwind_info_regular_page_header_t),
                .entry_count = (uint16_t) count,
        };
        memcpy(page, &page_header, sizeof(page_header));
        for (size_t i = 0; i < count; i++) {
            const ld_unwind_record_t *record = &state->records[first + i];
            ld_unwind_info_regular_entry_t entry = {
                    .function_offset = record->function_offset,
                    .encoding = record->encoding,
            };
            memcpy(page + sizeof(page_header) + i * sizeof(entry), &entry, sizeof(entry));
            if ((record->encoding & LD_UNWIND_HAS_LSDA) != 0) {
                ld_unwind_info_lsda_entry_t lsda = {
                        .function_offset = record->function_offset,
                        .lsda_offset = record->lsda_offset,
                };
                memcpy(output + lsda_offset + lsda_written * sizeof(lsda), &lsda,
                       sizeof(lsda));
                lsda_written++;
            }
        }
    }

    const ld_unwind_record_t *last = &state->records[state->count - 1U];
    if (last->length > UINT32_MAX - last->function_offset) {
        return ld_fail(ctx, LD_OUTPUT_ERROR,
                       "compact unwind sentinel offset overflows for '%s'%s%s",
                       last->object->file->path,
                       last->object->member_name ? " member " : "",
                       last->object->member_name ? last->object->member_name : "");
    }
    ld_unwind_info_index_entry_t sentinel = {
            .function_offset = last->function_offset + last->length,
            .second_level_page_offset = 0,
            .lsda_index_offset = lsda_offset +
                                 (uint32_t) lsda_written *
                                         sizeof(ld_unwind_info_lsda_entry_t),
    };
    memcpy(output + index_offset + page_count * sizeof(sentinel), &sentinel,
           sizeof(sentinel));
    return LD_OK;
}
