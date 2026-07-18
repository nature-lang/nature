#include "ld_coff.h"

#include "ld_coff_internal.h"
#include "ld_output.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Pipeline and symbol resolution are semantic C ports of the ordinary-link
 * portions of lld/COFF/COFFLinkerContext.cpp, SymbolTable.cpp, Symbols.cpp,
 * and MarkLive.cpp.
 * Upstream commit: c58ba1cf51d2886994da7e667a05c1bfe4f4396b
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

void *ld_coff_grow(void *items, size_t old_count, size_t new_count,
                   size_t item_size) {
    if (item_size == 0U || new_count > SIZE_MAX / item_size) return NULL;
    void *result = realloc(items, new_count * item_size);
    if (result && new_count > old_count) {
        memset((uint8_t *) result + old_count * item_size, 0,
               (new_count - old_count) * item_size);
    }
    return result;
}

char *ld_coff_strndup(const char *text, size_t length) {
    if (!text || length == SIZE_MAX) return NULL;
    char *result = malloc(length + 1U);
    if (!result) return NULL;
    memcpy(result, text, length);
    result[length] = '\0';
    return result;
}

int ld_coff_fail(ld_coff_context_t *ctx, int code, const char *format, ...) {
    if (!ctx) return code;
    ctx->error = code;
    if (ctx->options && ctx->options->diagnostic) {
        char message[4096];
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);
        ctx->options->diagnostic(ctx->options->diagnostic_context,
                                 LD_DIAG_ERROR, message);
    }
    return code;
}

const char *ld_coff_relocation_name(uint16_t type) {
    switch (type) {
        case LD_COFF_REL_AMD64_ABSOLUTE:
            return "ABSOLUTE";
        case LD_COFF_REL_AMD64_ADDR64:
            return "ADDR64";
        case LD_COFF_REL_AMD64_ADDR32:
            return "ADDR32";
        case LD_COFF_REL_AMD64_ADDR32NB:
            return "ADDR32NB";
        case LD_COFF_REL_AMD64_REL32:
            return "REL32";
        case LD_COFF_REL_AMD64_REL32_1:
            return "REL32_1";
        case LD_COFF_REL_AMD64_REL32_2:
            return "REL32_2";
        case LD_COFF_REL_AMD64_REL32_3:
            return "REL32_3";
        case LD_COFF_REL_AMD64_REL32_4:
            return "REL32_4";
        case LD_COFF_REL_AMD64_REL32_5:
            return "REL32_5";
        case LD_COFF_REL_AMD64_SECTION:
            return "SECTION";
        case LD_COFF_REL_AMD64_SECREL:
            return "SECREL";
        default:
            return "UNKNOWN";
    }
}

int ld_coff_relocation_fail(ld_coff_context_t *ctx, int code,
                            const ld_coff_object_t *object,
                            const ld_coff_section_t *section,
                            const ld_coff_relocation_t *relocation,
                            const char *symbol_name, const char *format, ...) {
    char detail[2048];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(detail, sizeof(detail), format, arguments);
    va_end(arguments);

    const char *object_name =
            object && object->file && object->file->path
                    ? object->file->path
                    : (object && object->display_name ? object->display_name
                                                      : "<synthetic>");
    const char *member_name =
            object && object->member_name ? object->member_name : "<direct>";
    const char *section_name =
            section && section->name ? section->name : "<unknown>";
    uint32_t offset = relocation ? relocation->offset : 0U;
    uint16_t type = relocation ? relocation->type : UINT16_MAX;
    return ld_coff_fail(
            ctx, code,
            "relocation error: object='%s', member='%s', section='%s', "
            "offset=0x%08x, symbol='%s', type=%s(0x%04x): %s",
            object_name, member_name, section_name, offset,
            symbol_name ? symbol_name : "<unknown>",
            ld_coff_relocation_name(type), type, detail);
}

void ld_coff_context_init(ld_coff_context_t *ctx,
                          const ld_options_t *options) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->options = options;
    ctx->image_base = LD_PE_IMAGE_BASE64;
    ctx->section_alignment = LD_PE_SECTION_ALIGNMENT;
    ctx->file_alignment = LD_PE_FILE_ALIGNMENT;
}

static void ld_coff_object_destroy(ld_coff_object_t *object) {
    if (!object) return;
    for (size_t i = 0; i < object->section_count; i++) {
        free(object->sections[i].name);
        free(object->sections[i].owned_data);
        free(object->sections[i].relocations);
    }
    for (size_t i = 0; i < object->symbol_count; i++)
        free(object->symbols[i].name);
    if (object->import) {
        free(object->import->public_name);
        free(object->import->import_name);
        free(object->import->dll_name);
        free(object->import);
    }
    free(object->sections);
    free(object->symbols);
    free(object->display_name);
    free(object->member_name);
    free(object);
}

void ld_coff_context_deinit(ld_coff_context_t *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->object_count; i++)
        ld_coff_object_destroy(ctx->objects[i]);
    for (size_t i = 0; i < ctx->synthetic_count; i++) {
        ld_coff_section_t *section = ctx->synthetic_sections[i];
        free(section->name);
        free(section->owned_data);
        free(section->relocations);
        free(section);
    }
    for (size_t i = 0; i < ctx->file_count; i++) {
        free(ctx->files[i]->bytes);
        free(ctx->files[i]->path);
        free(ctx->files[i]);
    }
    for (size_t i = 0; i < ctx->output_count; i++) {
        free(ctx->outputs[i]->name);
        free(ctx->outputs[i]->inputs);
        free(ctx->outputs[i]->data);
        free(ctx->outputs[i]);
    }
    ld_coff_global_t *global, *temporary;
    HASH_ITER(hh, ctx->globals, global, temporary) {
        HASH_DEL(ctx->globals, global);
        free(global->name);
        free(global->fallback_name);
        free(global->lazy_objects);
        free(global);
    }
    for (size_t i = 0; i < ctx->alias_count; i++) {
        free(ctx->aliases[i].source);
        free(ctx->aliases[i].target);
    }
    for (size_t i = 0; i < ctx->mismatch_count; i++) {
        free(ctx->mismatches[i].key);
        free(ctx->mismatches[i].value);
        free(ctx->mismatches[i].source);
    }
    for (size_t i = 0; i < ctx->default_library_count; i++)
        free(ctx->default_libraries[i]);
    for (size_t i = 0; i < ctx->nodefault_library_count; i++)
        free(ctx->nodefault_libraries[i]);
    for (size_t i = 0; i < ctx->loaded_path_count; i++)
        free(ctx->loaded_paths[i]);
    free(ctx->files);
    free(ctx->objects);
    free(ctx->synthetic_sections);
    free(ctx->global_order);
    free(ctx->imports);
    free(ctx->outputs);
    free(ctx->aliases);
    free(ctx->mismatches);
    free(ctx->default_libraries);
    free(ctx->nodefault_libraries);
    free(ctx->loaded_paths);
    free(ctx->base_relocations);
    memset(ctx, 0, sizeof(*ctx));
}

static int ld_coff_push_global_order(ld_coff_context_t *ctx,
                                     ld_coff_global_t *global) {
    if (ctx->global_count == ctx->global_capacity) {
        size_t next = ctx->global_capacity ? ctx->global_capacity * 2U : 128U;
        ld_coff_global_t **items = ld_coff_grow(
                ctx->global_order, ctx->global_capacity, next, sizeof(*items));
        if (!items) return LD_IO_ERROR;
        ctx->global_order = items;
        ctx->global_capacity = next;
    }
    global->insertion_order = (uint32_t) ctx->global_count;
    ctx->global_order[ctx->global_count++] = global;
    return LD_OK;
}

ld_coff_global_t *ld_coff_get_global(ld_coff_context_t *ctx,
                                     const char *name, bool create) {
    if (!ctx || !name || !*name) return NULL;
    ld_coff_global_t *global = NULL;
    HASH_FIND_STR(ctx->globals, name, global);
    if (global || !create) return global;
    global = calloc(1, sizeof(*global));
    if (!global) return NULL;
    global->name = ld_coff_strndup(name, strlen(name));
    if (!global->name || ld_coff_push_global_order(ctx, global) != LD_OK) {
        free(global->name);
        free(global);
        return NULL;
    }
    global->kind = LD_COFF_GLOBAL_UNDEFINED;
    HASH_ADD_KEYPTR(hh, ctx->globals, global->name, strlen(global->name),
                    global);
    return global;
}

int ld_coff_register_lazy(ld_coff_context_t *ctx, const char *name,
                          ld_coff_object_t *object) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, name, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    for (size_t i = 0; i < global->lazy_count; i++)
        if (global->lazy_objects[i] == object) return LD_OK;
    if (global->lazy_count == global->lazy_capacity) {
        size_t next = global->lazy_capacity ? global->lazy_capacity * 2U : 2U;
        ld_coff_object_t **items = ld_coff_grow(
                global->lazy_objects, global->lazy_capacity, next,
                sizeof(*items));
        if (!items)
            return ld_coff_fail(ctx, LD_IO_ERROR,
                                "out of memory indexing archive symbols");
        global->lazy_objects = items;
        global->lazy_capacity = next;
    }
    global->lazy_objects[global->lazy_count++] = object;
    return LD_OK;
}

static bool ld_coff_section_is_comdat(const ld_coff_section_t *section) {
    return section &&
           (section->characteristics & LD_COFF_SCN_LNK_COMDAT) != 0U;
}

static int ld_coff_choose_comdat(ld_coff_context_t *ctx,
                                 ld_coff_global_t *existing,
                                 ld_coff_section_t *candidate,
                                 bool *candidate_wins) {
    ld_coff_section_t *current = existing->section;
    *candidate_wins = false;
    if (!ld_coff_section_is_comdat(current) ||
        !ld_coff_section_is_comdat(candidate)) {
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "duplicate symbol '%s' in %s and %s",
                            existing->name,
                            current && current->object
                                    ? current->object->display_name
                                    : "<synthetic>",
                            candidate->object
                                    ? candidate->object->display_name
                                    : "<synthetic>");
    }
    if (!current->comdat_key || !candidate->comdat_key ||
        strcmp(current->comdat_key, candidate->comdat_key) != 0) {
        return ld_coff_fail(
                ctx, LD_SYMBOL_ERROR,
                "duplicate symbol '%s' in COMDATs with different leaders",
                existing->name);
    }
    uint8_t selection = current->comdat_selection
                                ? current->comdat_selection
                                : candidate->comdat_selection;
    if (candidate->comdat_selection && current->comdat_selection &&
        candidate->comdat_selection != current->comdat_selection) {
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "conflicting COMDAT selections for '%s'",
                            existing->name);
    }
    switch (selection) {
        case LD_COFF_COMDAT_NODUPLICATES:
            return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                "duplicate NODUPLICATES COMDAT '%s'",
                                existing->name);
        case LD_COFF_COMDAT_ANY:
            return LD_OK;
        case LD_COFF_COMDAT_SAME_SIZE:
            if (current->virtual_size != candidate->virtual_size)
                return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                    "SAME_SIZE COMDAT '%s' has different sizes",
                                    existing->name);
            return LD_OK;
        case LD_COFF_COMDAT_EXACT_MATCH:
            /* Match link.exe/lld-link: EXACT_MATCH compares section contents,
               not the auxiliary section-definition checksum or alignment. */
            if (current->data_size != candidate->data_size ||
                (current->data_size &&
                 memcmp(current->data, candidate->data,
                        current->data_size) != 0))
                return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                    "EXACT_MATCH COMDAT '%s' differs",
                                    existing->name);
            return LD_OK;
        case LD_COFF_COMDAT_LARGEST:
            *candidate_wins = candidate->virtual_size > current->virtual_size;
            return LD_OK;
        case LD_COFF_COMDAT_ASSOCIATIVE:
            return LD_OK;
        case LD_COFF_COMDAT_NEWEST:
            return ld_coff_fail(ctx, LD_UNSUPPORTED,
                                "unsupported NEWEST COMDAT '%s'",
                                existing->name);
        default:
            return ld_coff_fail(ctx, LD_UNSUPPORTED,
                                "unsupported COMDAT selection %u for '%s'",
                                selection, existing->name);
    }
}

static void ld_coff_clear_section_definitions(ld_coff_context_t *ctx,
                                              ld_coff_section_t *section) {
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (global->section != section) continue;
        global->kind = LD_COFF_GLOBAL_UNDEFINED;
        global->object = NULL;
        global->symbol = NULL;
        global->section = NULL;
        global->import = NULL;
        global->value = 0U;
    }
}

static int ld_coff_select_object_comdats(ld_coff_context_t *ctx,
                                         ld_coff_object_t *object) {
    for (size_t i = 0; i < object->section_count; i++) {
        ld_coff_section_t *section = &object->sections[i];
        if (!ld_coff_section_is_comdat(section)) continue;

        uint8_t selection = section->comdat_selection;
        if (selection == LD_COFF_COMDAT_NEWEST)
            return ld_coff_fail(
                    ctx, LD_UNSUPPORTED,
                    "%s(%s): NEWEST COMDAT selection is unsupported",
                    object->display_name, section->name);
        if (selection < LD_COFF_COMDAT_NODUPLICATES ||
            selection > LD_COFF_COMDAT_LARGEST)
            return ld_coff_fail(
                    ctx, LD_UNSUPPORTED,
                    "%s(%s): unsupported or missing COMDAT selection %u",
                    object->display_name, section->name, selection);
        if (selection == LD_COFF_COMDAT_ASSOCIATIVE) continue;
        /* LLD treats a COMDAT whose leader is not external as locally
           prevailing.  Such unwind/debug contributions have no global key
           and therefore do not participate in cross-object selection. */
        if (!section->comdat_key || !*section->comdat_key) continue;

        ld_coff_global_t *leader =
                ld_coff_get_global(ctx, section->comdat_key, false);
        if (!leader || leader->kind == LD_COFF_GLOBAL_UNDEFINED) continue;

        bool candidate_wins = false;
        int status =
                ld_coff_choose_comdat(ctx, leader, section, &candidate_wins);
        if (status != LD_OK) return status;
        if (!candidate_wins) {
            section->discarded = true;
            continue;
        }

        ld_coff_section_t *previous = leader->section;
        if (previous) {
            previous->discarded = true;
            ld_coff_clear_section_definitions(ctx, previous);
        }
    }
    return LD_OK;
}

static int ld_coff_define_regular(ld_coff_context_t *ctx,
                                  ld_coff_symbol_t *symbol) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, symbol->name, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    symbol->global = global;
    if (symbol->section && symbol->section->discarded) return LD_OK;
    if (global->kind == LD_COFF_GLOBAL_DEFINED) {
        bool candidate_wins = false;
        int status = ld_coff_choose_comdat(ctx, global, symbol->section,
                                           &candidate_wins);
        if (status != LD_OK) return status;
        if (!candidate_wins) {
            if (symbol->section) {
                symbol->section->discarded = true;
                ld_coff_clear_section_definitions(ctx, symbol->section);
            }
            return LD_OK;
        }
        if (global->section) {
            ld_coff_section_t *previous = global->section;
            previous->discarded = true;
            ld_coff_clear_section_definitions(ctx, previous);
        }
    }
    global->kind = LD_COFF_GLOBAL_DEFINED;
    global->object = symbol->section ? symbol->section->object : NULL;
    global->symbol = symbol;
    global->section = symbol->section;
    global->import = NULL;
    global->value = symbol->value;
    return LD_OK;
}

static int ld_coff_define_common(ld_coff_context_t *ctx,
                                 ld_coff_object_t *object,
                                 ld_coff_symbol_t *symbol) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, symbol->name, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    symbol->global = global;
    if (global->kind == LD_COFF_GLOBAL_DEFINED) return LD_OK;
    /* LLD's addCommon replaces every non-DefinedCOFF symbol (including an
       import or absolute symbol), but among common definitions it retains
       the first unless a later input requests strictly more storage. */
    if (global->kind == LD_COFF_GLOBAL_COMMON &&
        symbol->value <= global->common_size) {
        return LD_OK;
    }
    global->kind = LD_COFF_GLOBAL_COMMON;
    global->common_size = symbol->value;
    uint64_t alignment = 1U;
    while (alignment < global->common_size && alignment < 32U)
        alignment <<= 1U;
    global->common_alignment = (uint32_t) alignment;
    global->object = object;
    global->symbol = symbol;
    global->section = NULL;
    global->import = NULL;
    return LD_OK;
}

static int ld_coff_define_absolute(ld_coff_context_t *ctx,
                                   ld_coff_symbol_t *symbol) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, symbol->name, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    symbol->global = global;
    if (global->kind != LD_COFF_GLOBAL_UNDEFINED &&
        (global->kind != LD_COFF_GLOBAL_ABSOLUTE ||
         global->value != symbol->value))
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "duplicate absolute symbol '%s'", symbol->name);
    global->kind = LD_COFF_GLOBAL_ABSOLUTE;
    global->value = symbol->value;
    global->symbol = symbol;
    return LD_OK;
}

static int ld_coff_push_import(ld_coff_context_t *ctx,
                               ld_coff_import_t *import,
                               ld_coff_import_t **canonical) {
    for (size_t i = 0; i < ctx->import_count; i++) {
        ld_coff_import_t *old = ctx->imports[i];
        if (strcmp(old->public_name, import->public_name) == 0) {
            if (old->type != import->type)
                return ld_coff_fail(
                        ctx, LD_SYMBOL_ERROR,
                        "conflicting import symbol '%s': type %u from %s and type %u from %s",
                        import->public_name, old->type, old->dll_name,
                        import->type, import->dll_name);
            // UCRT umbrella and API-set import libraries intentionally expose
            // many of the same public symbols. Match normal COFF link-order
            // semantics by retaining the first selected import definition.
            *canonical = old;
            return LD_OK;
        }
    }
    if (ctx->import_count == ctx->import_capacity) {
        size_t next = ctx->import_capacity ? ctx->import_capacity * 2U : 32U;
        ld_coff_import_t **items = ld_coff_grow(
                ctx->imports, ctx->import_capacity, next, sizeof(*items));
        if (!items) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        ctx->imports = items;
        ctx->import_capacity = next;
    }
    ctx->imports[ctx->import_count++] = import;
    import->selected = true;
    *canonical = import;
    return LD_OK;
}

static int ld_coff_define_import_symbol(ld_coff_context_t *ctx,
                                        const char *name,
                                        ld_coff_import_t *import,
                                        ld_coff_global_kind_t kind) {
    ld_coff_global_t *global = ld_coff_get_global(ctx, name, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    if (global->kind != LD_COFF_GLOBAL_UNDEFINED && global->import != import)
        return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                            "duplicate import symbol '%s'", name);
    global->kind = kind;
    global->import = import;
    global->object = import->object;
    return LD_OK;
}

static int ld_coff_select_import(ld_coff_context_t *ctx,
                                 ld_coff_object_t *object) {
    ld_coff_import_t *import = object->import;
    ld_coff_import_t *canonical = NULL;
    int status = ld_coff_push_import(ctx, import, &canonical);
    if (status != LD_OK) return status;
    import = canonical;
    size_t length = strlen(import->public_name);
    char *iat = malloc(length + 7U);
    if (!iat) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    memcpy(iat, "__imp_", 6U);
    memcpy(iat + 6U, import->public_name, length + 1U);
    status = ld_coff_define_import_symbol(ctx, iat, import,
                                          LD_COFF_GLOBAL_IMPORT_IAT);
    free(iat);
    if (status != LD_OK) return status;
    return ld_coff_define_import_symbol(
            ctx, import->public_name, import,
            import->type == LD_COFF_IMPORT_CODE
                    ? LD_COFF_GLOBAL_IMPORT_THUNK
                    : LD_COFF_GLOBAL_IMPORT_IAT);
}

int ld_coff_select_object(ld_coff_context_t *ctx, ld_coff_object_t *object) {
    if (!ctx || !object) return LD_INVALID_ARGUMENT;
    if (object->selected) return LD_OK;
    object->selected = true;
    if (object->import_object) return ld_coff_select_import(ctx, object);

    for (size_t i = 0; i < object->section_count; i++) {
        ld_coff_section_t *section = &object->sections[i];
        if (strcmp(section->name, ".drectve") == 0) {
            int status = ld_coff_parse_directives(ctx, section);
            if (status != LD_OK) return status;
            section->discarded = true;
        }
    }

    int status = ld_coff_select_object_comdats(ctx, object);
    if (status != LD_OK) return status;

    for (size_t i = 0; i < object->symbol_count; i++) {
        ld_coff_symbol_t *symbol = &object->symbols[i];
        if (symbol->auxiliary || !symbol->name || !*symbol->name) continue;
        bool external = symbol->storage_class == LD_COFF_STORAGE_CLASS_EXTERNAL ||
                        symbol->storage_class ==
                                LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL;
        if (!external) continue;
        status = LD_OK;
        if (symbol->section_number > 0) {
            status = ld_coff_define_regular(ctx, symbol);
        } else if (symbol->section_number == LD_COFF_SYM_ABSOLUTE) {
            status = ld_coff_define_absolute(ctx, symbol);
        } else if (symbol->section_number == LD_COFF_SYM_UNDEFINED &&
                   symbol->value != 0U) {
            status = ld_coff_define_common(ctx, object, symbol);
        } else if (symbol->section_number == LD_COFF_SYM_UNDEFINED) {
            ld_coff_global_t *global =
                    ld_coff_get_global(ctx, symbol->name, true);
            if (!global)
                return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            symbol->global = global;
            global->referenced = true;
            if (symbol->storage_class == LD_COFF_STORAGE_CLASS_WEAK_EXTERNAL &&
                symbol->weak_target_index < object->symbol_count) {
                ld_coff_symbol_t *target =
                        &object->symbols[symbol->weak_target_index];
                if (target->name && *target->name && !global->fallback_name) {
                    global->fallback_name = ld_coff_strndup(
                            target->name, strlen(target->name));
                    if (!global->fallback_name)
                        return ld_coff_fail(ctx, LD_IO_ERROR,
                                            "out of memory");
                    global->fallback_characteristics =
                            symbol->weak_characteristics;
                    global->fallback_object = object;
                }
            }
        }
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static bool ld_coff_global_resolved(const ld_coff_global_t *global) {
    return global && global->kind != LD_COFF_GLOBAL_UNDEFINED;
}

static int ld_coff_apply_aliases(ld_coff_context_t *ctx, bool *changed) {
    for (size_t i = 0; i < ctx->alias_count; i++) {
        ld_coff_alias_t *alias = &ctx->aliases[i];
        ld_coff_global_t *source =
                ld_coff_get_global(ctx, alias->source, true);
        ld_coff_global_t *target =
                ld_coff_get_global(ctx, alias->target, true);
        if (!source || !target)
            return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        if (source->referenced && !ld_coff_global_resolved(source) &&
            !target->referenced) {
            target->referenced = true;
            *changed = true;
        }
        if (!source->fallback_name) {
            source->fallback_name =
                    ld_coff_strndup(alias->target, strlen(alias->target));
            if (!source->fallback_name)
                return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
            source->fallback_characteristics = LD_COFF_WEAK_SEARCH_ALIAS;
        }
    }
    return LD_OK;
}

int ld_coff_resolve_archives(ld_coff_context_t *ctx) {
    if (!ctx) return LD_INVALID_ARGUMENT;
    bool loaded_defaults;
    do {
        /* Finish the current archive fixed point before admitting queued
           /DEFAULTLIB inputs. A selected member may introduce another
           undefined symbol whose provider carries /NODEFAULTLIB; loading
           defaults between those two selections makes the result depend on
           member discovery order. This corresponds to lld-link's ordinary
           input queue reaching all currently available providers first. */
        bool changed;
        do {
            changed = false;
            int status = ld_coff_apply_aliases(ctx, &changed);
            if (status != LD_OK) return status;
            size_t count = ctx->global_count;
            for (size_t i = 0; i < count; i++) {
                ld_coff_global_t *global = ctx->global_order[i];
                if (!global->referenced || ld_coff_global_resolved(global))
                    continue;
                ld_coff_object_t *provider = NULL;
                for (size_t j = 0; j < global->lazy_count; j++) {
                    ld_coff_object_t *candidate = global->lazy_objects[j];
                    if (candidate->selected) continue;
                    /* COFF weak externals are added with overrideLazy=false.
                       An earlier lazy symbol is therefore forced, while a
                       lazy archive registered after the weak alias is
                       ignored.  Nature parses archives up front, so recover
                       LLD's incremental input semantics using stable object
                       input order at extraction time. */
                    if (global->fallback_object &&
                        candidate->input_order >
                                global->fallback_object->input_order) {
                        continue;
                    }
                    provider = candidate;
                    break;
                }
                if (provider) {
                    status = ld_coff_select_object(ctx, provider);
                    if (status != LD_OK) return status;
                    changed = true;
                    continue;
                }
                if (global->fallback_name &&
                    global->fallback_characteristics !=
                            LD_COFF_WEAK_SEARCH_NOLIBRARY) {
                    ld_coff_global_t *target = ld_coff_get_global(
                            ctx, global->fallback_name, true);
                    if (!target)
                        return ld_coff_fail(ctx, LD_IO_ERROR,
                                            "out of memory");
                    if (!target->referenced) {
                        target->referenced = true;
                        changed = true;
                    }
                }
            }
        } while (changed);

        size_t objects_before = ctx->object_count;
        int status = ld_coff_load_default_libraries(ctx);
        if (status != LD_OK) return status;
        loaded_defaults = ctx->object_count != objects_before;
    } while (loaded_defaults);
    return LD_OK;
}

static int ld_coff_finalize_weak_aliases(ld_coff_context_t *ctx) {
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (ld_coff_global_resolved(global) || !global->fallback_name) continue;
        ld_coff_global_t *target =
                ld_coff_get_global(ctx, global->fallback_name, false);
        size_t depth = 0U;
        while (target && !ld_coff_global_resolved(target) &&
               target->fallback_name && depth++ < ctx->global_count) {
            target = ld_coff_get_global(ctx, target->fallback_name, false);
        }
        if (depth >= ctx->global_count)
            return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                "weak alias cycle involving '%s'",
                                global->name);
        if (target && ld_coff_global_resolved(target)) {
            global->kind = target->kind;
            global->object = target->object;
            global->symbol = target->symbol;
            global->section = target->section;
            global->import = target->import;
            global->value = target->value;
        }
    }
    return LD_OK;
}

static int ld_coff_check_undefined(ld_coff_context_t *ctx) {
    for (size_t i = 0; i < ctx->global_count; i++) {
        ld_coff_global_t *global = ctx->global_order[i];
        if (global->referenced && !ld_coff_global_resolved(global))
            return ld_coff_fail(ctx, LD_SYMBOL_ERROR,
                                "undefined symbol: %s", global->name);
    }
    return LD_OK;
}

static int ld_coff_mark_entry(ld_coff_context_t *ctx) {
    const char *entry = ctx->options->entry_symbol;
    if (!entry || !*entry) entry = "mainCRTStartup";
    ld_coff_global_t *global = ld_coff_get_global(ctx, entry, true);
    if (!global) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
    global->referenced = true;
    const char *image_names[] = {"__ImageBase", "__image_base__"};
    for (size_t i = 0; i < sizeof(image_names) / sizeof(*image_names); i++) {
        ld_coff_global_t *image =
                ld_coff_get_global(ctx, image_names[i], true);
        if (!image) return ld_coff_fail(ctx, LD_IO_ERROR, "out of memory");
        if (image->kind == LD_COFF_GLOBAL_UNDEFINED) {
            /* A section-less defined value of zero denotes image RVA zero.
               ADDR64 then adds the configured image base. */
            image->kind = LD_COFF_GLOBAL_DEFINED;
            image->value = 0U;
        }
    }
    return LD_OK;
}

static int ld_coff_define_mingw_synthetics(ld_coff_context_t *ctx) {
    /* MinGW startup objects refer to linker-created boundary symbols.  They
       can first appear while resolving libmingw32, so install placeholders
       after the archive fixed point and attach them to concrete synthetic
       chunks during PE construction. */
    static const char *names[] = {
            "__CTOR_LIST__",
            "__DTOR_LIST__",
            "__RUNTIME_PSEUDO_RELOC_LIST__",
            "__RUNTIME_PSEUDO_RELOC_LIST_END__",
            "__data_start__",
            "__data_end__",
            "__bss_start__",
            "__bss_end__",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        ld_coff_global_t *global = ld_coff_get_global(ctx, names[i], false);
        if (!global || global->kind != LD_COFF_GLOBAL_UNDEFINED) continue;
        global->kind = LD_COFF_GLOBAL_DEFINED;
        global->value = 0U;
    }
    return LD_OK;
}

int ld_link_coff(const ld_options_t *options) {
    if (!options || !options->output_path || !*options->output_path ||
        options->os != LD_OS_WINDOWS)
        return LD_INVALID_ARGUMENT;
    if (options->arch != LD_ARCH_AMD64) {
        if (options->diagnostic)
            options->diagnostic(options->diagnostic_context, LD_DIAG_ERROR,
                                "the COFF linker supports windows/amd64 only");
        return LD_UNSUPPORTED;
    }
    if (options->inputs.count == 0U) {
        if (options->diagnostic)
            options->diagnostic(options->diagnostic_context, LD_DIAG_ERROR,
                                "no COFF linker input files were provided");
        return LD_INVALID_ARGUMENT;
    }

    ld_coff_context_t ctx;
    ld_coff_context_init(&ctx, options);
    int status = ld_coff_load_options(&ctx);
    if (status == LD_OK) status = ld_coff_mark_entry(&ctx);
    if (status == LD_OK) status = ld_coff_resolve_archives(&ctx);
    if (status == LD_OK) status = ld_coff_define_mingw_synthetics(&ctx);
    if (status == LD_OK) status = ld_coff_finalize_weak_aliases(&ctx);
    if (status == LD_OK) status = ld_coff_check_undefined(&ctx);
    uint8_t *image = NULL;
    size_t image_size = 0U;
    if (status == LD_OK) status = ld_coff_build_image(&ctx, &image, &image_size);
    if (status == LD_OK)
        status = ld_write_output_atomic(options, image, image_size);
    if (status == LD_OK) status = ld_coff_write_map(&ctx);
    free(image);
    ld_coff_context_deinit(&ctx);
    return status;
}
