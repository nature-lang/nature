#include "ld_macho_synthetic.h"

#include <limits.h>

/* The reference representation and stable-list allocation mirror Zig's
   MachO.Ref-based synthetic GOT/TlvPtr collections at commit
   738d2be9d6b6ef3ff3559130c05159ef53336224, translated to owned C arrays. */

ld_macho_ref_t ld_macho_global_ref(ld_symbol_t *symbol) {
    return (ld_macho_ref_t) {.global = symbol};
}

ld_macho_ref_t ld_macho_local_ref(ld_object_t *object, uint32_t input_index) {
    return (ld_macho_ref_t) {
            .object = object,
            .input_index = input_index,
    };
}

bool ld_macho_ref_is_valid(const ld_macho_ref_t *ref) {
    if (!ref) return false;
    if (ref->global) return ref->object == NULL;
    return ref->object && ref->input_index < ref->object->symbol_count;
}

bool ld_macho_ref_equal(const ld_macho_ref_t *left,
                        const ld_macho_ref_t *right) {
    if (!left || !right) return false;
    if (left->global || right->global) {
        return left->global && left->global == right->global;
    }
    return left->object == right->object &&
           left->input_index == right->input_index;
}

ld_input_symbol_t *ld_macho_ref_input(const ld_macho_ref_t *ref) {
    if (!ld_macho_ref_is_valid(ref)) return NULL;
    if (ref->global) return ref->global->input;
    return &ref->object->symbols[ref->input_index];
}

const char *ld_macho_ref_name(const ld_macho_ref_t *ref) {
    if (!ref) return "<invalid>";
    if (ref->global) return ref->global->name ? ref->global->name : "<unnamed>";
    ld_input_symbol_t *input = ld_macho_ref_input(ref);
    return input && input->name ? input->name : "<local>";
}

uint32_t ld_macho_ref_section_type(const ld_macho_ref_t *ref) {
    ld_input_symbol_t *input = ld_macho_ref_input(ref);
    if (!input || !input->object ||
        (input->entry.n_type & LD_N_TYPE) != LD_N_SECT ||
        input->entry.n_sect == 0 ||
        input->entry.n_sect > input->object->section_count) {
        return UINT32_MAX;
    }
    return input->object->sections[input->entry.n_sect - 1U].header.flags &
           LD_SECTION_TYPE;
}

bool ld_macho_ref_is_tlv(const ld_macho_ref_t *ref) {
    if (!ld_macho_ref_is_valid(ref)) return false;
    if (ref->global && ref->global->tlv) return true;
    return ld_macho_ref_section_type(ref) == LD_S_THREAD_LOCAL_VARIABLES;
}

uint64_t ld_macho_ref_address(const ld_macho_ref_t *ref) {
    if (!ld_macho_ref_is_valid(ref)) return 0;
    if (ref->global) {
        if (ref->global->kind == LD_SYMBOL_ABSOLUTE) {
            return ref->global->value;
        }
        if (ref->global->output) {
            return ref->global->output->addr + ref->global->output_offset;
        }
        return ref->global->value;
    }
    ld_input_symbol_t *input = ld_macho_ref_input(ref);
    uint8_t type = input->entry.n_type & LD_N_TYPE;
    if (type == LD_N_ABS) return input->entry.n_value;
    if (type != LD_N_SECT || input->entry.n_sect == 0 ||
        input->entry.n_sect > input->object->section_count) {
        return 0;
    }
    ld_input_section_t *section =
            &input->object->sections[input->entry.n_sect - 1U];
    if (!section->output) return 0;
    uint64_t relative = input->entry.n_value >= section->header.addr
                                ? input->entry.n_value - section->header.addr
                                : input->entry.n_value;
    return section->output->addr + section->output_offset + relative;
}

bool ld_macho_ref_symtab_index(const ld_macho_ref_t *ref, uint32_t *index) {
    if (!index || !ld_macho_ref_is_valid(ref)) return false;
    uint32_t value = ref->global ? ref->global->symtab_index
                                 : ref->object->symbols[ref->input_index]
                                           .output_symtab_index;
    if (value == UINT32_MAX) return false;
    *index = value;
    return true;
}

bool ld_macho_ref_list_find(const ld_macho_ref_list_t *list,
                            const ld_macho_ref_t *ref, uint32_t *index) {
    if (!list || !ref) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (ld_macho_ref_equal(&list->items[i], ref)) {
            if (index) *index = (uint32_t) i;
            return true;
        }
    }
    return false;
}

int ld_macho_ref_list_add(ld_context_t *ctx, ld_macho_ref_list_t *list,
                          ld_macho_ref_t ref, uint32_t *index) {
    if (!list || !index || !ld_macho_ref_is_valid(&ref)) {
        return ld_fail(ctx, LD_RELOCATION_ERROR,
                       "invalid Mach-O synthetic symbol reference");
    }
    if (ld_macho_ref_list_find(list, &ref, index)) return LD_OK;
    if (list->count == UINT32_MAX) {
        return ld_fail(ctx, LD_OUTPUT_ERROR,
                       "too many Mach-O synthetic symbol references");
    }
    if (list->count == list->capacity) {
        size_t next = list->capacity ? list->capacity * 2U : 32U;
        if (next < list->capacity) {
            return ld_fail(ctx, LD_OUTPUT_ERROR,
                           "Mach-O synthetic symbol reference list overflows");
        }
        ld_macho_ref_t *items = ld_realloc_array(
                list->items, list->capacity, next, sizeof(*items));
        if (!items) {
            return ld_fail(ctx, LD_IO_ERROR,
                           "out of memory recording Mach-O synthetic symbol reference");
        }
        list->items = items;
        list->capacity = next;
    }
    *index = (uint32_t) list->count;
    list->items[list->count++] = ref;
    return LD_OK;
}

int ld_macho_got_add(ld_context_t *ctx, ld_macho_ref_t ref,
                     uint32_t *index) {
    int result = ld_macho_ref_list_add(ctx, &ctx->got_refs, ref, index);
    if (result != LD_OK) return result;
    ctx->got_count = (uint32_t) ctx->got_refs.count;
    if (ref.global) ref.global->got_index = *index;
    return LD_OK;
}

bool ld_macho_got_find(const ld_context_t *ctx, const ld_macho_ref_t *ref,
                       uint32_t *index) {
    return ctx && ld_macho_ref_list_find(&ctx->got_refs, ref, index);
}

bool ld_macho_got_address(const ld_context_t *ctx,
                          const ld_macho_ref_t *ref, uint64_t *address) {
    uint32_t index;
    if (!ctx || !ctx->got || !address ||
        !ld_macho_got_find(ctx, ref, &index)) {
        return false;
    }
    *address = ctx->got->addr + (uint64_t) index * sizeof(uint64_t);
    return true;
}

int ld_macho_tlv_ptr_add(ld_context_t *ctx, ld_macho_ref_t ref,
                         uint32_t *index) {
    int result = ld_macho_ref_list_add(ctx, &ctx->tlv_ptr_refs, ref, index);
    if (result != LD_OK) return result;
    ctx->tlv_ptr_count = (uint32_t) ctx->tlv_ptr_refs.count;
    if (ref.global) ref.global->tlv_ptr_index = *index;
    return LD_OK;
}

bool ld_macho_tlv_ptr_find(const ld_context_t *ctx,
                           const ld_macho_ref_t *ref, uint32_t *index) {
    return ctx && ld_macho_ref_list_find(&ctx->tlv_ptr_refs, ref, index);
}

bool ld_macho_tlv_ptr_address(const ld_context_t *ctx,
                              const ld_macho_ref_t *ref, uint64_t *address) {
    uint32_t index;
    if (!ctx || !ctx->tlv_ptrs || !address ||
        !ld_macho_tlv_ptr_find(ctx, ref, &index)) {
        return false;
    }
    *address = ctx->tlv_ptrs->addr + (uint64_t) index * sizeof(uint64_t);
    return true;
}
