#include "ld_coff_internal.h"
#include "ld_output.h"

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * Chunk grouping, AMD64 relocation, import synthesis, base relocations, and
 * PE32+ emission are semantic C ports of lld/COFF/Chunks.cpp and Writer.cpp.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

static bool ld_coff_u32_align(uint32_t value, uint32_t alignment,
                              uint32_t *result) {
    uint64_t aligned;
    if (!ld_coff_align_ok(value, alignment, &aligned) || aligned > UINT32_MAX)
        return false;
    *result = (uint32_t) aligned;
    return true;
}

static int ld_coff_push_synthetic(ld_coff_context_t *ctx,
                                  ld_coff_section_t *section) {
    if (ctx->synthetic_count == ctx->synthetic_capacity) {
        size_t next = ctx->synthetic_capacity
                              ? ctx->synthetic_capacity * 2U
                              : 8U;
        ld_coff_section_t **items = ld_coff_grow(
                ctx->synthetic_sections, ctx->synthetic_capacity, next,
                sizeof(*items));
        if (!items) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ctx->synthetic_sections = items;
        ctx->synthetic_capacity = next;
    }
    ctx->synthetic_sections[ctx->synthetic_count++] = section;
    return LD_OK;
}

static ld_coff_section_t *ld_coff_new_synthetic(
        ld_coff_context_t *ctx, const char *name, uint32_t characteristics,
        uint32_t size, uint32_t alignment, bool uninitialized) {
    ld_coff_section_t *section = calloc(1, sizeof(*section));
    if (!section) return NULL;
    section->name = ld_coff_strndup(name, strlen(name));
    section->owned_data = size && !uninitialized ? calloc(1, size) : NULL;
    section->data = section->owned_data;
    section->data_size = uninitialized ? 0U : size;
    section->virtual_size = size;
    section->alignment = alignment;
    section->characteristics = characteristics;
    section->uninitialized = uninitialized;
    section->synthetic = true;
    section->input_order = ctx->next_input_order++;
    if (!section->name || (size && !uninitialized && !section->owned_data) ||
        ld_coff_push_synthetic(ctx, section) != LD_OK) {
        free(section->name);
        free(section->owned_data);
        free(section);
        return NULL;
    }
    return section;
}

static int ld_coff_prepare_common(ld_coff_context_t *ctx) {
    uint64_t size = 0U;
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (global->kind != LD_COFF_GLOBAL_COMMON) continue;
        uint64_t aligned;
        if (!ld_coff_align_ok(size, global->common_alignment, &aligned) ||
            !ld_coff_add_ok(aligned, global->common_size, &size) ||
            size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "common symbols exceed PE section limits");
        global->value = aligned;
    }
    if (size == 0U) return LD_OK;
    ld_coff_section_t *section = ld_coff_new_synthetic(
            ctx, ".bss", LD_COFF_SCN_CNT_UNINITIALIZED_DATA | LD_COFF_SCN_MEM_READ | LD_COFF_SCN_MEM_WRITE,
            (uint32_t) size, 32U, true);
    if (!section) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (global->kind == LD_COFF_GLOBAL_COMMON) global->section = section;
    }
    return LD_OK;
}

static int ld_coff_prepare_ctor_dtor_list(ld_coff_context_t *ctx,
                                          const char *symbol_name,
                                          const char *head_name,
                                          const char *tail_name) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, symbol_name, false);
    if (!global || global->kind != LD_COFF_GLOBAL_DEFINED || global->section)
        return LD_OK;
    const uint32_t characteristics =
            LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
            LD_COFF_SCN_MEM_WRITE;
    ld_coff_section_t *head = ld_coff_new_synthetic(
            ctx, head_name, characteristics, 8U, 8U, false);
    ld_coff_section_t *tail = ld_coff_new_synthetic(
            ctx, tail_name, characteristics, 8U, 8U, false);
    if (!head || !tail)
        return ld_coff_fail(ctx, LD_IO_ERROR,
                            "out of memory creating MinGW constructor lists");
    memset(head->owned_data, 0xff, 8U);
    global->section = head;
    global->value = 0U;
    return LD_OK;
}

static int ld_coff_prepare_mingw_synthetics(ld_coff_context_t *ctx) {
    int status = ld_coff_prepare_ctor_dtor_list(
            ctx, "__CTOR_LIST__", ".ctors$__nature_head",
            ".ctors$__nature_tail");
    if (status != LD_OK) return status;
    return ld_coff_prepare_ctor_dtor_list(
            ctx, "__DTOR_LIST__", ".dtors$__nature_head",
            ".dtors$__nature_tail");
}

static int ld_coff_import_compare(const void *left_value,
                                  const void *right_value) {
    const ld_coff_import_t *left = *(ld_coff_import_t *const *) left_value;
    const ld_coff_import_t *right = *(ld_coff_import_t *const *) right_value;
    int compare = strcasecmp(left->dll_name, right->dll_name);
    if (compare) return compare;
    if (left->name_type == LD_COFF_IMPORT_ORDINAL &&
        right->name_type == LD_COFF_IMPORT_ORDINAL)
        return (left->ordinal_hint > right->ordinal_hint) -
               (left->ordinal_hint < right->ordinal_hint);
    if (left->name_type == LD_COFF_IMPORT_ORDINAL) return -1;
    if (right->name_type == LD_COFF_IMPORT_ORDINAL) return 1;
    compare = strcmp(left->import_name, right->import_name);
    if (compare) return compare;
    return strcmp(left->public_name, right->public_name);
}

static int ld_coff_prepare_imports(ld_coff_context_t *ctx) {
    if (ctx->import_count == 0U) return LD_OK;
    qsort(ctx->imports, ctx->import_count, sizeof(*ctx->imports),
          ld_coff_import_compare);
    size_t group_count = 0U;
    for (size_t i = 0; i < ctx->import_count; i++) {
        if (i == 0U ||
            strcasecmp(ctx->imports[i - 1U]->dll_name,
                       ctx->imports[i]->dll_name) != 0)
            group_count++;
    }
    uint64_t descriptor_size = (group_count + 1U) * 20U;
    uint64_t lookup_size = 0U;
    uint64_t address_size = 0U;
    uint64_t hint_size = 0U;
    uint64_t dll_name_size = 0U;
    if (descriptor_size > UINT32_MAX)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "import table size overflow");
    size_t group_index = 0U;
    for (size_t first = 0; first < ctx->import_count;) {
        size_t end = first + 1U;
        while (end < ctx->import_count &&
               strcasecmp(ctx->imports[first]->dll_name,
                          ctx->imports[end]->dll_name) == 0)
            end++;
        uint64_t table_size = (end - first + 1U) * 8U;
        for (size_t i = first; i < end; i++) {
            ctx->imports[i]->descriptor_index = (uint32_t) group_index;
            ctx->imports[i]->ilt_offset =
                    (uint32_t) (lookup_size + (i - first) * 8U);
        }
        if (!ld_coff_add_ok(lookup_size, table_size, &lookup_size) ||
            lookup_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import lookup table is too large");
        first = end;
        group_index++;
    }
    for (size_t first = 0; first < ctx->import_count;) {
        size_t end = first + 1U;
        while (end < ctx->import_count &&
               strcasecmp(ctx->imports[first]->dll_name,
                          ctx->imports[end]->dll_name) == 0)
            end++;
        uint64_t table_size = (end - first + 1U) * 8U;
        for (size_t i = first; i < end; i++)
            ctx->imports[i]->iat_offset =
                    (uint32_t) (address_size + (i - first) * 8U);
        if (!ld_coff_add_ok(address_size, table_size, &address_size) ||
            address_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import address table is too large");
        first = end;
    }
    for (size_t i = 0; i < ctx->import_count; i++) {
        ld_coff_import_t *import = ctx->imports[i];
        if (import->name_type == LD_COFF_IMPORT_ORDINAL) continue;
        if (!ld_coff_align_ok(hint_size, 2U, &hint_size) ||
            hint_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import name table is too large");
        import->hint_name_offset = (uint32_t) hint_size;
        if (!ld_coff_add_ok(hint_size,
                            2U + strlen(import->import_name) + 1U,
                            &hint_size) ||
            hint_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import name table is too large");
    }
    for (size_t first = 0; first < ctx->import_count;) {
        size_t end = first + 1U;
        while (end < ctx->import_count &&
               strcasecmp(ctx->imports[first]->dll_name,
                          ctx->imports[end]->dll_name) == 0)
            end++;
        if (dll_name_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import DLL name table is too large");
        uint32_t dll_offset = (uint32_t) dll_name_size;
        for (size_t i = first; i < end; i++)
            ctx->imports[i]->dll_name_offset = dll_offset;
        if (!ld_coff_add_ok(dll_name_size,
                            strlen(ctx->imports[first]->dll_name) + 1U,
                            &dll_name_size) ||
            dll_name_size > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "import DLL name table is too large");
        first = end;
    }
    const uint32_t idata_characteristics =
            LD_COFF_SCN_CNT_INITIALIZED_DATA | LD_COFF_SCN_MEM_READ |
            LD_COFF_SCN_MEM_WRITE;
    /* Match lld/COFF/Writer.cpp addSyntheticIdata(): keeping each table in
       its standard subsection lets synthetic short imports compose with GNU
       import-library contributions before directory ranges are located. */
    ld_coff_section_t *directories = ld_coff_new_synthetic(
            ctx, ".idata$2", idata_characteristics,
            (uint32_t) descriptor_size, 4U, false);
    ld_coff_section_t *lookups = ld_coff_new_synthetic(
            ctx, ".idata$4", idata_characteristics, (uint32_t) lookup_size,
            8U, false);
    ld_coff_section_t *addresses = ld_coff_new_synthetic(
            ctx, ".idata$5", idata_characteristics, (uint32_t) address_size,
            8U, false);
    ld_coff_section_t *hints = ld_coff_new_synthetic(
            ctx, ".idata$6", idata_characteristics, (uint32_t) hint_size, 2U,
            false);
    ld_coff_section_t *dll_names = ld_coff_new_synthetic(
            ctx, ".idata$7", idata_characteristics, (uint32_t) dll_name_size,
            1U, false);
    if (!directories || !lookups || !addresses || !hints || !dll_names)
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");

    size_t code_count = 0U;
    for (size_t i = 0; i < ctx->import_count; i++)
        if (ctx->imports[i]->type == LD_COFF_IMPORT_CODE) code_count++;
    ld_coff_section_t *thunks = NULL;
    if (code_count) {
        if (code_count > UINT32_MAX / 6U)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "too many import thunks");
        thunks = ld_coff_new_synthetic(
                ctx, ".text$imports",
                LD_COFF_SCN_CNT_CODE | LD_COFF_SCN_MEM_EXECUTE |
                        LD_COFF_SCN_MEM_READ,
                (uint32_t) code_count * 6U, 2U, false);
        if (!thunks) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    uint32_t thunk_offset = 0U;
    for (size_t i = 0; i < ctx->import_count; i++) {
        ld_coff_import_t *import = ctx->imports[i];
        size_t public_length = strlen(import->public_name);
        char *iat_name = malloc(public_length + 7U);
        if (!iat_name) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        memcpy(iat_name, "__imp_", 6U);
        memcpy(iat_name + 6U, import->public_name, public_length + 1U);
        ld_coff_global_t *iat_global =
                ld_coff_get_global(ctx, iat_name, false);
        free(iat_name);
        ld_coff_global_t *public_global =
                ld_coff_get_global(ctx, import->public_name, false);
        if (!iat_global || !public_global)
            return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                "internal import symbol is missing");
        if (iat_global->import == import &&
            iat_global->kind == LD_COFF_GLOBAL_IMPORT_IAT) {
            iat_global->section = addresses;
            iat_global->value = import->iat_offset;
        }
        if (import->type == LD_COFF_IMPORT_CODE) {
            import->thunk_offset = thunk_offset;
            if (public_global->import == import &&
                public_global->kind == LD_COFF_GLOBAL_IMPORT_THUNK) {
                public_global->section = thunks;
                public_global->value = thunk_offset;
            }
            thunk_offset += 6U;
        } else if (public_global->import == import &&
                   public_global->kind == LD_COFF_GLOBAL_IMPORT_IAT) {
            public_global->section = addresses;
            public_global->value = import->iat_offset;
        }
    }
    /* Weak externals in GNU/MinGW import archives create additional public
       and __imp_ aliases for the same import record.  Alias finalization
       happens before synthetic import layout, so propagate the final thunk
       or IAT address to every global that was resolved through this import,
       not only to its canonical short-import name. */
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (!global->import) continue;
        if (global->kind == LD_COFF_GLOBAL_IMPORT_THUNK) {
            global->section = thunks;
            global->value = global->import->thunk_offset;
        } else if (global->kind == LD_COFF_GLOBAL_IMPORT_IAT) {
            global->section = addresses;
            global->value = global->import->iat_offset;
        }
    }
    return LD_OK;
}

static bool ld_coff_writer_section_is_comdat(
        const ld_coff_section_t *section) {
    return section &&
           (section->characteristics & LD_COFF_SCN_LNK_COMDAT) != 0U;
}

static void ld_coff_discard_associative(ld_coff_context_t *ctx) {
    bool changed;
    do {
        changed = false;
        for (size_t i = 0; i < ctx->object_count; i++) {
            ld_coff_object_t *object = ctx->objects[i];
            if (!object->selected || object->import_object) continue;
            for (size_t j = 0; j < object->section_count; j++) {
                ld_coff_section_t *section = &object->sections[j];
                if (section->discarded ||
                    section->comdat_selection !=
                            LD_COFF_COMDAT_ASSOCIATIVE)
                    continue;
                uint32_t parent = section->associative_section;
                if (parent == 0U || parent > object->section_count ||
                    object->sections[parent - 1U].discarded) {
                    section->discarded = true;
                    changed = true;
                }
            }

            /* Clang/GCC MinGW objects commonly encode .pdata$<func>,
               .xdata$<func>, and .eh_frame$<func> as leaderless ANY
               COMDATs rather than standard ASSOCIATIVE COMDATs.  LLD's
               maybeAssociateSEHForMingw() attaches such a contribution to
               the prevailing executable COMDAT with the same `$` suffix;
               if this object lost that code COMDAT, the unwind contribution
               must be discarded as well. */
            for (size_t j = 0; j < object->section_count; j++) {
                ld_coff_section_t *section = &object->sections[j];
                if (section->discarded || section->comdat_key ||
                    !ld_coff_writer_section_is_comdat(section) ||
                    section->comdat_selection ==
                            LD_COFF_COMDAT_ASSOCIATIVE)
                    continue;
                const char *suffix = NULL;
                if (strncmp(section->name, ".pdata$", 7U) == 0)
                    suffix = section->name + 7U;
                else if (strncmp(section->name, ".xdata$", 7U) == 0)
                    suffix = section->name + 7U;
                else if (strncmp(section->name, ".eh_frame$", 10U) == 0)
                    suffix = section->name + 10U;
                if (!suffix || !*suffix) continue;

                bool prevailing_parent = false;
                for (size_t k = 0; k < object->section_count; k++) {
                    ld_coff_section_t *parent = &object->sections[k];
                    if (parent->discarded ||
                        !ld_coff_writer_section_is_comdat(parent) ||
                        (parent->characteristics &
                         LD_COFF_SCN_MEM_EXECUTE) == 0U)
                        continue;
                    const char *dollar = strchr(parent->name, '$');
                    if (dollar && strcmp(dollar + 1U, suffix) == 0) {
                        prevailing_parent = true;
                        break;
                    }
                }
                if (!prevailing_parent) {
                    section->discarded = true;
                    changed = true;
                }
            }
        }
    } while (changed);
}

static bool ld_coff_section_kept(const ld_coff_context_t *ctx,
                                 const ld_coff_section_t *section) {
    if (!section || section->discarded ||
        (section->characteristics &
         (LD_COFF_SCN_LNK_REMOVE | LD_COFF_SCN_LNK_INFO)) != 0U)
        return false;
    if (strcmp(section->name, ".drectve") == 0 ||
        strcmp(section->name, ".llvm_addrsig") == 0)
        return false;
    /* CodeView type merging/PDB emission is intentionally outside this
       backend.  DWARF `.debug_*` sections are handled below. */
    if (strncmp(section->name, ".debug$", 7U) == 0) return false;
    if (ctx->options->debug_mode == LD_DEBUG_NONE &&
        (strncmp(section->name, ".debug", 6U) == 0 ||
         strncmp(section->name, ".zdebug", 7U) == 0))
        return false;
    return section->virtual_size != 0U || section->data_size != 0U;
}

static bool ld_coff_decimal_suffix(const char *input, const char *base,
                                   uint16_t *value) {
    size_t base_length = strlen(base);
    if (strncmp(input, base, base_length) != 0 ||
        input[base_length] != '.' || input[base_length + 1U] == '\0')
        return false;
    uint32_t parsed = 0U;
    for (const char *cursor = input + base_length + 1U; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9') return false;
        unsigned digit = (unsigned) (*cursor - '0');
        if (parsed > (UINT16_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    if (value) *value = (uint16_t) parsed;
    return true;
}

static const char *ld_coff_legacy_family(const char *input) {
    uint16_t unused;
    if (strcmp(input, ".ctors") == 0 ||
        ld_coff_decimal_suffix(input, ".ctors", &unused))
        return ".ctors";
    if (strcmp(input, ".dtors") == 0 ||
        ld_coff_decimal_suffix(input, ".dtors", &unused))
        return ".dtors";
    return NULL;
}

static char *ld_coff_output_name(const char *input) {
    const char *legacy = ld_coff_legacy_family(input);
    if (legacy) return ld_coff_strndup(legacy, strlen(legacy));
    const char *dollar = strchr(input, '$');
    size_t length = dollar ? (size_t) (dollar - input) : strlen(input);
    return ld_coff_strndup(input, length);
}

static ld_coff_output_section_t *ld_coff_find_output(
        ld_coff_context_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->output_count; i++)
        if (strcmp(ctx->outputs[i]->name, name) == 0) return ctx->outputs[i];
    return NULL;
}

static ld_coff_output_section_t *ld_coff_create_output(
        ld_coff_context_t *ctx, const char *name) {
    ld_coff_output_section_t *output = calloc(1, sizeof(*output));
    if (!output) return NULL;
    output->name = ld_coff_strndup(name, strlen(name));
    if (!output->name) {
        free(output);
        return NULL;
    }
    output->alignment = 1U;
    if (ctx->output_count == ctx->output_capacity) {
        size_t next = ctx->output_capacity ? ctx->output_capacity * 2U : 16U;
        ld_coff_output_section_t **items = ld_coff_grow(
                ctx->outputs, ctx->output_capacity, next, sizeof(*items));
        if (!items) {
            free(output->name);
            free(output);
            return NULL;
        }
        ctx->outputs = items;
        ctx->output_capacity = next;
    }
    ctx->outputs[ctx->output_count++] = output;
    return output;
}

static int ld_coff_output_add(ld_coff_context_t *ctx,
                              ld_coff_output_section_t *output,
                              ld_coff_section_t *section) {
    if (output->input_count == output->input_capacity) {
        size_t next = output->input_capacity ? output->input_capacity * 2U : 8U;
        ld_coff_section_t **items = ld_coff_grow(
                output->inputs, output->input_capacity, next, sizeof(*items));
        if (!items) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        output->inputs = items;
        output->input_capacity = next;
    }
    output->inputs[output->input_count++] = section;
    output->characteristics |= section->characteristics;
    output->characteristics &= ~(LD_COFF_SCN_LNK_COMDAT |
                                 LD_COFF_SCN_LNK_NRELOC_OVFL |
                                 LD_COFF_SCN_ALIGN_MASK |
                                 LD_COFF_SCN_LNK_REMOVE |
                                 LD_COFF_SCN_LNK_INFO);
    if (section->alignment > output->alignment)
        output->alignment = section->alignment;
    section->output = output;
    return LD_OK;
}

static int ld_coff_input_section_compare(const void *left_value,
                                         const void *right_value) {
    const ld_coff_section_t *left =
            *(ld_coff_section_t *const *) left_value;
    const ld_coff_section_t *right =
            *(ld_coff_section_t *const *) right_value;
    bool left_head = strstr(left->name, "$__nature_head") != NULL;
    bool right_head = strstr(right->name, "$__nature_head") != NULL;
    if (left_head != right_head) return left_head ? -1 : 1;
    bool left_tail = strstr(left->name, "$__nature_tail") != NULL;
    bool right_tail = strstr(right->name, "$__nature_tail") != NULL;
    if (left_tail != right_tail) return left_tail ? 1 : -1;
    const char *left_family = ld_coff_legacy_family(left->name);
    const char *right_family = ld_coff_legacy_family(right->name);
    if (left_family && right_family &&
        strcmp(left_family, right_family) == 0) {
        int32_t left_priority = -1;
        int32_t right_priority = -1;
        uint16_t suffix;
        if (ld_coff_decimal_suffix(left->name, left_family, &suffix))
            left_priority = (int32_t) UINT16_MAX - suffix;
        if (ld_coff_decimal_suffix(right->name, right_family, &suffix))
            right_priority = (int32_t) UINT16_MAX - suffix;
        if (left_priority != right_priority)
            return (left_priority > right_priority) -
                   (left_priority < right_priority);
    } else if (left_family != NULL || right_family != NULL) {
        return left_family ? -1 : 1;
    }
    int compare = strcmp(left->name, right->name);
    if (compare) return compare;
    return (left->input_order > right->input_order) -
           (left->input_order < right->input_order);
}

static int ld_coff_output_rank(const char *name) {
    if (strcmp(name, ".text") == 0) return 0;
    if (strcmp(name, ".rdata") == 0) return 1;
    if (strcmp(name, ".data") == 0) return 2;
    if (strcmp(name, ".bss") == 0) return 3;
    if (strcmp(name, ".pdata") == 0) return 4;
    if (strcmp(name, ".xdata") == 0) return 5;
    if (strcmp(name, ".idata") == 0) return 6;
    if (strcmp(name, ".CRT") == 0) return 7;
    if (strcmp(name, ".tls") == 0) return 8;
    /* .reloc is materialized after ordinary relocations have been applied.
       Keep it last so changing its initially empty size cannot overlap a
       following retained DWARF section. */
    if (strncmp(name, ".debug", 6U) == 0 ||
        strncmp(name, ".zdebug", 7U) == 0)
        return 100;
    if (strcmp(name, ".reloc") == 0) return 110;
    return 20;
}

static int ld_coff_output_compare(const void *left_value,
                                  const void *right_value) {
    const ld_coff_output_section_t *left =
            *(ld_coff_output_section_t *const *) left_value;
    const ld_coff_output_section_t *right =
            *(ld_coff_output_section_t *const *) right_value;
    int left_rank = ld_coff_output_rank(left->name);
    int right_rank = ld_coff_output_rank(right->name);
    if (left_rank != right_rank) return left_rank - right_rank;
    return strcmp(left->name, right->name);
}

static int ld_coff_collect_sections(ld_coff_context_t *ctx) {
    ld_coff_discard_associative(ctx);
    for (size_t i = 0; i < ctx->object_count; i++) {
        ld_coff_object_t *object = ctx->objects[i];
        if (!object->selected || object->import_object) continue;
        for (size_t j = 0; j < object->section_count; j++) {
            ld_coff_section_t *section = &object->sections[j];
            if (!ld_coff_section_kept(ctx, section)) continue;
            char *name = ld_coff_output_name(section->name);
            if (!name) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            ld_coff_output_section_t *output =
                    ld_coff_find_output(ctx, name);
            if (!output) output = ld_coff_create_output(ctx, name);
            free(name);
            if (!output)
                return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            int status = ld_coff_output_add(ctx, output, section);
            if (status != LD_OK) return status;
        }
    }
    for (size_t i = 0; i < ctx->synthetic_count; i++) {
        ld_coff_section_t *section = ctx->synthetic_sections[i];
        if (!ld_coff_section_kept(ctx, section)) continue;
        char *name = ld_coff_output_name(section->name);
        if (!name) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ld_coff_output_section_t *output = ld_coff_find_output(ctx, name);
        if (!output) output = ld_coff_create_output(ctx, name);
        free(name);
        if (!output) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        int status = ld_coff_output_add(ctx, output, section);
        if (status != LD_OK) return status;
    }
    if (ctx->options->pie) {
        const char *name = ".reloc";
        if (!ld_coff_find_output(ctx, name) &&
            !ld_coff_create_output(ctx, name))
            return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ld_coff_output_section_t *reloc = ld_coff_find_output(ctx, name);
        reloc->characteristics = LD_COFF_SCN_CNT_INITIALIZED_DATA |
                                 LD_COFF_SCN_MEM_READ |
                                 LD_COFF_SCN_MEM_DISCARDABLE;
        reloc->alignment = 4U;
    }
    for (size_t i = 0; i < ctx->output_count; i++)
        qsort(ctx->outputs[i]->inputs, ctx->outputs[i]->input_count,
              sizeof(*ctx->outputs[i]->inputs), ld_coff_input_section_compare);
    qsort(ctx->outputs, ctx->output_count, sizeof(*ctx->outputs),
          ld_coff_output_compare);
    for (size_t i = 0; i < ctx->output_count; i++)
        ctx->outputs[i]->index = (uint32_t) i + 1U;
    return LD_OK;
}

static int ld_coff_layout(ld_coff_context_t *ctx) {
    uint64_t headers = LD_PE_OFFSET + 4U + LD_PE_COFF_HEADER_SIZE +
                       LD_PE_OPTIONAL_HEADER64_SIZE +
                       ctx->output_count * LD_PE_SECTION_HEADER_SIZE;
    uint64_t aligned_headers;
    if (!ld_coff_align_ok(headers, ctx->file_alignment, &aligned_headers) ||
        aligned_headers > UINT32_MAX)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "PE headers exceed format limits");
    ctx->size_of_headers = (uint32_t) aligned_headers;
    uint32_t rva = ctx->section_alignment;
    uint32_t file_offset = ctx->size_of_headers;
    for (size_t i = 0; i < ctx->output_count; i++) {
        ld_coff_output_section_t *output = ctx->outputs[i];
        uint64_t cursor = 0U;
        uint64_t raw_end = 0U;
        for (size_t j = 0; j < output->input_count; j++) {
            ld_coff_section_t *input = output->inputs[j];
            uint64_t aligned;
            uint32_t alignment = input->alignment ? input->alignment : 1U;
            if (!ld_coff_align_ok(cursor, alignment, &aligned) ||
                aligned > UINT32_MAX)
                return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                    "section '%s' is too large", output->name);
            input->output_offset = (uint32_t) aligned;
            cursor = aligned + input->virtual_size;
            if (cursor > UINT32_MAX)
                return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                    "section '%s' is too large", output->name);
            if (input->data_size) {
                uint64_t input_raw_end = aligned + input->data_size;
                if (input_raw_end > raw_end) raw_end = input_raw_end;
            }
        }
        output->virtual_size = (uint32_t) cursor;
        uint64_t aligned_raw;
        if (!ld_coff_align_ok(raw_end, ctx->file_alignment, &aligned_raw) ||
            aligned_raw > UINT32_MAX)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "raw section '%s' is too large", output->name);
        output->raw_size = (uint32_t) aligned_raw;
        output->rva = rva;
        output->file_offset = output->raw_size ? file_offset : 0U;
        if (!ld_coff_u32_align(rva + output->virtual_size,
                               ctx->section_alignment, &rva) ||
            output->raw_size > UINT32_MAX - file_offset)
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "PE image exceeds 4 GiB");
        file_offset += output->raw_size;
        free(output->data);
        output->data = output->raw_size ? calloc(1, output->raw_size) : NULL;
        if (output->raw_size && !output->data)
            return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        for (size_t j = 0; j < output->input_count; j++) {
            ld_coff_section_t *input = output->inputs[j];
            if (input->data_size)
                memcpy(output->data + input->output_offset, input->data,
                       input->data_size);
        }
    }
    ctx->size_of_image = rva;
    return LD_OK;
}

static void ld_coff_bind_output_boundary(ld_coff_context_t *ctx,
                                         const char *start_name,
                                         const char *end_name,
                                         const char *output_name) {
    ld_coff_output_section_t *output =
            ld_coff_find_output(ctx, output_name);
    if (!output || output->input_count == 0U) return;
    ld_coff_section_t *first = output->inputs[0];
    ld_coff_section_t *last = output->inputs[output->input_count - 1U];
    ld_coff_global_t *start =
            ld_coff_get_global(ctx, start_name, false);
    ld_coff_global_t *end = ld_coff_get_global(ctx, end_name, false);
    if (start && start->kind == LD_COFF_GLOBAL_DEFINED && !start->section) {
        start->section = first;
        start->value = 0U;
    }
    if (end && end->kind == LD_COFF_GLOBAL_DEFINED && !end->section) {
        end->section = last;
        end->value = last->virtual_size;
    }
}

static void ld_coff_bind_mingw_boundaries(ld_coff_context_t *ctx) {
    ld_coff_bind_output_boundary(ctx, "__data_start__", "__data_end__",
                                 ".data");
    ld_coff_bind_output_boundary(ctx, "__bss_start__", "__bss_end__",
                                 ".bss");

    /* An empty pseudo-relocation table is represented by identical valid
       addresses. MinGW's runtime relocator treats head == end as no work. */
    ld_coff_global_t *head = ld_coff_get_global(
            ctx, "__RUNTIME_PSEUDO_RELOC_LIST__", false);
    ld_coff_global_t *end = ld_coff_get_global(
            ctx, "__RUNTIME_PSEUDO_RELOC_LIST_END__", false);
    ld_coff_output_section_t *output = ld_coff_find_output(ctx, ".rdata");
    if (!output) output = ld_coff_find_output(ctx, ".text");
    if (!output || output->input_count == 0U) return;
    ld_coff_section_t *anchor = output->inputs[0];
    if (head && head->kind == LD_COFF_GLOBAL_DEFINED && !head->section) {
        head->section = anchor;
        head->value = 0U;
    }
    if (end && end->kind == LD_COFF_GLOBAL_DEFINED && !end->section) {
        end->section = anchor;
        end->value = 0U;
    }
}

static bool ld_coff_symbol_address(ld_coff_context_t *ctx,
                                   ld_coff_object_t *object,
                                   ld_coff_symbol_t *symbol, uint64_t *rva,
                                   ld_coff_output_section_t **output,
                                   bool *absolute) {
    (void) ctx;
    *output = NULL;
    *absolute = false;
    if (!symbol || symbol->auxiliary) return false;
    bool external = symbol->storage_class == LD_COFF_STORAGE_CLASS_EXTERNAL ||
                    symbol->storage_class ==
                            LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL;
    if (external) {
        ld_coff_global_t *global = symbol->global;
        if (!global && symbol->name)
            global = ld_coff_get_global(ctx, symbol->name, false);
        if (!global || global->kind == LD_COFF_GLOBAL_UNDEFINED) return false;
        if (global->kind == LD_COFF_GLOBAL_ABSOLUTE) {
            *rva = global->value;
            *absolute = true;
            return true;
        }
        if (global->section) {
            if (!global->section->output) return false;
            *output = global->section->output;
            *rva = (*output)->rva + global->section->output_offset +
                   global->value;
            return true;
        }
        *rva = global->value;
        return true;
    }
    if (symbol->section_number == LD_COFF_SYM_ABSOLUTE) {
        *rva = symbol->value;
        *absolute = true;
        return true;
    }
    if (!symbol->section || symbol->section->discarded ||
        !symbol->section->output || symbol->section->object != object)
        return false;
    *output = symbol->section->output;
    *rva = (*output)->rva + symbol->section->output_offset + symbol->value;
    return true;
}

static int ld_coff_push_base_relocation(ld_coff_context_t *ctx,
                                        uint32_t rva) {
    if (ctx->base_relocation_count == ctx->base_relocation_capacity) {
        size_t next = ctx->base_relocation_capacity
                              ? ctx->base_relocation_capacity * 2U
                              : 64U;
        uint32_t *items = ld_coff_grow(
                ctx->base_relocations, ctx->base_relocation_capacity, next,
                sizeof(*items));
        if (!items) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ctx->base_relocations = items;
        ctx->base_relocation_capacity = next;
    }
    ctx->base_relocations[ctx->base_relocation_count++] = rva;
    return LD_OK;
}

static bool ld_coff_add_signed_u64(uint64_t base, int64_t addend,
                                   uint64_t *result) {
    if (addend >= 0) {
        uint64_t positive = (uint64_t) addend;
        if (base > UINT64_MAX - positive) return false;
        *result = base + positive;
        return true;
    }
    uint64_t magnitude = (uint64_t) (-(addend + 1)) + 1U;
    if (base < magnitude) return false;
    *result = base - magnitude;
    return true;
}

static int ld_coff_apply_one_relocation(
        ld_coff_context_t *ctx, ld_coff_section_t *section,
        const ld_coff_relocation_t *relocation) {
    ld_coff_object_t *object = section->object;
    if (!object || relocation->symbol_index >= object->symbol_count) {
        char symbol_name[64];
        snprintf(symbol_name, sizeof(symbol_name), "<index %u>",
                 relocation->symbol_index);
        return ld_coff_relocation_fail(
                ctx, LD_INVALID_INPUT, object, section, relocation,
                symbol_name, "invalid relocation symbol index");
    }
    size_t width;
    switch (relocation->type) {
        case LD_COFF_REL_AMD64_ADDR64:
            width = 8U;
            break;
        case LD_COFF_REL_AMD64_SECTION:
            width = 2U;
            break;
        case LD_COFF_REL_AMD64_ABSOLUTE:
            return LD_OK;
        default:
            width = 4U;
            break;
    }
    if (relocation->offset > section->data_size ||
        width > section->data_size - relocation->offset)
        return ld_coff_relocation_fail(
                ctx, LD_RELOCATION_ERROR, object, section, relocation,
                object->symbols[relocation->symbol_index].name,
                "relocation field extends past section");
    ld_coff_symbol_t *symbol = &object->symbols[relocation->symbol_index];
    uint64_t symbol_rva;
    ld_coff_output_section_t *symbol_output;
    bool absolute;
    if (!ld_coff_symbol_address(ctx, object, symbol, &symbol_rva,
                                &symbol_output, &absolute))
        return ld_coff_relocation_fail(
                ctx, LD_RELOCATION_ERROR, object, section, relocation,
                symbol->name ? symbol->name : "<auxiliary>",
                "cannot resolve relocation target");
    uint8_t *field = section->output->data + section->output_offset +
                     relocation->offset;
    ld_coff_view_t field_view = {field, width};
    uint32_t place = section->output->rva + section->output_offset +
                     relocation->offset;
    uint32_t addend32 = 0U;
    uint64_t addend64 = 0U;
    uint16_t addend16 = 0U;
    if (width == 8U)
        ld_coff_read_u64(field_view, 0, &addend64);
    else if (width == 4U)
        ld_coff_read_u32(field_view, 0, &addend32);
    else
        ld_coff_read_u16(field_view, 0, &addend16);

    switch (relocation->type) {
        case LD_COFF_REL_AMD64_ADDR64: {
            uint64_t base = absolute ? 0U : ctx->image_base;
            int64_t signed_addend;
            memcpy(&signed_addend, &addend64, sizeof(signed_addend));
            uint64_t value;
            if (symbol_rva > UINT64_MAX - base ||
                !ld_coff_add_signed_u64(symbol_rva + base, signed_addend,
                                        &value))
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "relocation result overflows 64 bits");
            ld_coff_write_u64(field, width, 0, value);
            if (ctx->options->pie && !absolute &&
                (section->output->characteristics &
                 LD_COFF_SCN_MEM_DISCARDABLE) == 0U &&
                strncmp(section->name, ".debug", 6U) != 0 &&
                strncmp(section->name, ".zdebug", 7U) != 0)
                return ld_coff_push_base_relocation(ctx, place);
            return LD_OK;
        }
        case LD_COFF_REL_AMD64_ADDR32: {
            int32_t signed_addend;
            memcpy(&signed_addend, &addend32, sizeof(signed_addend));
            uint64_t base = symbol_rva + (absolute ? 0U : ctx->image_base);
            uint64_t value;
            if (!ld_coff_add_signed_u64(base, signed_addend, &value))
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "relocation result underflows 32 bits");
            if (value > UINT32_MAX)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "relocation result overflows 32 bits");
            ld_coff_write_u32(field, width, 0, (uint32_t) value);
            return LD_OK;
        }
        case LD_COFF_REL_AMD64_ADDR32NB: {
            int32_t signed_addend;
            memcpy(&signed_addend, &addend32, sizeof(signed_addend));
            uint64_t value;
            if (!ld_coff_add_signed_u64(symbol_rva, signed_addend, &value))
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "relocation result underflows 32-bit RVA");
            if (value > UINT32_MAX)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "relocation result overflows 32-bit RVA");
            ld_coff_write_u32(field, width, 0, (uint32_t) value);
            return LD_OK;
        }
        case LD_COFF_REL_AMD64_REL32:
        case LD_COFF_REL_AMD64_REL32_1:
        case LD_COFF_REL_AMD64_REL32_2:
        case LD_COFF_REL_AMD64_REL32_3:
        case LD_COFF_REL_AMD64_REL32_4:
        case LD_COFF_REL_AMD64_REL32_5: {
            int32_t signed_addend;
            memcpy(&signed_addend, &addend32, sizeof(signed_addend));
            unsigned extra = relocation->type - LD_COFF_REL_AMD64_REL32;
            int64_t value = (int64_t) symbol_rva + signed_addend - place - 4 -
                            (int64_t) extra;
            if (value < INT32_MIN || value > INT32_MAX)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "PC-relative displacement is outside int32 range");
            ld_coff_write_u32(field, width, 0, (uint32_t) (int32_t) value);
            return LD_OK;
        }
        case LD_COFF_REL_AMD64_SECTION: {
            if (!symbol_output)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "target has no output section");
            int16_t signed_addend;
            memcpy(&signed_addend, &addend16, sizeof(signed_addend));
            uint64_t value;
            if (!ld_coff_add_signed_u64(symbol_output->index, signed_addend,
                                        &value))
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "section index underflow");
            if (value > UINT16_MAX)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "section index overflow");
            ld_coff_write_u16(field, width, 0, (uint16_t) value);
            return LD_OK;
        }
        case LD_COFF_REL_AMD64_SECREL: {
            if (!symbol_output)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "target has no output section");
            int32_t signed_addend;
            memcpy(&signed_addend, &addend32, sizeof(signed_addend));
            uint64_t value;
            if (!ld_coff_add_signed_u64(symbol_rva - symbol_output->rva,
                                        signed_addend, &value))
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "section-relative offset underflow");
            if (value > UINT32_MAX)
                return ld_coff_relocation_fail(
                        ctx, LD_RELOCATION_ERROR, object, section, relocation,
                        symbol->name, "section-relative offset overflow");
            ld_coff_write_u32(field, width, 0, (uint32_t) value);
            return LD_OK;
        }
        default:
            return ld_coff_relocation_fail(
                    ctx, LD_UNSUPPORTED, object, section, relocation,
                    symbol->name ? symbol->name : "<auxiliary>",
                    "unsupported AMD64 relocation");
    }
}

static int ld_coff_apply_relocations(ld_coff_context_t *ctx) {
    for (size_t i = 0; i < ctx->object_count; i++) {
        ld_coff_object_t *object = ctx->objects[i];
        if (!object->selected || object->import_object) continue;
        for (size_t j = 0; j < object->section_count; j++) {
            ld_coff_section_t *section = &object->sections[j];
            if (!section->output || section->discarded) continue;
            for (size_t k = 0; k < section->relocation_count; k++) {
                int status = ld_coff_apply_one_relocation(
                        ctx, section, &section->relocations[k]);
                if (status != LD_OK) return status;
            }
        }
    }
    return LD_OK;
}

static ld_coff_output_section_t *ld_coff_output_named(
        ld_coff_context_t *ctx, const char *name) {
    for (size_t i = 0; i < ctx->output_count; i++)
        if (strcmp(ctx->outputs[i]->name, name) == 0) return ctx->outputs[i];
    return NULL;
}

static int ld_coff_fill_imports(ld_coff_context_t *ctx) {
    if (ctx->import_count == 0U) return LD_OK;
    ld_coff_section_t *directories = NULL;
    ld_coff_section_t *lookups = NULL;
    ld_coff_section_t *addresses = NULL;
    ld_coff_section_t *hints = NULL;
    ld_coff_section_t *dll_names = NULL;
    ld_coff_section_t *thunks = NULL;
    for (size_t i = 0; i < ctx->synthetic_count; i++) {
        if (strcmp(ctx->synthetic_sections[i]->name, ".idata$2") == 0)
            directories = ctx->synthetic_sections[i];
        else if (strcmp(ctx->synthetic_sections[i]->name, ".idata$4") == 0)
            lookups = ctx->synthetic_sections[i];
        else if (strcmp(ctx->synthetic_sections[i]->name, ".idata$5") == 0)
            addresses = ctx->synthetic_sections[i];
        else if (strcmp(ctx->synthetic_sections[i]->name, ".idata$6") == 0)
            hints = ctx->synthetic_sections[i];
        else if (strcmp(ctx->synthetic_sections[i]->name, ".idata$7") == 0)
            dll_names = ctx->synthetic_sections[i];
        else if (strcmp(ctx->synthetic_sections[i]->name,
                        ".text$imports") == 0)
            thunks = ctx->synthetic_sections[i];
    }
    if (!directories || !lookups || !addresses || !hints || !dll_names ||
        !directories->output || !lookups->output || !addresses->output ||
        !dll_names->output)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "internal import section is missing");
    uint8_t *directory_data =
            directories->output->data + directories->output_offset;
    uint8_t *lookup_data = lookups->output->data + lookups->output_offset;
    uint8_t *address_data =
            addresses->output->data + addresses->output_offset;
    uint8_t *hint_data = hints->data_size
                                 ? hints->output->data + hints->output_offset
                                 : NULL;
    uint8_t *dll_data = dll_names->output->data + dll_names->output_offset;
    uint32_t lookup_rva = lookups->output->rva + lookups->output_offset;
    uint32_t address_rva = addresses->output->rva + addresses->output_offset;
    uint32_t hint_rva = hints->output
                                ? hints->output->rva + hints->output_offset
                                : 0U;
    uint32_t dll_rva =
            dll_names->output->rva + dll_names->output_offset;
    for (size_t i = 0; i < ctx->import_count; i++) {
        ld_coff_import_t *import = ctx->imports[i];
        uint64_t entry = import->name_type == LD_COFF_IMPORT_ORDINAL
                                 ? UINT64_C(0x8000000000000000) |
                                           import->ordinal_hint
                                 : hint_rva + import->hint_name_offset;
        ld_coff_write_u64(lookup_data, lookups->data_size,
                          import->ilt_offset, entry);
        ld_coff_write_u64(address_data, addresses->data_size,
                          import->iat_offset, entry);
        if (import->name_type != LD_COFF_IMPORT_ORDINAL) {
            ld_coff_write_u16(hint_data, hints->data_size,
                              import->hint_name_offset,
                              import->ordinal_hint);
            memcpy(hint_data + import->hint_name_offset + 2U,
                   import->import_name, strlen(import->import_name) + 1U);
        }
    }
    for (size_t first = 0; first < ctx->import_count;) {
        size_t end = first + 1U;
        while (end < ctx->import_count &&
               strcasecmp(ctx->imports[first]->dll_name,
                          ctx->imports[end]->dll_name) == 0)
            end++;
        ld_coff_import_t *head = ctx->imports[first];
        uint32_t descriptor = head->descriptor_index * 20U;
        ld_coff_write_u32(directory_data, directories->data_size, descriptor,
                          lookup_rva + head->ilt_offset);
        ld_coff_write_u32(directory_data, directories->data_size,
                          descriptor + 12U,
                          dll_rva + head->dll_name_offset);
        ld_coff_write_u32(directory_data, directories->data_size,
                          descriptor + 16U,
                          address_rva + head->iat_offset);
        memcpy(dll_data + head->dll_name_offset, head->dll_name,
               strlen(head->dll_name) + 1U);
        first = end;
    }
    if (thunks) {
        uint8_t *code = thunks->output->data + thunks->output_offset;
        uint32_t code_rva = thunks->output->rva + thunks->output_offset;
        for (size_t i = 0; i < ctx->import_count; i++) {
            ld_coff_import_t *import = ctx->imports[i];
            if (import->type != LD_COFF_IMPORT_CODE) continue;
            uint32_t offset = import->thunk_offset;
            int64_t displacement =
                    (int64_t) address_rva + import->iat_offset -
                    ((int64_t) code_rva + offset + 6);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
                return ld_coff_fail(ctx, LD_RELOCATION_ERROR,
                                    "import thunk displacement overflow");
            code[offset] = 0xffU;
            code[offset + 1U] = 0x25U;
            ld_coff_write_u32(code, thunks->data_size, offset + 2U,
                              (uint32_t) (int32_t) displacement);
        }
    }
    return LD_OK;
}

static int ld_coff_u32_compare(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *) left;
    uint32_t b = *(const uint32_t *) right;
    return (a > b) - (a < b);
}

static int ld_coff_build_base_relocations(ld_coff_context_t *ctx) {
    ld_coff_output_section_t *output = ld_coff_output_named(ctx, ".reloc");
    if (!output) return LD_OK;
    if (ctx->base_relocation_count > 1U)
        qsort(ctx->base_relocations, ctx->base_relocation_count,
              sizeof(*ctx->base_relocations), ld_coff_u32_compare);
    size_t unique = 0U;
    for (size_t i = 0; i < ctx->base_relocation_count; i++) {
        if (unique == 0U ||
            ctx->base_relocations[i] != ctx->base_relocations[unique - 1U])
            ctx->base_relocations[unique++] = ctx->base_relocations[i];
    }
    ctx->base_relocation_count = unique;
    /* Keep a valid ABSOLUTE-only block when no fixups are required.  The
       image remains fully position-independent, while the PE contract still
       advertises ASLR and supplies a non-empty relocation directory. */
    uint64_t size = unique == 0U ? 12U : 0U;
    for (size_t first = 0; first < unique;) {
        uint32_t page = ctx->base_relocations[first] & ~0xfffU;
        size_t end = first + 1U;
        while (end < unique &&
               (ctx->base_relocations[end] & ~0xfffU) == page)
            end++;
        size_t entries = end - first;
        size += 8U + ((entries + 1U) & ~1U) * 2U;
        first = end;
    }
    if (size > UINT32_MAX)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "base relocation table is too large");
    free(output->data);
    uint32_t raw_size;
    if (!ld_coff_u32_align((uint32_t) size, ctx->file_alignment, &raw_size))
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "base relocation table is too large");
    output->data = calloc(1, raw_size);
    if (!output->data) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    output->virtual_size = (uint32_t) size;
    output->raw_size = raw_size;
    if (output->file_offset == 0U) {
        uint32_t end = ctx->size_of_headers;
        for (size_t i = 0; i < ctx->output_count; i++) {
            ld_coff_output_section_t *other = ctx->outputs[i];
            if (other != output && other->raw_size &&
                other->file_offset + other->raw_size > end)
                end = other->file_offset + other->raw_size;
        }
        output->file_offset = end;
    }
    size_t cursor = 0U;
    if (unique == 0U) {
        ld_coff_write_u32(output->data, output->raw_size, 0U, 0U);
        ld_coff_write_u32(output->data, output->raw_size, 4U, 12U);
        cursor = 12U;
    }
    for (size_t first = 0; first < unique;) {
        uint32_t page = ctx->base_relocations[first] & ~0xfffU;
        size_t end = first + 1U;
        while (end < unique &&
               (ctx->base_relocations[end] & ~0xfffU) == page)
            end++;
        size_t entries = end - first;
        uint32_t block_size = (uint32_t) (8U + ((entries + 1U) & ~1U) * 2U);
        ld_coff_write_u32(output->data, output->raw_size, cursor, page);
        ld_coff_write_u32(output->data, output->raw_size, cursor + 4U,
                          block_size);
        for (size_t i = first; i < end; i++) {
            uint16_t item = (uint16_t) (LD_PE_BASE_RELOC_DIR64 << 12U) |
                            (uint16_t) (ctx->base_relocations[i] - page);
            ld_coff_write_u16(output->data, output->raw_size,
                              cursor + 8U + (i - first) * 2U, item);
        }
        cursor += block_size;
        first = end;
    }
    if (!ld_coff_u32_align(output->rva + output->virtual_size,
                           ctx->section_alignment, &ctx->size_of_image))
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR, "PE image too large");
    return LD_OK;
}

static int ld_coff_runtime_function_compare(const void *left,
                                            const void *right) {
    uint32_t a, b;
    memcpy(&a, left, sizeof(a));
    memcpy(&b, right, sizeof(b));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    a = __builtin_bswap32(a);
    b = __builtin_bswap32(b);
#endif
    return (a > b) - (a < b);
}

static int ld_coff_sort_pdata(ld_coff_context_t *ctx) {
    ld_coff_output_section_t *pdata = ld_coff_output_named(ctx, ".pdata");
    if (!pdata || pdata->virtual_size == 0U) return LD_OK;
    if (pdata->virtual_size % 12U != 0U)
        return ld_coff_fail(ctx, LD_INVALID_INPUT,
                            ".pdata size is not a multiple of 12 bytes");
    qsort(pdata->data, pdata->virtual_size / 12U, 12U,
          ld_coff_runtime_function_compare);
    for (uint32_t offset = 0; offset < pdata->virtual_size; offset += 12U) {
        ld_coff_view_t view = {pdata->data, pdata->raw_size};
        uint32_t begin, end;
        ld_coff_read_u32(view, offset, &begin);
        ld_coff_read_u32(view, offset + 4U, &end);
        if (begin >= end || end >= ctx->size_of_image)
            return ld_coff_fail(ctx, LD_INVALID_INPUT,
                                ".pdata contains an invalid function range");
    }
    return LD_OK;
}

static bool ld_coff_global_rva(ld_coff_context_t *ctx, const char *name,
                               uint32_t *rva) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, name, false);
    if (!global || global->kind == LD_COFF_GLOBAL_UNDEFINED ||
        !global->section || !global->section->output)
        return false;
    uint64_t value = global->section->output->rva +
                     global->section->output_offset + global->value;
    if (value > UINT32_MAX) return false;
    *rva = (uint32_t) value;
    return true;
}

static int ld_coff_resolve_entry(ld_coff_context_t *ctx) {
    const char *name = ctx->options->entry_symbol;
    if (!name || !*name) name = "mainCRTStartup";
    ld_coff_global_t *entry = ld_coff_get_global(ctx, name, false);
    if (!entry || entry->kind == LD_COFF_GLOBAL_UNDEFINED)
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "entry symbol '%s' is undefined", name);
    if (!entry->section || !entry->section->output)
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "entry symbol '%s' has no mapped section", name);
    uint64_t value = entry->section->output->rva + entry->section->output_offset +
                     entry->value;
    if (value > UINT32_MAX)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "entry symbol is outside PE address space");
    ctx->entry_rva = (uint32_t) value;
    return LD_OK;
}

static void ld_coff_write_directory(uint8_t *image, size_t size,
                                    unsigned index, uint32_t rva,
                                    uint32_t directory_size) {
    uint64_t offset = LD_PE_OFFSET + 4U + LD_PE_COFF_HEADER_SIZE + 112U +
                      index * 8U;
    ld_coff_write_u32(image, size, offset, rva);
    ld_coff_write_u32(image, size, offset + 4U, directory_size);
}

static int ld_coff_write_pe(ld_coff_context_t *ctx, uint8_t **result,
                            size_t *result_size) {
    uint64_t image_size = ctx->size_of_headers;
    for (size_t i = 0; i < ctx->output_count; i++) {
        ld_coff_output_section_t *output = ctx->outputs[i];
        if (output->raw_size) {
            uint64_t end = (uint64_t) output->file_offset + output->raw_size;
            if (end > image_size) image_size = end;
        }
    }
    uint32_t *section_name_offsets =
            ctx->output_count ? calloc(ctx->output_count, sizeof(uint32_t))
                              : NULL;
    if (ctx->output_count && !section_name_offsets)
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    uint64_t string_table_size = 4U;
    for (size_t i = 0; i < ctx->output_count; i++) {
        ld_coff_output_section_t *output = ctx->outputs[i];
        size_t name_length = strlen(output->name);
        if (name_length <= 8U ||
            (output->characteristics & LD_COFF_SCN_MEM_DISCARDABLE) == 0U)
            continue;
        if (string_table_size > 9999999U ||
            name_length > UINT32_MAX - string_table_size - 1U) {
            free(section_name_offsets);
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "PE debug section name table is too large");
        }
        section_name_offsets[i] = (uint32_t) string_table_size;
        string_table_size += name_length + 1U;
    }
    uint32_t string_table_offset = 0U;
    if (string_table_size > 4U) {
        if (image_size > UINT32_MAX ||
            string_table_size > UINT32_MAX - image_size) {
            free(section_name_offsets);
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "PE string table is too large");
        }
        string_table_offset = (uint32_t) image_size;
        image_size += string_table_size;
        uint64_t aligned_size;
        if (!ld_coff_align_ok(image_size, ctx->file_alignment,
                              &aligned_size)) {
            free(section_name_offsets);
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "PE output size overflow");
        }
        image_size = aligned_size;
    }
    if (image_size > SIZE_MAX || image_size > UINT32_MAX) {
        free(section_name_offsets);
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "PE output is too large");
    }
    uint8_t *image = calloc(1, (size_t) image_size);
    if (!image) {
        free(section_name_offsets);
        return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    image[0] = 'M';
    image[1] = 'Z';
    ld_coff_write_u32(image, (size_t) image_size, 0x3cU, LD_PE_OFFSET);
    memcpy(image + LD_PE_OFFSET, "PE\0\0", 4U);
    uint64_t coff = LD_PE_OFFSET + 4U;
    ld_coff_write_u16(image, (size_t) image_size, coff,
                      LD_COFF_MACHINE_AMD64);
    ld_coff_write_u16(image, (size_t) image_size, coff + 2U,
                      (uint16_t) ctx->output_count);
    if (string_table_offset != 0U) {
        ld_coff_write_u32(image, (size_t) image_size, coff + 8U,
                          string_table_offset);
        ld_coff_write_u32(image, (size_t) image_size, string_table_offset,
                          (uint32_t) string_table_size);
    }
    ld_coff_write_u16(image, (size_t) image_size, coff + 16U,
                      LD_PE_OPTIONAL_HEADER64_SIZE);
    ld_coff_output_section_t *base_reloc =
            ld_coff_output_named(ctx, ".reloc");
    bool has_base_relocations = base_reloc && base_reloc->virtual_size != 0U;
    uint16_t file_characteristics = LD_PE_FILE_EXECUTABLE_IMAGE |
                                    LD_PE_FILE_LARGE_ADDRESS_AWARE;
    if (!has_base_relocations)
        file_characteristics |= LD_PE_FILE_RELOCS_STRIPPED;
    ld_coff_write_u16(image, (size_t) image_size, coff + 18U,
                      file_characteristics);
    uint64_t optional = coff + LD_PE_COFF_HEADER_SIZE;
    ld_coff_write_u16(image, (size_t) image_size, optional,
                      LD_PE_OPTIONAL_MAGIC_PE32_PLUS);
    image[optional + 2U] = 1U;
    uint32_t size_code = 0U, size_initialized = 0U, size_uninitialized = 0U;
    uint32_t base_code = 0U;
    for (size_t i = 0; i < ctx->output_count; i++) {
        ld_coff_output_section_t *output = ctx->outputs[i];
        if ((output->characteristics & LD_COFF_SCN_CNT_CODE) != 0U) {
            size_code += output->raw_size;
            if (base_code == 0U) base_code = output->rva;
        }
        if ((output->characteristics &
             LD_COFF_SCN_CNT_INITIALIZED_DATA) != 0U)
            size_initialized += output->raw_size;
        if ((output->characteristics &
             LD_COFF_SCN_CNT_UNINITIALIZED_DATA) != 0U)
            size_uninitialized += output->virtual_size;
    }
    ld_coff_write_u32(image, (size_t) image_size, optional + 4U, size_code);
    ld_coff_write_u32(image, (size_t) image_size, optional + 8U,
                      size_initialized);
    ld_coff_write_u32(image, (size_t) image_size, optional + 12U,
                      size_uninitialized);
    ld_coff_write_u32(image, (size_t) image_size, optional + 16U,
                      ctx->entry_rva);
    ld_coff_write_u32(image, (size_t) image_size, optional + 20U, base_code);
    ld_coff_write_u64(image, (size_t) image_size, optional + 24U,
                      ctx->image_base);
    ld_coff_write_u32(image, (size_t) image_size, optional + 32U,
                      ctx->section_alignment);
    ld_coff_write_u32(image, (size_t) image_size, optional + 36U,
                      ctx->file_alignment);
    ld_coff_write_u16(image, (size_t) image_size, optional + 40U, 6U);
    ld_coff_write_u16(image, (size_t) image_size, optional + 44U, 1U);
    ld_coff_write_u16(image, (size_t) image_size, optional + 48U, 6U);
    ld_coff_write_u32(image, (size_t) image_size, optional + 52U, 0U);
    ld_coff_write_u32(image, (size_t) image_size, optional + 56U,
                      ctx->size_of_image);
    ld_coff_write_u32(image, (size_t) image_size, optional + 60U,
                      ctx->size_of_headers);
    ld_coff_write_u16(image, (size_t) image_size, optional + 68U,
                      LD_PE_SUBSYSTEM_WINDOWS_CUI);
    uint16_t dll_characteristics = LD_PE_DLL_NX_COMPAT |
                                   LD_PE_DLL_TERMINAL_SERVER_AWARE;
    if (ctx->options->pie && has_base_relocations)
        dll_characteristics |=
                LD_PE_DLL_DYNAMIC_BASE | LD_PE_DLL_HIGH_ENTROPY_VA;
    ld_coff_write_u16(image, (size_t) image_size, optional + 70U,
                      dll_characteristics);
    ld_coff_write_u64(image, (size_t) image_size, optional + 72U,
                      LD_PE_STACK_RESERVE);
    ld_coff_write_u64(image, (size_t) image_size, optional + 80U,
                      LD_PE_STACK_COMMIT);
    ld_coff_write_u64(image, (size_t) image_size, optional + 88U,
                      LD_PE_HEAP_RESERVE);
    ld_coff_write_u64(image, (size_t) image_size, optional + 96U,
                      LD_PE_HEAP_COMMIT);
    ld_coff_write_u32(image, (size_t) image_size, optional + 108U,
                      LD_PE_DIRECTORY_COUNT);

    ld_coff_output_section_t *idata = ld_coff_output_named(ctx, ".idata");
    uint32_t import_start = UINT32_MAX, import_end = 0U;
    uint32_t iat_start = UINT32_MAX, iat_end = 0U;
    if (idata && idata->virtual_size) {
        /* Equivalent to lld/COFF/Writer.cpp locateImportTables(): data
           directories describe only the live .idata$2 and .idata$5 chunk
           ranges, including both GNU objects and synthetic short imports. */
        for (size_t i = 0; i < idata->input_count; i++) {
            ld_coff_section_t *section = idata->inputs[i];
            uint32_t start = section->output_offset;
            uint32_t end = start + section->virtual_size;
            if (strncmp(section->name, ".idata$2", 8U) == 0) {
                if (start < import_start) import_start = start;
                if (end > import_end) import_end = end;
            } else if (strncmp(section->name, ".idata$5", 8U) == 0) {
                if (start < iat_start) iat_start = start;
                if (end > iat_end) iat_end = end;
            }
        }
    }
    if (import_start != UINT32_MAX && import_end > import_start)
        ld_coff_write_directory(image, (size_t) image_size,
                                LD_PE_DIRECTORY_IMPORT,
                                idata->rva + import_start,
                                import_end - import_start);
    ld_coff_output_section_t *pdata = ld_coff_output_named(ctx, ".pdata");
    if (pdata && pdata->virtual_size)
        ld_coff_write_directory(image, (size_t) image_size,
                                LD_PE_DIRECTORY_EXCEPTION, pdata->rva,
                                pdata->virtual_size);
    ld_coff_output_section_t *reloc = ld_coff_output_named(ctx, ".reloc");
    if (reloc && reloc->virtual_size)
        ld_coff_write_directory(image, (size_t) image_size,
                                LD_PE_DIRECTORY_BASERELOC, reloc->rva,
                                reloc->virtual_size);
    uint32_t tls_rva;
    if (ld_coff_global_rva(ctx, "_tls_used", &tls_rva) ||
        ld_coff_global_rva(ctx, "__tls_used", &tls_rva))
        ld_coff_write_directory(image, (size_t) image_size,
                                LD_PE_DIRECTORY_TLS, tls_rva, 40U);
    if (iat_start != UINT32_MAX && iat_end > iat_start)
        ld_coff_write_directory(image, (size_t) image_size,
                                LD_PE_DIRECTORY_IAT,
                                idata->rva + iat_start,
                                iat_end - iat_start);

    uint64_t section_headers = optional + LD_PE_OPTIONAL_HEADER64_SIZE;
    for (size_t i = 0; i < ctx->output_count; i++) {
        ld_coff_output_section_t *output = ctx->outputs[i];
        uint64_t header = section_headers + i * LD_PE_SECTION_HEADER_SIZE;
        size_t name_length = strlen(output->name);
        if (section_name_offsets[i] != 0U) {
            char encoded[9] = {0};
            snprintf(encoded, sizeof(encoded), "/%u", section_name_offsets[i]);
            memcpy(image + header, encoded, 8U);
            memcpy(image + string_table_offset + section_name_offsets[i],
                   output->name, name_length + 1U);
        } else {
            memcpy(image + header, output->name,
                   name_length < 8U ? name_length : 8U);
        }
        ld_coff_write_u32(image, (size_t) image_size, header + 8U,
                          output->virtual_size);
        ld_coff_write_u32(image, (size_t) image_size, header + 12U,
                          output->rva);
        ld_coff_write_u32(image, (size_t) image_size, header + 16U,
                          output->raw_size);
        ld_coff_write_u32(image, (size_t) image_size, header + 20U,
                          output->file_offset);
        ld_coff_write_u32(image, (size_t) image_size, header + 36U,
                          output->characteristics);
        if (output->raw_size)
            memcpy(image + output->file_offset, output->data,
                   output->raw_size);
    }
    free(section_name_offsets);
    *result = image;
    *result_size = (size_t) image_size;
    return LD_OK;
}

int ld_coff_build_image(ld_coff_context_t *ctx, uint8_t **image,
                        size_t *image_size) {
    if (!ctx || !image || !image_size) return LD_INVALID_ARGUMENT;
    *image = NULL;
    *image_size = 0U;
    int status = ld_coff_prepare_common(ctx);
    if (status == LD_OK) status = ld_coff_prepare_mingw_synthetics(ctx);
    if (status == LD_OK) status = ld_coff_prepare_imports(ctx);
    if (status == LD_OK) status = ld_coff_collect_sections(ctx);
    if (status == LD_OK) status = ld_coff_layout(ctx);
    if (status == LD_OK) ld_coff_bind_mingw_boundaries(ctx);
    if (status == LD_OK) status = ld_coff_fill_imports(ctx);
    if (status == LD_OK) status = ld_coff_apply_relocations(ctx);
    if (status == LD_OK) status = ld_coff_sort_pdata(ctx);
    if (status == LD_OK) status = ld_coff_build_base_relocations(ctx);
    if (status == LD_OK) status = ld_coff_resolve_entry(ctx);
    if (status == LD_OK) status = ld_coff_write_pe(ctx, image, image_size);
    return status;
}

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} ld_coff_text_buffer_t;

typedef struct {
    const ld_coff_global_t *global;
    const ld_coff_output_section_t *output;
    uint64_t va;
    uint32_t rva;
} ld_coff_map_symbol_t;

static int ld_coff_text_append(ld_coff_context_t *ctx,
                               ld_coff_text_buffer_t *buffer,
                               const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list copied;
    va_copy(copied, args);
    int required = vsnprintf(NULL, 0U, format, copied);
    va_end(copied);
    if (required < 0) {
        va_end(args);
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "cannot format COFF map output");
    }
    size_t additional = (size_t) required;
    if (additional > SIZE_MAX - buffer->size - 1U) {
        va_end(args);
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "COFF map output is too large");
    }
    size_t needed = buffer->size + additional + 1U;
    if (needed > buffer->capacity) {
        size_t next = buffer->capacity ? buffer->capacity : 4096U;
        while (next < needed) {
            if (next > SIZE_MAX / 2U) {
                next = needed;
                break;
            }
            next *= 2U;
        }
        char *data = realloc(buffer->data, next);
        if (!data) {
            va_end(args);
            return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        }
        buffer->data = data;
        buffer->capacity = next;
    }
    int written = vsnprintf(buffer->data + buffer->size,
                            buffer->capacity - buffer->size, format, args);
    va_end(args);
    if (written != required)
        return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                            "cannot format COFF map output");
    buffer->size += additional;
    return LD_OK;
}

static int ld_coff_map_symbol_compare(const void *left_value,
                                      const void *right_value) {
    const ld_coff_map_symbol_t *left = left_value;
    const ld_coff_map_symbol_t *right = right_value;
    if (left->rva != right->rva)
        return (left->rva > right->rva) - (left->rva < right->rva);
    int compare = strcmp(left->global->name, right->global->name);
    if (compare) return compare;
    return (left->global->insertion_order > right->global->insertion_order) -
           (left->global->insertion_order < right->global->insertion_order);
}

static const char *ld_coff_map_source(const ld_coff_global_t *global) {
    const ld_coff_object_t *object = global->object;
    if (!object && global->symbol && global->symbol->section)
        object = global->symbol->section->object;
    if (!object || !object->display_name || !*object->display_name)
        return "<synthetic>";
    const char *source = object->display_name;
    const char *slash = strrchr(source, '/');
    const char *backslash = strrchr(source, '\\');
    if (slash && (!backslash || slash > backslash)) return slash + 1U;
    if (backslash) return backslash + 1U;
    return source;
}

int ld_coff_write_map(ld_coff_context_t *ctx) {
    if (!ctx || !ctx->options) return LD_INVALID_ARGUMENT;
    if (!ctx->options->map_path || !*ctx->options->map_path) return LD_OK;

    ld_coff_map_symbol_t *symbols = NULL;
    if (ctx->global_count != 0U) {
        symbols = calloc(ctx->global_count, sizeof(*symbols));
        if (!symbols) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    }
    size_t symbol_count = 0U;
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (!global->name || global->kind == LD_COFF_GLOBAL_UNDEFINED ||
            global->kind == LD_COFF_GLOBAL_ABSOLUTE || !global->section ||
            !global->section->output)
            continue;
        uint64_t rva;
        uint64_t section_rva = global->section->output->rva;
        if (!ld_coff_add_ok(section_rva, global->section->output_offset,
                            &section_rva) ||
            !ld_coff_add_ok(section_rva, global->value, &rva) ||
            rva > UINT32_MAX || rva > UINT64_MAX - ctx->image_base) {
            free(symbols);
            return ld_coff_fail(ctx, LD_OUTPUT_ERROR,
                                "symbol '%s' is outside PE address space",
                                global->name);
        }
        ld_coff_map_symbol_t *entry = &symbols[symbol_count++];
        entry->global = global;
        entry->output = global->section->output;
        entry->rva = (uint32_t) rva;
        entry->va = ctx->image_base + rva;
    }
    qsort(symbols, symbol_count, sizeof(*symbols),
          ld_coff_map_symbol_compare);

    ld_coff_text_buffer_t buffer = {0};
    int status = ld_coff_text_append(
            ctx, &buffer,
            "# Nature COFF/PE map v1\n"
            "image_base 0x%016" PRIx64 "\n"
            "entry      0x%016" PRIx64 " 0x%08" PRIx32 " %s\n"
            "\nsections\n"
            "# VA                 RVA        Size       Name\n",
            ctx->image_base, ctx->image_base + ctx->entry_rva,
            ctx->entry_rva,
            ctx->options->entry_symbol && *ctx->options->entry_symbol
                    ? ctx->options->entry_symbol
                    : "mainCRTStartup");
    for (size_t i = 0; status == LD_OK && i < ctx->output_count; i++) {
        const ld_coff_output_section_t *output = ctx->outputs[i];
        status = ld_coff_text_append(
                ctx, &buffer, "0x%016" PRIx64 " 0x%08" PRIx32 " 0x%08" PRIx32 " %s\n",
                ctx->image_base + output->rva, output->rva,
                output->virtual_size, output->name);
    }
    if (status == LD_OK)
        status = ld_coff_text_append(
                ctx, &buffer,
                "\nsymbols\n"
                "# VA                 RVA        Section  Symbol Source\n");
    for (size_t i = 0; status == LD_OK && i < symbol_count; i++) {
        const ld_coff_map_symbol_t *symbol = &symbols[i];
        status = ld_coff_text_append(
                ctx, &buffer, "0x%016" PRIx64 " 0x%08" PRIx32 " %-8s %s %s\n",
                symbol->va, symbol->rva, symbol->output->name,
                symbol->global->name,
                ld_coff_map_source(symbol->global));
    }
    if (status == LD_OK)
        status = ld_write_file_atomic(
                ctx->options, ctx->options->map_path,
                (const uint8_t *) buffer.data, buffer.size, false);
    free(buffer.data);
    free(symbols);
    return status;
}
