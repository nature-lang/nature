#include "ld_elf_cie.h"
#include "ld_elf_debug.h"
#include "ld_elf_dynamic.h"
#include "ld_elf_eh_frame.h"
#include "ld_elf_ifunc.h"
#include "ld_elf_internal.h"
#include "ld_elf_merge.h"
#include "ld_elf_property.h"
#include "ld_elf_reloc.h"
#include "ld_elf_relro.h"
#include "ld_elf_riscv_uleb.h"
#include "ld_elf_symtab.h"
#include "ld_elf_thunk.h"

#include "utils/uthash.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * The phase ordering follows Elf.flushInner, Object.resolveSymbols, and
 * LinkerDefined.allocateSymbols from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224. This is a standalone C
 * implementation for Nature's batch linker; it deliberately omits Zig's
 * incremental compilation and ZigObject layers.
 */

#define LD_ELF_IMAGE_BASE 0x400000ULL
#define LD_ELF_PAGE_SIZE_AARCH64 0x10000ULL
#define LD_ELF_PAGE_SIZE_X86_64 0x1000ULL
#define LD_ELF_NO_GOT UINT32_MAX
#define LD_ELF_RISCV_RVC 0x1U
#define LD_ELF_RISCV_KNOWN_FLAGS 0x1fU

typedef enum {
    LD_ELF_SYNTHETIC_NONE = 0,
    LD_ELF_SYNTHETIC_ZERO,
    LD_ELF_SYNTHETIC_DYNAMIC,
    LD_ELF_SYNTHETIC_IMAGE_START,
    LD_ELF_SYNTHETIC_INIT_ARRAY_START,
    LD_ELF_SYNTHETIC_INIT_ARRAY_END,
    LD_ELF_SYNTHETIC_FINI_ARRAY_START,
    LD_ELF_SYNTHETIC_FINI_ARRAY_END,
    LD_ELF_SYNTHETIC_PREINIT_ARRAY_START,
    LD_ELF_SYNTHETIC_PREINIT_ARRAY_END,
    LD_ELF_SYNTHETIC_GOT,
    LD_ELF_SYNTHETIC_BSS_START,
    LD_ELF_SYNTHETIC_END,
    LD_ELF_SYNTHETIC_TEXT_END,
    LD_ELF_SYNTHETIC_DATA_END,
    LD_ELF_SYNTHETIC_EH_FRAME_START,
    LD_ELF_SYNTHETIC_EH_FRAME_HDR,
    LD_ELF_SYNTHETIC_RELA_IPLT_START,
    LD_ELF_SYNTHETIC_RELA_IPLT_END,
    LD_ELF_SYNTHETIC_DSO_HANDLE,
    LD_ELF_SYNTHETIC_GLOBAL_POINTER,
    LD_ELF_SYNTHETIC_TLS_MODULE_BASE,
    LD_ELF_SYNTHETIC_SECTION_START,
    LD_ELF_SYNTHETIC_SECTION_STOP,
} ld_elf_synthetic_t;

struct ld_elf_global {
    char *name;
    ld_elf_symbol_t *definition;
    ld_elf_object_t *object;
    uint64_t common_size;
    uint64_t common_align;
    uint64_t common_offset;
    uint32_t got_index;
    uint32_t gottp_index;
    uint32_t tlsgd_index;
    uint32_t pltgot_index;
    ld_elf_synthetic_t synthetic;
    ld_elf_output_section_t *synthetic_output;
    bool definition_weak;
    bool common;
    bool common_tls;
    bool common_reference;
    bool referenced;
    bool strong_reference;
    bool entry_reference;
    UT_hash_handle hh;
};

typedef struct {
    ld_elf_output_section_t **items;
    size_t count;
    size_t capacity;
} ld_elf_output_list_t;

typedef enum {
    LD_ELF_ARRAY_NONE = 0,
    LD_ELF_ARRAY_PREINIT,
    LD_ELF_ARRAY_INIT,
    LD_ELF_ARRAY_FINI,
} ld_elf_array_kind_t;

typedef struct ld_elf_input_placement {
    ld_elf_object_t *object;
    ld_elf_section_t *section;
    struct ld_elf_input_placement *link_order_target;
    uint64_t emitted_size;
    uint64_t sequence;
    int64_t priority;
    ld_elf_array_kind_t array_kind;
    uint32_t aarch64_thunk_group_index;
} ld_elf_input_placement_t;

typedef struct {
    ld_elf_input_placement_t *items;
    size_t count;
    size_t capacity;
} ld_elf_input_placement_list_t;

typedef struct {
    ld_elf_symtab_entry_t *items;
    size_t count;
    size_t capacity;
} ld_elf_symtab_entry_list_t;

typedef struct {
    ld_elf_global_t *global;
    ld_elf_object_t *object;
    ld_elf_symbol_t *symbol;
    const char *name;
    uint32_t got_index;
    uint32_t pltgot_index;
} ld_elf_ifunc_reference_t;

typedef struct {
    uint64_t target_address;
    int64_t resolver_addend;
} ld_elf_irelative_t;

typedef struct {
    ld_elf_context_t *ctx;
    ld_elf_global_t *globals;
    ld_elf_output_list_t outputs;
    ld_elf_input_placement_list_t placements;
    ld_elf_output_section_t *init;
    ld_elf_output_section_t *text;
    ld_elf_output_section_t *fini;
    ld_elf_output_section_t *rodata;
    ld_elf_output_section_t *gnu_property;
    ld_elf_output_section_t *eh_frame;
    ld_elf_output_section_t *eh_frame_hdr;
    ld_elf_output_section_t *tdata;
    ld_elf_output_section_t *tbss;
    ld_elf_output_section_t *preinit_array;
    ld_elf_output_section_t *init_array;
    ld_elf_output_section_t *fini_array;
    ld_elf_output_section_t *plt_got;
    ld_elf_output_section_t *rela_dyn;
    ld_elf_output_section_t *dynsym;
    ld_elf_output_section_t *dynstr;
    ld_elf_output_section_t *hash;
    ld_elf_output_section_t *gnu_hash;
    ld_elf_output_section_t *dynamic;
    ld_elf_output_section_t *got;
    ld_elf_output_section_t *data;
    ld_elf_output_section_t *bss;
    ld_elf_merge_plan_t merge_plan;
    ld_elf_property_plan_t property_plan;
    ld_elf_relro_plan_t relro_plan;
    uint64_t page_size;
    uint64_t image_base;
    uint64_t header_size;
    uint64_t rx_file_size;
    uint64_t rw_file_offset;
    uint64_t rw_addr;
    uint64_t rw_file_size;
    uint64_t rw_mem_size;
    uint64_t tls_addr;
    uint64_t tls_file_offset;
    uint64_t tls_file_size;
    uint64_t tls_mem_size;
    uint64_t tls_align;
    uint64_t tp_addr;
    uint64_t image_end;
    uint32_t elf_flags;
    uint32_t got_count;
    uint32_t gottp_count;
    uint32_t tlsgd_count;
    uint32_t pltgot_count;
    size_t ifunc_input_relocation_count;
    size_t pie_input_relocation_count;
    size_t pie_got_relocation_count;
    size_t relative_relocation_count;
    bool needs_got_base;
    ld_elf_aarch64_thunk_plan_t aarch64_thunk_plan;
    size_t aarch64_branch_relocation_count;
    size_t eh_frame_fde_count;
    uint16_t phnum;
} ld_elf_backend_t;

static bool ld_elf_add_overflow(uint64_t left, uint64_t right, uint64_t *result) {
    if (left > UINT64_MAX - right) return true;
    *result = left + right;
    return false;
}

static bool ld_elf_add_signed(uint64_t value, int64_t addend,
                              uint64_t *result) {
    if (addend >= 0) {
        return !ld_elf_add_overflow(value, (uint64_t) addend, result);
    }
    uint64_t magnitude = (uint64_t) (-(addend + 1)) + 1U;
    if (magnitude > value) return false;
    *result = value - magnitude;
    return true;
}

/*
 * Compute the mathematical S + A used by Elf64_Rela.r_addend.  Unlike an
 * address calculation, a dynamic relocation addend may legitimately be
 * negative, so the unsigned-address helper above is intentionally not used.
 */
static bool ld_elf_add_signed_to_i64(uint64_t value, int64_t addend,
                                     int64_t *result) {
    if (!result) return false;
    if (addend >= 0) {
        uint64_t positive_addend = (uint64_t) addend;
        if (value > (uint64_t) INT64_MAX - positive_addend) return false;
        *result = (int64_t) value + addend;
        return true;
    }

    uint64_t magnitude = (uint64_t) (-(addend + 1)) + 1U;
    if (value >= magnitude) {
        uint64_t positive = value - magnitude;
        if (positive > (uint64_t) INT64_MAX) return false;
        *result = (int64_t) positive;
        return true;
    }

    uint64_t negative_magnitude = magnitude - value;
    if (negative_magnitude > UINT64_C(0x8000000000000000)) return false;
    if (negative_magnitude == UINT64_C(0x8000000000000000)) {
        *result = INT64_MIN;
    } else {
        *result = -(int64_t) negative_magnitude;
    }
    return true;
}

static bool ld_elf_subtract_signed(uint64_t value, int64_t subtrahend,
                                   uint64_t *result) {
    if (subtrahend >= 0) {
        uint64_t magnitude = (uint64_t) subtrahend;
        if (magnitude > value) return false;
        *result = value - magnitude;
        return true;
    }
    uint64_t magnitude = (uint64_t) (-(subtrahend + 1)) + 1U;
    return !ld_elf_add_overflow(value, magnitude, result);
}

static bool ld_elf_mul_overflow(size_t left, size_t right, size_t *result) {
    if (left != 0U && right > SIZE_MAX / left) return true;
    *result = left * right;
    return false;
}

static bool ld_elf_align(uint64_t value, uint64_t align, uint64_t *result) {
    if (align == 0U) align = 1U;
    uint64_t mask = align - 1U;
    if ((align & mask) != 0U || value > UINT64_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static char *ld_elf_strdup(const char *value) {
    size_t length = strlen(value);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

static void ld_elf_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
}

static void ld_elf_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_write_u32(bytes, (uint32_t) value);
    ld_elf_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static ld_elf_synthetic_t ld_elf_synthetic_kind(
        const ld_elf_backend_t *backend, const char *name) {
    if (strcmp(name, "_DYNAMIC") == 0) {
        return backend->ctx->options->pie ? LD_ELF_SYNTHETIC_DYNAMIC
                                          : LD_ELF_SYNTHETIC_ZERO;
    }
    if (strcmp(name, "_PROCEDURE_LINKAGE_TABLE_") == 0) {
        return LD_ELF_SYNTHETIC_ZERO;
    }
    if (strcmp(name, "__rela_iplt_start") == 0)
        return LD_ELF_SYNTHETIC_RELA_IPLT_START;
    if (strcmp(name, "__rela_iplt_end") == 0)
        return LD_ELF_SYNTHETIC_RELA_IPLT_END;
    if (strcmp(name, "__ehdr_start") == 0 ||
        strcmp(name, "__executable_start") == 0) {
        return LD_ELF_SYNTHETIC_IMAGE_START;
    }
    if (strcmp(name, "__init_array_start") == 0)
        return LD_ELF_SYNTHETIC_INIT_ARRAY_START;
    if (strcmp(name, "__init_array_end") == 0)
        return LD_ELF_SYNTHETIC_INIT_ARRAY_END;
    if (strcmp(name, "__fini_array_start") == 0)
        return LD_ELF_SYNTHETIC_FINI_ARRAY_START;
    if (strcmp(name, "__fini_array_end") == 0)
        return LD_ELF_SYNTHETIC_FINI_ARRAY_END;
    if (strcmp(name, "__preinit_array_start") == 0)
        return LD_ELF_SYNTHETIC_PREINIT_ARRAY_START;
    if (strcmp(name, "__preinit_array_end") == 0)
        return LD_ELF_SYNTHETIC_PREINIT_ARRAY_END;
    if (strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0)
        return LD_ELF_SYNTHETIC_GOT;
    if (strcmp(name, "__bss_start") == 0 ||
        strcmp(name, "__bss_start__") == 0) {
        return LD_ELF_SYNTHETIC_BSS_START;
    }
    if (strcmp(name, "_end") == 0 || strcmp(name, "end") == 0 ||
        strcmp(name, "__end__") == 0 || strcmp(name, "_bss_end__") == 0 ||
        strcmp(name, "__bss_end__") == 0) {
        return LD_ELF_SYNTHETIC_END;
    }
    if (strcmp(name, "_etext") == 0 || strcmp(name, "etext") == 0)
        return LD_ELF_SYNTHETIC_TEXT_END;
    if (strcmp(name, "_edata") == 0 || strcmp(name, "edata") == 0 ||
        strcmp(name, "__TMC_END__") == 0) {
        return LD_ELF_SYNTHETIC_DATA_END;
    }
    if (strcmp(name, "__eh_frame_start") == 0)
        return LD_ELF_SYNTHETIC_EH_FRAME_START;
    if (strcmp(name, "__GNU_EH_FRAME_HDR") == 0)
        return LD_ELF_SYNTHETIC_EH_FRAME_HDR;
    if (strcmp(name, "__dso_handle") == 0)
        return LD_ELF_SYNTHETIC_DSO_HANDLE;
    if (backend->ctx->options->arch == LD_ARCH_RISCV64 &&
        strcmp(name, "__global_pointer$") == 0)
        return LD_ELF_SYNTHETIC_GLOBAL_POINTER;
    if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
        strcmp(name, "_TLS_MODULE_BASE_") == 0)
        return LD_ELF_SYNTHETIC_TLS_MODULE_BASE;
    return LD_ELF_SYNTHETIC_NONE;
}

static ld_elf_global_t *ld_elf_global_get(ld_elf_backend_t *backend,
                                          const char *name, bool create) {
    ld_elf_global_t *global = NULL;
    HASH_FIND_STR(backend->globals, name, global);
    if (global || !create) return global;
    global = calloc(1, sizeof(*global));
    if (!global) return NULL;
    global->name = ld_elf_strdup(name);
    if (!global->name) {
        free(global);
        return NULL;
    }
    global->got_index = LD_ELF_NO_GOT;
    global->gottp_index = LD_ELF_NO_GOT;
    global->tlsgd_index = LD_ELF_NO_GOT;
    global->pltgot_index = LD_ELF_NO_GOT;
    global->synthetic = ld_elf_synthetic_kind(backend, name);
    HASH_ADD_KEYPTR(hh, backend->globals, global->name, strlen(global->name), global);
    return global;
}

static void ld_elf_globals_clear(ld_elf_backend_t *backend) {
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        HASH_DEL(backend->globals, global);
        free(global->name);
        free(global);
    }
}

static bool ld_elf_is_eh_frame_section(const ld_elf_section_t *section) {
    return section && strcmp(section->name, ".eh_frame") == 0;
}

static void ld_elf_update_eh_frame_liveness(ld_elf_object_t *object) {
    for (size_t i = 1; i < object->section_count; i++) {
        ld_elf_section_t *section = &object->sections[i];
        if (!ld_elf_is_eh_frame_section(section)) continue;

        section->eh_output_size = 0U;
        for (size_t j = 0; j < section->eh_record_count; j++) {
            ld_elf_eh_record_t *record = &section->eh_records[j];
            record->alive = false;
            record->output_offset = 0U;
            record->canonical_cie_section = record->cie ? section : NULL;
            record->canonical_cie_record_index =
                    record->cie ? (uint32_t) j : LD_ELF_EH_CIE_NONE;
        }
        for (size_t j = 0; j < section->eh_record_count; j++) {
            ld_elf_eh_record_t *record = &section->eh_records[j];
            if (record->cie ||
                record->owner_section_index == LD_ELF_GROUP_NONE ||
                record->owner_section_index >= object->section_count) {
                continue;
            }
            const ld_elf_section_t *owner =
                    &object->sections[record->owner_section_index];
            if (owner->discarded) {
                continue;
            }
            record->alive = true;
            if (record->cie_record_index < section->eh_record_count)
                section->eh_records[record->cie_record_index].alive = true;
        }
        for (size_t j = 0; j < section->eh_record_count; j++) {
            ld_elf_eh_record_t *record = &section->eh_records[j];
            if (!record->alive) continue;
            record->output_offset = section->eh_output_size;
            section->eh_output_size += record->size;
        }
    }
}

typedef struct {
    ld_elf_object_t *object;
    ld_elf_section_t *section;
    uint32_t record_index;
} ld_elf_cie_context_t;

static bool ld_elf_relocation_is_in_eh_record(
        const ld_elf_relocation_t *relocation,
        const ld_elf_eh_record_t *record) {
    return relocation->offset >= record->input_offset &&
           relocation->offset - record->input_offset < record->size;
}

static void ld_elf_cie_relocation_key(
        ld_elf_object_t *object, const ld_elf_eh_record_t *record,
        const ld_elf_relocation_t *relocation,
        ld_elf_cie_relocation_t *key) {
    ld_elf_symbol_t *symbol = &object->symbols[relocation->symbol_index];
    key->offset = relocation->offset - record->input_offset;
    key->type = relocation->type;
    key->addend = relocation->addend;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
        symbol->resolved) {
        key->target_namespace = symbol->resolved;
        key->target_index = 0U;
    } else {
        key->target_namespace = object;
        key->target_index = symbol->index;
    }
}

static int ld_elf_deduplicate_cies(ld_elf_backend_t *backend) {
    size_t cie_count = 0U;
    size_t relocation_count = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!ld_elf_is_eh_frame_section(section)) continue;
            for (size_t k = 0; k < section->eh_record_count; k++) {
                ld_elf_eh_record_t *record = &section->eh_records[k];
                if (!record->cie || !record->alive) continue;
                if (cie_count == SIZE_MAX) {
                    return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                       "too many live ELF CIE records");
                }
                cie_count++;
                for (size_t m = 0; m < section->relocation_count; m++) {
                    if (!ld_elf_relocation_is_in_eh_record(
                                &section->relocations[m], record)) {
                        continue;
                    }
                    if (relocation_count == SIZE_MAX) {
                        return ld_elf_fail(
                                backend->ctx, LD_OUTPUT_ERROR,
                                "too many live ELF CIE relocations");
                    }
                    relocation_count++;
                }
            }
        }
    }
    if (cie_count == 0U) return LD_OK;

    size_t entry_bytes, context_bytes, canonical_bytes, relocation_bytes;
    if (ld_elf_mul_overflow(cie_count, sizeof(ld_elf_cie_entry_t),
                            &entry_bytes) ||
        ld_elf_mul_overflow(cie_count, sizeof(ld_elf_cie_context_t),
                            &context_bytes) ||
        ld_elf_mul_overflow(cie_count, sizeof(size_t), &canonical_bytes) ||
        ld_elf_mul_overflow(relocation_count,
                            sizeof(ld_elf_cie_relocation_t),
                            &relocation_bytes)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF CIE deduplication allocation overflows");
    }
    ld_elf_cie_entry_t *entries = calloc(1, entry_bytes);
    ld_elf_cie_context_t *contexts = malloc(context_bytes);
    size_t *canonical_indices = malloc(canonical_bytes);
    ld_elf_cie_relocation_t *relocations =
            relocation_count ? malloc(relocation_bytes) : NULL;
    if (!entries || !contexts || !canonical_indices ||
        (relocation_count && !relocations)) {
        free(entries);
        free(contexts);
        free(canonical_indices);
        free(relocations);
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory deduplicating ELF CIE records");
    }

    size_t cie_index = 0U;
    size_t relocation_index = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!ld_elf_is_eh_frame_section(section)) continue;
            for (size_t k = 0; k < section->eh_record_count; k++) {
                ld_elf_eh_record_t *record = &section->eh_records[k];
                if (!record->cie || !record->alive) continue;
                contexts[cie_index] = (ld_elf_cie_context_t) {
                        .object = object,
                        .section = section,
                        .record_index = (uint32_t) k,
                };
                entries[cie_index].bytes =
                        section->data + record->input_offset;
                entries[cie_index].bytes_size = (size_t) record->size;
                size_t first_relocation = relocation_index;
                for (size_t m = 0; m < section->relocation_count; m++) {
                    ld_elf_relocation_t *relocation =
                            &section->relocations[m];
                    if (!ld_elf_relocation_is_in_eh_record(relocation,
                                                           record)) {
                        continue;
                    }
                    ld_elf_cie_relocation_key(
                            object, record, relocation,
                            &relocations[relocation_index++]);
                }
                entries[cie_index].relocation_count =
                        relocation_index - first_relocation;
                entries[cie_index].relocations =
                        entries[cie_index].relocation_count
                                ? &relocations[first_relocation]
                                : NULL;
                cie_index++;
            }
        }
    }

    size_t error_entry_index = SIZE_MAX;
    ld_elf_cie_result_t result = ld_elf_cie_deduplicate(
            entries, cie_count, canonical_indices, &error_entry_index);
    int status = LD_OK;
    if (result != LD_ELF_CIE_OK) {
        const char *object_name =
                error_entry_index < cie_count
                        ? contexts[error_entry_index].object->display_name
                        : NULL;
        status = object_name
                         ? ld_elf_fail(
                                   backend->ctx, LD_OUTPUT_ERROR,
                                   "cannot deduplicate ELF CIE in '%s': %s",
                                   object_name,
                                   ld_elf_cie_result_string(result))
                         : ld_elf_fail(
                                   backend->ctx, LD_OUTPUT_ERROR,
                                   "cannot deduplicate ELF CIE records: %s",
                                   ld_elf_cie_result_string(result));
    }
    if (status == LD_OK) {
        for (size_t i = 0; i < cie_count; i++) {
            ld_elf_cie_context_t *context = &contexts[i];
            ld_elf_eh_record_t *record =
                    &context->section->eh_records[context->record_index];
            ld_elf_cie_context_t *canonical =
                    &contexts[canonical_indices[i]];
            record->canonical_cie_section = canonical->section;
            record->canonical_cie_record_index = canonical->record_index;
            if (canonical_indices[i] != i) record->alive = false;
        }
        for (size_t i = 0; i < backend->ctx->objects.count; i++) {
            ld_elf_object_t *object = backend->ctx->objects.items[i];
            if (!object->selected) continue;
            for (size_t j = 1; j < object->section_count; j++) {
                ld_elf_section_t *section = &object->sections[j];
                if (!ld_elf_is_eh_frame_section(section)) continue;
                section->eh_output_size = 0U;
                for (size_t k = 0; k < section->eh_record_count; k++) {
                    ld_elf_eh_record_t *record = &section->eh_records[k];
                    record->output_offset = 0U;
                    if (!record->alive) continue;
                    record->output_offset = section->eh_output_size;
                    section->eh_output_size += record->size;
                }
            }
        }
    }

    free(entries);
    free(contexts);
    free(canonical_indices);
    free(relocations);
    return status;
}

static bool ld_elf_map_input_offset(const ld_elf_section_t *section,
                                    uint64_t input_offset, uint64_t width,
                                    uint64_t *output_offset, bool *alive,
                                    uint64_t *available) {
    if (section && section->merge_input) {
        return ld_elf_merge_map_input(section, input_offset, width,
                                      output_offset, alive, available);
    }
    if (section && ld_elf_riscv_relax_plan_active(
                           &section->riscv_relax_plan)) {
        return ld_elf_riscv_relax_map(
                &section->riscv_relax_plan, input_offset, width,
                output_offset, alive, available);
    }
    if (!ld_elf_is_eh_frame_section(section)) {
        if (input_offset > section->header.sh_size ||
            width > section->header.sh_size - input_offset) {
            return false;
        }
        if (output_offset) *output_offset = input_offset;
        if (alive) *alive = true;
        if (available) *available = section->header.sh_size - input_offset;
        return true;
    }
    if (input_offset == section->header.sh_size && width == 0U) {
        if (output_offset) *output_offset = section->eh_output_size;
        if (alive) *alive = true;
        if (available) *available = 0U;
        return true;
    }
    for (size_t i = 0; i < section->eh_record_count; i++) {
        const ld_elf_eh_record_t *record = &section->eh_records[i];
        if (input_offset < record->input_offset ||
            input_offset - record->input_offset >= record->size) {
            continue;
        }
        uint64_t within = input_offset - record->input_offset;
        if (width > record->size - within) return false;
        if (output_offset) *output_offset = record->output_offset + within;
        if (alive) *alive = record->alive;
        if (available) *available = record->size - within;
        return true;
    }
    return false;
}

static bool ld_elf_relocation_is_live(const ld_elf_section_t *section,
                                      const ld_elf_relocation_t *relocation) {
    bool alive = true;
    if (!ld_elf_map_input_offset(section, relocation->offset, 0U, NULL,
                                 &alive, NULL)) {
        return true;
    }
    return alive;
}

static void ld_elf_resolve_comdat_groups(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        for (size_t j = 0; j < object->section_count; j++) {
            object->sections[j].group_discarded = false;
        }
        for (size_t j = 0; j < object->group_count; j++) {
            object->groups[j].discarded = false;
        }
    }

    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->group_count; j++) {
            ld_elf_group_t *group = &object->groups[j];
            if (!group->is_comdat) continue;

            bool duplicate = false;
            for (size_t k = 0; k <= i && !duplicate; k++) {
                ld_elf_object_t *earlier = backend->ctx->objects.items[k];
                if (!earlier->selected) continue;
                size_t limit = k == i ? j : earlier->group_count;
                for (size_t m = 0; m < limit; m++) {
                    ld_elf_group_t *winner = &earlier->groups[m];
                    if (winner->is_comdat && !winner->discarded &&
                        strcmp(winner->signature, group->signature) == 0) {
                        duplicate = true;
                        break;
                    }
                }
            }
            if (!duplicate) continue;

            group->discarded = true;
            for (size_t k = 0; k < group->member_count; k++) {
                uint32_t member = group->members[k];
                if (member < object->section_count) {
                    object->sections[member].group_discarded = true;
                }
            }
        }
    }
}

static bool ld_elf_section_base_discarded(
        const ld_elf_backend_t *backend,
        const ld_elf_section_t *section) {
    if (section->group_discarded ||
        (section->header.sh_flags & LD_ELF_SHF_EXCLUDE) != 0U) {
        return true;
    }
    if ((section->header.sh_flags & LD_ELF_SHF_ALLOC) != 0U) return false;
    bool keep_dwarf = backend->ctx->options->debug_mode == LD_DEBUG_DWARF;
    return ld_elf_debug_classify_nonalloc_section(
                   section->name, section->header.sh_type,
                   section->header.sh_flags, keep_dwarf) ==
           LD_ELF_DEBUG_SECTION_SKIP;
}

static void ld_elf_mark_discarded_sections(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->section_count; j++) {
            object->sections[j].discarded =
                    ld_elf_section_base_discarded(
                            backend, &object->sections[j]);
            object->sections[j].link_order_discard_state = 0U;
        }

        /*
         * The ELF gABI requires a SHF_LINK_ORDER contribution to disappear
         * when its associated input section disappears.  The parser already
         * rejected cycles, so a three-state chain walk computes the same
         * fixed point in linear time, including multi-level dependencies.
         *
         * Zig commit 738d2be9 only formats the L flag and does not implement
         * this rule; the propagation follows the ELF gABI and GNU/lld linker
         * behavior rather than claiming to be a Zig translation.
         */
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if ((section->header.sh_flags & LD_ELF_SHF_LINK_ORDER) == 0U ||
                section->link_order_discard_state == 2U) {
                continue;
            }

            uint32_t cursor = (uint32_t) j;
            while (cursor != LD_ELF_SHN_UNDEF) {
                ld_elf_section_t *current = &object->sections[cursor];
                if ((current->header.sh_flags &
                     LD_ELF_SHF_LINK_ORDER) == 0U ||
                    current->discarded ||
                    current->link_order_discard_state != 0U) {
                    break;
                }
                current->link_order_discard_state = 1U;
                cursor = current->header.sh_link;
            }

            bool discarded = cursor != LD_ELF_SHN_UNDEF &&
                             object->sections[cursor].discarded;
            cursor = (uint32_t) j;
            while (cursor != LD_ELF_SHN_UNDEF) {
                ld_elf_section_t *current = &object->sections[cursor];
                if (current->link_order_discard_state != 1U) break;
                current->discarded = discarded;
                current->link_order_discard_state = 2U;
                cursor = current->header.sh_link;
            }
        }
    }

    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (object->selected) ld_elf_update_eh_frame_liveness(object);
    }
}

static bool ld_elf_symbol_has_live_reference(const ld_elf_object_t *object,
                                             uint32_t symbol_index) {
    bool referenced = false;
    for (size_t i = 1; i < object->section_count; i++) {
        const ld_elf_section_t *section = &object->sections[i];
        for (size_t j = 0; j < section->relocation_count; j++) {
            if (section->relocations[j].symbol_index != symbol_index) continue;
            referenced = true;
            if (section->relocations[j].x86_tls_pair_follower) continue;
            if (!section->discarded &&
                ld_elf_relocation_is_live(section,
                                          &section->relocations[j])) {
                return true;
            }
        }
    }
    /* Preserve ELF undefined declarations which have no relocation site. */
    return !referenced;
}

static int ld_elf_record_definition(ld_elf_backend_t *backend,
                                    ld_elf_object_t *object,
                                    ld_elf_symbol_t *symbol) {
    ld_elf_global_t *global = ld_elf_global_get(backend, symbol->name, true);
    if (!global) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory resolving ELF symbol '%s'", symbol->name);
    }
    symbol->resolved = global;
    bool common = symbol->entry.st_shndx == LD_ELF_SHN_COMMON;
    bool weak = symbol->binding == LD_ELF_STB_WEAK;
    if (common) {
        if (!global->definition) {
            bool tls = symbol->type == LD_ELF_STT_TLS;
            if (global->common && global->common_tls != tls) {
                return ld_elf_fail(
                        backend->ctx, LD_SYMBOL_ERROR,
                        "ELF common symbol '%s' has conflicting TLS types",
                        symbol->name);
            }
            global->common = true;
            global->common_tls = tls;
            if (!weak) global->common_reference = true;
            if (symbol->entry.st_size > global->common_size)
                global->common_size = symbol->entry.st_size;
            uint64_t align = symbol->entry.st_value ? symbol->entry.st_value : 1U;
            if (align > global->common_align) global->common_align = align;
        }
        return LD_OK;
    }

    if (!global->definition) {
        global->definition = symbol;
        global->object = object;
        global->definition_weak = weak;
        global->common = false;
        global->common_tls = false;
        return LD_OK;
    }
    if (global->definition_weak && !weak) {
        global->definition = symbol;
        global->object = object;
        global->definition_weak = false;
        global->common = false;
        global->common_tls = false;
        return LD_OK;
    }
    if (!global->definition_weak && weak) return LD_OK;
    if (global->definition_weak && weak) return LD_OK;
    return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                       "duplicate strong ELF symbol '%s': '%s' and '%s'",
                       symbol->name, global->object->display_name,
                       object->display_name);
}

static int ld_elf_record_undefined_references(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            for (size_t k = 0;
                 k < object->sections[j].relocation_count; k++) {
                object->sections[j].relocations[k].ifunc_irelative = false;
            }
        }
        for (size_t j = 0; j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (!ld_elf_symbol_is_global(symbol) || !symbol->name[0] ||
                !ld_elf_symbol_is_undefined(symbol) ||
                !ld_elf_symbol_has_live_reference(object, symbol->index)) {
                continue;
            }
            ld_elf_global_t *global =
                    ld_elf_global_get(backend, symbol->name, true);
            if (!global) {
                return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                                   "out of memory resolving ELF symbol '%s'",
                                   symbol->name);
            }
            symbol->resolved = global;
            global->referenced = true;
            if (symbol->binding != LD_ELF_STB_WEAK)
                global->strong_reference = true;
        }
    }
    return LD_OK;
}

static int ld_elf_record_entry_reference(ld_elf_backend_t *backend) {
    const char *entry_name = backend->ctx->options->entry_symbol &&
                                             *backend->ctx->options->entry_symbol
                                     ? backend->ctx->options->entry_symbol
                                     : "_start";
    ld_elf_global_t *global = ld_elf_global_get(backend, entry_name, true);
    if (!global) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory resolving ELF entry symbol '%s'",
                           entry_name);
    }
    /*
     * Zig models --entry as an undefined symbol in LinkerDefined.zig.  That
     * synthetic reference must participate in lazy archive extraction even
     * when no ordinary object relocates against the entry point.
     */
    global->referenced = true;
    global->strong_reference = true;
    global->entry_reference = true;
    return LD_OK;
}

static int ld_elf_rebuild_globals(ld_elf_backend_t *backend) {
    ld_elf_resolve_comdat_groups(backend);
    ld_elf_mark_discarded_sections(backend);
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        int status = ld_elf_relocation_prepare_x86_tls_sequences(
                backend->ctx, object);
        if (status != LD_OK) return status;
    }
    ld_elf_globals_clear(backend);
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            symbol->resolved = NULL;
            symbol->got_index = LD_ELF_NO_GOT;
            symbol->gottp_index = LD_ELF_NO_GOT;
            symbol->tlsgd_index = LD_ELF_NO_GOT;
            symbol->pltgot_index = LD_ELF_NO_GOT;
            symbol->common_offset = 0U;
            if (!ld_elf_symbol_is_global(symbol) || !symbol->name[0]) continue;
            if (ld_elf_symbol_is_undefined(symbol)) continue;
            if (symbol->section && symbol->section->discarded) continue;
            int status = ld_elf_record_definition(backend, object, symbol);
            if (status != LD_OK) return status;
        }
    }

    int status = ld_elf_record_undefined_references(backend);
    if (status != LD_OK) return status;

    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (!ld_elf_symbol_is_global(symbol) || !symbol->name[0]) continue;
            symbol->resolved = ld_elf_global_get(backend, symbol->name, false);
        }
    }
    status = ld_elf_deduplicate_cies(backend);
    if (status != LD_OK) return status;

    /* A duplicate CIE's relocations no longer contribute undefined
       references. Recompute the reference flags after deduplication so a
       dead personality reference cannot extract an archive member or report
       a false undefined symbol. */
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        global->referenced = false;
        global->strong_reference = false;
    }
    status = ld_elf_record_undefined_references(backend);
    if (status != LD_OK) return status;
    return ld_elf_record_entry_reference(backend);
}

static int ld_elf_extract_archives(ld_elf_backend_t *backend) {
    while (true) {
        int status = ld_elf_rebuild_globals(backend);
        if (status != LD_OK) return status;
        ld_elf_object_t *best_object = NULL;
        unsigned best_rank = UINT_MAX;
        for (size_t i = 0; i < backend->ctx->objects.count; i++) {
            ld_elf_object_t *object = backend->ctx->objects.items[i];
            if (!object->lazy || object->selected) continue;
            for (size_t j = object->first_global_symbol;
                 j < object->symbol_count; j++) {
                ld_elf_symbol_t *symbol = &object->symbols[j];
                if (!ld_elf_symbol_is_definition(symbol) || !symbol->name[0])
                    continue;
                ld_elf_global_t *global = ld_elf_global_get(
                        backend, symbol->name, false);
                bool regular_overrides_common =
                        global && global->common && global->common_reference &&
                        symbol->entry.st_shndx != LD_ELF_SHN_COMMON;
                if (!global ||
                    (!global->strong_reference &&
                     !regular_overrides_common) ||
                    global->synthetic != LD_ELF_SYNTHETIC_NONE) {
                    continue;
                }
                if (global->definition && !global->common) continue;
                unsigned rank = symbol->entry.st_shndx == LD_ELF_SHN_COMMON
                                        ? 2U
                                        : (symbol->binding == LD_ELF_STB_WEAK ? 1U : 0U);
                if (!best_object || rank < best_rank) {
                    best_object = object;
                    best_rank = rank;
                }
            }
        }
        if (!best_object) break;
        status = ld_elf_select_object(backend->ctx, best_object);
        if (status != LD_OK) return status;
    }
    return ld_elf_rebuild_globals(backend);
}

static int ld_elf_merge_object_flags(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_RISCV64) {
        backend->elf_flags = 0U;
        return LD_OK;
    }

    const ld_elf_object_t *first = NULL;
    uint32_t abi_flags = 0U;
    uint32_t rvc = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        const ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        uint32_t flags = object->header.e_flags;
        if ((flags & ~LD_ELF_RISCV_KNOWN_FLAGS) != 0U) {
            return ld_elf_fail(
                    backend->ctx, LD_UNSUPPORTED,
                    "unsupported RISC-V ELF flags 0x%x in '%s'", flags,
                    object->display_name);
        }
        uint32_t object_abi = flags & ~LD_ELF_RISCV_RVC;
        if (!first) {
            first = object;
            abi_flags = object_abi;
        } else if (object_abi != abi_flags) {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    "incompatible RISC-V ELF ABI flags: '%s' uses 0x%x but "
                    "'%s' uses 0x%x",
                    first->display_name, first->header.e_flags,
                    object->display_name, flags);
        }
        rvc |= flags & LD_ELF_RISCV_RVC;
    }
    backend->elf_flags = abi_flags | rvc;
    return LD_OK;
}

static int ld_elf_prepare_riscv_relaxations(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_RISCV64) return LD_OK;

    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        bool rvc = (object->header.e_flags & LD_ELF_RISCV_RVC) != 0U;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (section->discarded) continue;
            size_t alignment_count = 0U;
            for (size_t k = 0; k < section->relocation_count; k++) {
                if (section->relocations[k].type ==
                    LD_ELF_R_RISCV_ALIGN) {
                    alignment_count++;
                }
            }
            if (alignment_count == 0U) continue;

            uint64_t required_flags =
                    LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR;
            if (section->header.sh_type != LD_ELF_SHT_PROGBITS ||
                section->nobits ||
                (section->header.sh_flags & required_flags) !=
                        required_flags ||
                (section->header.sh_flags &
                 (LD_ELF_SHF_MERGE | LD_ELF_SHF_LINK_ORDER)) != 0U ||
                ld_elf_is_eh_frame_section(section)) {
                return ld_elf_fail(
                        backend->ctx, LD_UNSUPPORTED,
                        "R_RISCV_ALIGN is only supported in ordinary "
                        "allocated executable PROGBITS sections; found it "
                        "in section '%s' in '%s'",
                        section->name, object->display_name);
            }
            if (alignment_count >
                SIZE_MAX / sizeof(ld_elf_riscv_align_input_t)) {
                return ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "too many R_RISCV_ALIGN relocations in section '%s' "
                        "in '%s'",
                        section->name, object->display_name);
            }
            ld_elf_riscv_align_input_t *alignments =
                    malloc(alignment_count * sizeof(*alignments));
            if (!alignments) {
                return ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "out of memory collecting R_RISCV_ALIGN relocations "
                        "in section '%s' in '%s'",
                        section->name, object->display_name);
            }
            size_t alignment_index = 0U;
            for (size_t k = 0; k < section->relocation_count; k++) {
                const ld_elf_relocation_t *relocation =
                        &section->relocations[k];
                if (relocation->type != LD_ELF_R_RISCV_ALIGN) continue;
                alignments[alignment_index++] =
                        (ld_elf_riscv_align_input_t) {
                                .offset = relocation->offset,
                                .addend = relocation->addend,
                                .source_index = k,
                        };
            }
            size_t error_relocation_index = SIZE_MAX;
            ld_elf_riscv_relax_result_t result =
                    ld_elf_riscv_relax_plan_build(
                            &section->riscv_relax_plan, section->data,
                            section->data_size, alignments, alignment_count,
                            rvc, &error_relocation_index);
            free(alignments);
            if (result != LD_ELF_RISCV_RELAX_OK) {
                int code = result == LD_ELF_RISCV_RELAX_OUT_OF_MEMORY
                                   ? LD_IO_ERROR
                                   : LD_INVALID_INPUT;
                if (error_relocation_index < section->relocation_count) {
                    const ld_elf_relocation_t *relocation =
                            &section->relocations[error_relocation_index];
                    return ld_elf_fail(
                            backend->ctx, code,
                            "invalid R_RISCV_ALIGN at offset 0x%llx with "
                            "addend %lld in section '%s' in '%s': %s",
                            (unsigned long long) relocation->offset,
                            (long long) relocation->addend, section->name,
                            object->display_name,
                            ld_elf_riscv_relax_result_string(result));
                }
                return ld_elf_fail(
                        backend->ctx, code,
                        "cannot prepare RISC-V alignment relaxation for "
                        "section '%s' in '%s': %s",
                        section->name, object->display_name,
                        ld_elf_riscv_relax_result_string(result));
            }
        }
    }
    return LD_OK;
}

static int ld_elf_check_undefined(ld_elf_backend_t *backend) {
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (!global->strong_reference || global->entry_reference ||
            global->definition || global->common ||
            global->synthetic != LD_ELF_SYNTHETIC_NONE) {
            continue;
        }
        return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                           "undefined ELF symbol '%s'", global->name);
    }
    return LD_OK;
}

static uint64_t ld_elf_output_key_flags(uint64_t flags) {
    return flags &
           (LD_ELF_SHF_WRITE | LD_ELF_SHF_ALLOC |
            LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_TLS);
}

static bool ld_elf_output_compatible(
        const ld_elf_output_section_t *output, uint32_t type,
        uint64_t flags) {
    return output->type == type &&
           ld_elf_output_key_flags(output->flags) ==
                   ld_elf_output_key_flags(flags);
}

static ld_elf_output_section_t *ld_elf_output_get(
        ld_elf_backend_t *backend, const char *name, uint32_t type,
        uint64_t flags, uint64_t align) {
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if (strcmp(output->name, name) == 0 &&
            ld_elf_output_compatible(output, type, flags)) {
            if (align > output->align) output->align = align;
            bool both_merge = (output->flags & LD_ELF_SHF_MERGE) != 0U &&
                              (flags & LD_ELF_SHF_MERGE) != 0U;
            bool both_strings =
                    (output->flags & LD_ELF_SHF_STRINGS) != 0U &&
                    (flags & LD_ELF_SHF_STRINGS) != 0U;
            bool both_link_order =
                    (output->flags & LD_ELF_SHF_LINK_ORDER) != 0U &&
                    (flags & LD_ELF_SHF_LINK_ORDER) != 0U;
            output->flags |= flags &
                             (LD_ELF_SHF_WRITE | LD_ELF_SHF_ALLOC |
                              LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_TLS);
            if (!both_merge) {
                output->flags &=
                        ~(LD_ELF_SHF_MERGE | LD_ELF_SHF_STRINGS);
                output->entry_size = 0U;
            } else if (!both_strings) {
                output->flags &= ~LD_ELF_SHF_STRINGS;
            }
            if (!both_link_order) {
                output->flags &= ~LD_ELF_SHF_LINK_ORDER;
                output->link_order_target = NULL;
                output->link_order_target_recorded = false;
            }
            return output;
        }
    }
    if (backend->outputs.count == backend->outputs.capacity) {
        if (backend->outputs.capacity > SIZE_MAX / 2U) return NULL;
        size_t next = backend->outputs.capacity
                              ? backend->outputs.capacity * 2U
                              : 16U;
        size_t bytes;
        if (ld_elf_mul_overflow(next, sizeof(*backend->outputs.items), &bytes))
            return NULL;
        void *items = realloc(backend->outputs.items, bytes);
        if (!items) return NULL;
        backend->outputs.items = items;
        backend->outputs.capacity = next;
    }
    ld_elf_output_section_t *output = calloc(1, sizeof(*output));
    if (!output) return NULL;
    output->name = ld_elf_strdup(name);
    if (!output->name) {
        free(output);
        return NULL;
    }
    output->type = type;
    output->flags = flags;
    output->align = align ? align : 1U;
    /* The stable section index is assigned after the output list is sorted. */
    output->index = 0U;
    backend->outputs.items[backend->outputs.count++] = output;
    return output;
}

static int ld_elf_prepare_gnu_property(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        size_t input_count = 0U;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->discarded &&
                strcmp(section->name, ".note.gnu.property") == 0) {
                input_count++;
            }
        }
        if (input_count > SIZE_MAX / sizeof(ld_elf_property_input_t)) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "too many GNU property sections in '%s'",
                    object->display_name);
        }
        ld_elf_property_input_t *inputs =
                input_count ? calloc(input_count, sizeof(*inputs)) : NULL;
        if (input_count && !inputs) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory collecting GNU property sections from "
                    "'%s'",
                    object->display_name);
        }
        size_t input_index = 0U;
        int status = LD_OK;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (section->discarded ||
                strcmp(section->name, ".note.gnu.property") != 0) {
                continue;
            }
            uint64_t load_flags =
                    section->header.sh_flags &
                    (LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE |
                     LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_TLS);
            if (section->header.sh_type != LD_ELF_SHT_NOTE ||
                load_flags != LD_ELF_SHF_ALLOC || section->nobits ||
                section->header.sh_addralign != 8U ||
                section->header.sh_entsize != 0U ||
                section->header.sh_link != 0U ||
                section->header.sh_info != 0U ||
                section->relocation_count != 0U) {
                status = ld_elf_fail(
                        backend->ctx, LD_INVALID_INPUT,
                        "invalid ELF .note.gnu.property section %zu in '%s': "
                        "expected allocated SHT_NOTE with alignment 8 and "
                        "no links, entries, or relocations",
                        j, object->display_name);
                break;
            }
            inputs[input_index++] = (ld_elf_property_input_t) {
                    .data = section->data,
                    .size = section->data_size,
                    .section_index = section->index,
            };
        }
        if (status != LD_OK) {
            free(inputs);
            return status;
        }
        ld_elf_property_result_t result = ld_elf_property_add_object(
                &backend->property_plan, backend->ctx->options->arch,
                inputs, input_count);
        uint32_t error_section = UINT32_MAX;
        if (backend->property_plan.error_input_index < input_count) {
            error_section =
                    inputs[backend->property_plan.error_input_index]
                            .section_index;
        }
        free(inputs);
        if (result != LD_ELF_PROPERTY_OK) {
            int code = result == LD_ELF_PROPERTY_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : (result ==
                                                  LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY
                                          ? LD_UNSUPPORTED
                                          : LD_INVALID_INPUT);
            if (backend->property_plan.error_property_type != UINT32_MAX) {
                return ld_elf_fail(
                        backend->ctx, code,
                        "%s 0x%x at offset 0x%zx in ELF section %u "
                        "'.note.gnu.property' from '%s'",
                        ld_elf_property_result_string(result),
                        backend->property_plan.error_property_type,
                        backend->property_plan.error_offset, error_section,
                        object->display_name);
            }
            return ld_elf_fail(
                    backend->ctx, code,
                    "%s at offset 0x%zx in ELF section %u "
                    "'.note.gnu.property' from '%s'",
                    ld_elf_property_result_string(result),
                    backend->property_plan.error_offset, error_section,
                    object->display_name);
        }
    }
    ld_elf_property_result_t result =
            ld_elf_property_finalize(&backend->property_plan);
    if (result != LD_ELF_PROPERTY_OK) {
        return ld_elf_fail(
                backend->ctx,
                result == LD_ELF_PROPERTY_OUT_OF_MEMORY ? LD_IO_ERROR
                                                        : LD_OUTPUT_ERROR,
                "cannot merge ELF GNU properties: %s",
                ld_elf_property_result_string(result));
    }
    if (backend->property_plan.output_size == 0U) return LD_OK;
    backend->gnu_property = ld_elf_output_get(
            backend, ".note.gnu.property", LD_ELF_SHT_NOTE,
            LD_ELF_SHF_ALLOC, 8U);
    if (!backend->gnu_property) {
        return ld_elf_fail(
                backend->ctx, LD_IO_ERROR,
                "out of memory creating ELF .note.gnu.property output");
    }
    backend->gnu_property->size = backend->property_plan.output_size;
    backend->gnu_property->file_size = backend->property_plan.output_size;
    return LD_OK;
}

static bool ld_elf_is_c_identifier(const char *name) {
    if (!name || !name[0]) return false;
    unsigned char first = (unsigned char) name[0];
    if (!((first >= 'A' && first <= 'Z') ||
          (first >= 'a' && first <= 'z') || first == '_')) {
        return false;
    }
    for (size_t i = 1U; name[i]; i++) {
        unsigned char value = (unsigned char) name[i];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_')) {
            return false;
        }
    }
    return true;
}

static int ld_elf_define_start_stop_symbol(
        ld_elf_backend_t *backend, ld_elf_output_section_t *output,
        const char *prefix, ld_elf_synthetic_t synthetic) {
    size_t prefix_size = strlen(prefix);
    size_t output_name_size = strlen(output->name);
    if (output_name_size > SIZE_MAX - prefix_size - 1U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF linker-defined section symbol name is too "
                           "long for output section '%s'",
                           output->name);
    }
    size_t name_size = prefix_size + output_name_size + 1U;
    char *name = malloc(name_size);
    if (!name) {
        return ld_elf_fail(
                backend->ctx, LD_IO_ERROR,
                "out of memory creating ELF linker-defined symbol for "
                "section '%s'",
                output->name);
    }
    memcpy(name, prefix, prefix_size);
    memcpy(name + prefix_size, output->name, output_name_size + 1U);

    ld_elf_global_t *global = ld_elf_global_get(backend, name, true);
    free(name);
    if (!global) {
        return ld_elf_fail(
                backend->ctx, LD_IO_ERROR,
                "out of memory resolving ELF linker-defined symbol for "
                "section '%s'",
                output->name);
    }

    /*
     * LinkerDefined.initStartStopSymbols in Zig commit 738d2be9 installs
     * these resolver entries after ordinary input resolution and makes the
     * linker-defined reference authoritative even if an input supplied the
     * same weak or strong name. Preserve that phase behavior here.
     */
    global->definition = NULL;
    global->object = NULL;
    global->common = false;
    global->common_tls = false;
    global->common_reference = false;
    global->common_size = 0U;
    global->common_align = 0U;
    global->common_offset = 0U;
    global->definition_weak = false;
    global->synthetic = synthetic;
    global->synthetic_output = output;
    return LD_OK;
}

static int ld_elf_init_start_stop_symbols(ld_elf_backend_t *backend) {
    /*
     * This is a direct C translation of Elf.getStartStopBasename and
     * LinkerDefined.initStartStopSymbols from Zig commit 738d2be9. Only
     * allocated output sections whose complete name is a C identifier get
     * the conventional pair.
     */
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if ((output->flags & LD_ELF_SHF_ALLOC) == 0U ||
            !ld_elf_is_c_identifier(output->name)) {
            continue;
        }
        int status = ld_elf_define_start_stop_symbol(
                backend, output, "__start_",
                LD_ELF_SYNTHETIC_SECTION_START);
        if (status != LD_OK) return status;
        status = ld_elf_define_start_stop_symbol(
                backend, output, "__stop_",
                LD_ELF_SYNTHETIC_SECTION_STOP);
        if (status != LD_OK) return status;
    }
    return LD_OK;
}

static bool ld_elf_section_family(const char *name, const char *base,
                                  const char **suffix) {
    size_t base_length = strlen(base);
    if (strcmp(name, base) == 0) {
        if (suffix) *suffix = NULL;
        return true;
    }
    if (strncmp(name, base, base_length) != 0 || name[base_length] != '.')
        return false;
    if (suffix) *suffix = name + base_length + 1U;
    return true;
}

static ld_elf_array_kind_t ld_elf_array_kind(
        const ld_elf_section_t *section, bool *legacy) {
    const char *unused;
    *legacy = false;
    if (section->header.sh_type == LD_ELF_SHT_PREINIT_ARRAY ||
        ld_elf_section_family(section->name, ".preinit_array", &unused)) {
        return LD_ELF_ARRAY_PREINIT;
    }
    if (section->header.sh_type == LD_ELF_SHT_INIT_ARRAY ||
        ld_elf_section_family(section->name, ".init_array", &unused)) {
        return LD_ELF_ARRAY_INIT;
    }
    if (section->header.sh_type == LD_ELF_SHT_FINI_ARRAY ||
        ld_elf_section_family(section->name, ".fini_array", &unused)) {
        return LD_ELF_ARRAY_FINI;
    }
    if (ld_elf_section_family(section->name, ".ctors", &unused)) {
        *legacy = true;
        return LD_ELF_ARRAY_INIT;
    }
    if (ld_elf_section_family(section->name, ".dtors", &unused)) {
        *legacy = true;
        return LD_ELF_ARRAY_FINI;
    }
    return LD_ELF_ARRAY_NONE;
}

static int64_t ld_elf_array_priority(const ld_elf_section_t *section,
                                     ld_elf_array_kind_t kind, bool legacy) {
    const char *base = kind == LD_ELF_ARRAY_PREINIT
                               ? ".preinit_array"
                               : (kind == LD_ELF_ARRAY_INIT
                                          ? (legacy ? ".ctors" : ".init_array")
                                          : (legacy ? ".dtors" : ".fini_array"));
    const char *suffix = NULL;
    if (!ld_elf_section_family(section->name, base, &suffix) || !suffix ||
        !*suffix) {
        return legacy ? -1 : INT32_MAX;
    }
    uint64_t value = 0U;
    for (const char *cursor = suffix; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9')
            return legacy ? -1 : INT32_MAX;
        unsigned digit = (unsigned) (*cursor - '0');
        if (value > (UINT16_MAX - digit) / 10U)
            return legacy ? -1 : INT32_MAX;
        value = value * 10U + digit;
    }
    /* GCC's legacy .ctors.N/.dtors.N suffix is 65535 - priority. */
    return legacy ? (int64_t) UINT16_MAX - (int64_t) value
                  : (int64_t) value;
}

static int ld_elf_placement_push(ld_elf_backend_t *backend,
                                 ld_elf_input_placement_t placement) {
    if (backend->placements.count == backend->placements.capacity) {
        if (backend->placements.capacity > SIZE_MAX / 2U) return LD_IO_ERROR;
        size_t next = backend->placements.capacity
                              ? backend->placements.capacity * 2U
                              : 32U;
        size_t bytes;
        if (ld_elf_mul_overflow(next, sizeof(*backend->placements.items),
                                &bytes)) {
            return LD_IO_ERROR;
        }
        void *items = realloc(backend->placements.items, bytes);
        if (!items) return LD_IO_ERROR;
        backend->placements.items = items;
        backend->placements.capacity = next;
    }
    backend->placements.items[backend->placements.count++] = placement;
    return LD_OK;
}

static ld_elf_input_placement_t *ld_elf_placement_find(
        ld_elf_backend_t *backend, const ld_elf_section_t *section) {
    for (size_t i = 0; i < backend->placements.count; i++) {
        if (backend->placements.items[i].section == section)
            return &backend->placements.items[i];
    }
    return NULL;
}

static bool ld_elf_is_aarch64_branch_relocation(uint32_t type) {
    return type == LD_ELF_R_AARCH64_CALL26 ||
           type == LD_ELF_R_AARCH64_JUMP26;
}

static int ld_elf_build_aarch64_thunk_groups(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_ARM64 || !backend->text)
        return LD_OK;

    uint32_t current_group = LD_ELF_AARCH64_NO_THUNK;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        placement->aarch64_thunk_group_index = LD_ELF_AARCH64_NO_THUNK;
        if (placement->section->output != backend->text) continue;

        uint64_t start = placement->section->output_offset;
        bool new_group = current_group == LD_ELF_AARCH64_NO_THUNK;
        if (!new_group) {
            ld_elf_aarch64_thunk_group_t *group =
                    &backend->aarch64_thunk_plan.groups[current_group];
            if (start < group->payload_start_offset) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "AArch64 text placement order is not monotonic for "
                        "section '%s' in '%s'",
                        placement->section->name,
                        placement->object->display_name);
            }
            new_group = start - group->payload_start_offset >=
                        LD_ELF_AARCH64_THUNK_GROUP_MAX;
        }
        if (new_group && !ld_elf_aarch64_thunk_group_append(
                                 &backend->aarch64_thunk_plan, start, i,
                                 &current_group)) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory creating AArch64 branch thunk group");
        }

        placement->aarch64_thunk_group_index = current_group;
        ld_elf_aarch64_thunk_group_t *group =
                &backend->aarch64_thunk_plan.groups[current_group];
        group->last_placement_index = i;
        if (ld_elf_add_overflow(start, placement->emitted_size,
                                &group->payload_end_offset)) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "AArch64 thunk group size overflows for section '%s' "
                    "in '%s'",
                    placement->section->name,
                    placement->object->display_name);
        }
    }

    backend->aarch64_branch_relocation_count = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                relocation->aarch64_thunk_entry_index =
                        LD_ELF_AARCH64_NO_THUNK;
                if (section->output &&
                    ld_elf_is_aarch64_branch_relocation(relocation->type) &&
                    ld_elf_relocation_is_live(section, relocation)) {
                    if (backend->aarch64_branch_relocation_count == SIZE_MAX) {
                        return ld_elf_fail(
                                backend->ctx, LD_OUTPUT_ERROR,
                                "too many AArch64 branch relocations");
                    }
                    backend->aarch64_branch_relocation_count++;
                }
            }
        }
    }
    return LD_OK;
}

static int ld_elf_relayout_aarch64_text(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_ARM64 || !backend->text)
        return LD_OK;

    uint64_t cursor = 0U;
    bool has_thunks = false;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        if (placement->section->output != backend->text) continue;
        if (placement->aarch64_thunk_group_index >=
            backend->aarch64_thunk_plan.group_count) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "missing AArch64 thunk group for section '%s' in '%s'",
                    placement->section->name,
                    placement->object->display_name);
        }
        if (!ld_elf_align(cursor, placement->section->header.sh_addralign,
                          &cursor)) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "AArch64 text alignment overflows for section '%s' in "
                    "'%s'",
                    placement->section->name,
                    placement->object->display_name);
        }
        placement->section->output_offset = cursor;
        ld_elf_aarch64_thunk_group_t *group =
                &backend->aarch64_thunk_plan
                         .groups[placement->aarch64_thunk_group_index];
        if (i == group->first_placement_index)
            group->payload_start_offset = cursor;
        if (ld_elf_add_overflow(cursor, placement->emitted_size, &cursor)) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "AArch64 text size overflows for section '%s' in '%s'",
                    placement->section->name,
                    placement->object->display_name);
        }
        group->payload_end_offset = cursor;
        if (i != group->last_placement_index) continue;

        if (group->entry_count != 0U) {
            has_thunks = true;
            if (!ld_elf_align(cursor, 4U, &cursor)) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "AArch64 thunk alignment overflows");
            }
        }
        group->thunk_output_offset = cursor;
        if (group->entry_count > UINT64_MAX / LD_ELF_AARCH64_THUNK_SIZE ||
            ld_elf_add_overflow(
                    cursor,
                    (uint64_t) group->entry_count *
                            LD_ELF_AARCH64_THUNK_SIZE,
                    &cursor)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "AArch64 thunk island size overflows");
        }
    }
    backend->text->size = cursor;
    backend->text->file_size = cursor;
    if (has_thunks && backend->text->align < 4U) backend->text->align = 4U;
    return LD_OK;
}

static ld_elf_output_section_t *ld_elf_classified_output(
        ld_elf_backend_t *backend, ld_elf_output_section_t **slot,
        const char *name, uint32_t type, uint64_t flags, uint64_t align) {
    ld_elf_output_section_t *output =
            ld_elf_output_get(backend, name, type, flags, align);
    if (output) *slot = output;
    return output;
}

static ld_elf_output_section_t *ld_elf_classify_section(
        ld_elf_backend_t *backend, ld_elf_section_t *section) {
    uint64_t flags = section->header.sh_flags;
    uint64_t align = section->header.sh_addralign;
    uint64_t output_flags =
            flags & (LD_ELF_SHF_WRITE | LD_ELF_SHF_ALLOC |
                     LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_TLS);
    const char *unused;
    if ((flags & LD_ELF_SHF_TLS) != 0U) {
        if (section->header.sh_type == LD_ELF_SHT_NOBITS) {
            return ld_elf_classified_output(
                    backend, &backend->tbss, ".tbss", LD_ELF_SHT_NOBITS,
                    LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE | LD_ELF_SHF_TLS,
                    align);
        }
        return ld_elf_classified_output(
                backend, &backend->tdata, ".tdata", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE | LD_ELF_SHF_TLS,
                align);
    }
    bool legacy;
    ld_elf_array_kind_t array_kind = ld_elf_array_kind(section, &legacy);
    (void) legacy;
    if (array_kind == LD_ELF_ARRAY_PREINIT) {
        return ld_elf_classified_output(
                backend, &backend->preinit_array, ".preinit_array",
                LD_ELF_SHT_PREINIT_ARRAY,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, align);
    }
    if (array_kind == LD_ELF_ARRAY_INIT) {
        return ld_elf_classified_output(
                backend, &backend->init_array, ".init_array",
                LD_ELF_SHT_INIT_ARRAY,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, align);
    }
    if (array_kind == LD_ELF_ARRAY_FINI) {
        return ld_elf_classified_output(
                backend, &backend->fini_array, ".fini_array",
                LD_ELF_SHT_FINI_ARRAY,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, align);
    }
    if (strcmp(section->name, ".init") == 0) {
        return ld_elf_classified_output(
                backend, &backend->init, ".init", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, align);
    }
    if (strcmp(section->name, ".fini") == 0) {
        return ld_elf_classified_output(
                backend, &backend->fini, ".fini", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, align);
    }
    if ((flags & LD_ELF_SHF_EXECINSTR) != 0U &&
        ld_elf_section_family(section->name, ".text", &unused)) {
        return ld_elf_classified_output(
                backend, &backend->text, ".text", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, align);
    }
    if ((flags & LD_ELF_SHF_WRITE) != 0U) {
        if (ld_elf_section_family(section->name, ".data.rel.ro", &unused)) {
            return ld_elf_output_get(
                    backend, ".data.rel.ro", section->header.sh_type,
                    output_flags, align);
        }
        if (ld_elf_section_family(section->name, ".bss.rel.ro", &unused)) {
            return ld_elf_output_get(
                    backend, ".bss.rel.ro", LD_ELF_SHT_PROGBITS,
                    output_flags, align);
        }
        if (ld_elf_section_family(section->name, ".bss", &unused)) {
            return ld_elf_classified_output(
                    backend, &backend->bss, ".bss", LD_ELF_SHT_NOBITS,
                    LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, align);
        }
        if (ld_elf_section_family(section->name, ".data", &unused)) {
            return ld_elf_classified_output(
                    backend, &backend->data, ".data", LD_ELF_SHT_PROGBITS,
                    LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, align);
        }
        return ld_elf_output_get(
                backend, section->name, section->header.sh_type,
                output_flags, align);
    }
    if (strcmp(section->name, ".eh_frame") == 0) {
        return ld_elf_classified_output(
                backend, &backend->eh_frame, ".eh_frame",
                backend->ctx->options->arch == LD_ARCH_AMD64
                        ? LD_ELF_SHT_X86_64_UNWIND
                        : LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC, align);
    }
    if (ld_elf_section_family(section->name, ".rodata", &unused)) {
        return ld_elf_classified_output(
                backend, &backend->rodata, ".rodata", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC, align);
    }
    if (ld_elf_section_family(section->name, ".gcc_except_table", &unused)) {
        return ld_elf_output_get(
                backend, ".gcc_except_table", section->header.sh_type,
                output_flags, align);
    }
    if (ld_elf_section_family(section->name, ".gnu.warning", &unused)) {
        return ld_elf_output_get(
                backend, ".gnu.warning", section->header.sh_type,
                output_flags, align);
    }
    return ld_elf_output_get(backend, section->name,
                             section->header.sh_type, output_flags, align);
}

static int ld_elf_output_rank(const ld_elf_output_section_t *output) {
    /* Runtime sections always precede non-allocated debug/tool metadata in
       both the section table and the file layout. */
    if ((output->flags & LD_ELF_SHF_ALLOC) == 0U) return 90;
    if (strcmp(output->name, ".note.gnu.property") == 0) return 1;
    if (strcmp(output->name, ".dynsym") == 0) return 2;
    if (strcmp(output->name, ".dynstr") == 0) return 3;
    if (strcmp(output->name, ".hash") == 0) return 4;
    if (strcmp(output->name, ".gnu.hash") == 0) return 4;
    if (strcmp(output->name, ".rela.dyn") == 0) return 5;
    if (strcmp(output->name, ".init") == 0) return 9;
    if (strcmp(output->name, ".text") == 0) return 10;
    if (strcmp(output->name, ".fini") == 0) return 11;
    if (strcmp(output->name, ".plt.got") == 0) return 12;
    if (strcmp(output->name, ".rodata") == 0) return 20;
    if (strcmp(output->name, ".eh_frame_hdr") == 0) return 29;
    if (strcmp(output->name, ".eh_frame") == 0) return 30;
    if (strcmp(output->name, ".tdata") == 0) return 40;
    if (strcmp(output->name, ".tbss") == 0) return 41;
    if (strcmp(output->name, ".preinit_array") == 0) return 50;
    if (strcmp(output->name, ".init_array") == 0) return 51;
    if (strcmp(output->name, ".fini_array") == 0) return 52;
    if (strcmp(output->name, ".data.rel.ro") == 0) return 53;
    if (strcmp(output->name, ".bss.rel.ro") == 0) return 54;
    if (strcmp(output->name, ".dynamic") == 0) return 55;
    if (strcmp(output->name, ".got") == 0) return 60;
    if (strcmp(output->name, ".data") == 0) return 70;
    if (strcmp(output->name, ".bss") == 0) return 80;
    if ((output->flags & LD_ELF_SHF_MERGE) != 0U) {
        if ((output->flags & LD_ELF_SHF_EXECINSTR) != 0U) return 13;
        if ((output->flags & LD_ELF_SHF_WRITE) != 0U) return 71;
        return 21;
    }
    if ((output->flags & LD_ELF_SHF_EXECINSTR) != 0U) return 13;
    if ((output->flags & LD_ELF_SHF_WRITE) != 0U)
        return output->type == LD_ELF_SHT_NOBITS ? 80 : 71;
    return 21;
}

static int ld_elf_compare_output_order(
        const ld_elf_output_section_t *a,
        const ld_elf_output_section_t *b) {
    if (a == b) return 0;
    int ar = ld_elf_output_rank(a), br = ld_elf_output_rank(b);
    if (ar != br) return ar < br ? -1 : 1;
    int name_order = strcmp(a->name, b->name);
    if (name_order != 0) return name_order;
    if (a->type != b->type) return a->type < b->type ? -1 : 1;
    uint64_t a_key = ld_elf_output_key_flags(a->flags);
    uint64_t b_key = ld_elf_output_key_flags(b->flags);
    if (a_key != b_key) return a_key < b_key ? -1 : 1;
    if (a->flags != b->flags) return a->flags < b->flags ? -1 : 1;
    return 0;
}

static int ld_elf_compare_outputs(const void *left, const void *right) {
    const ld_elf_output_section_t *a =
            *(ld_elf_output_section_t *const *) left;
    const ld_elf_output_section_t *b =
            *(ld_elf_output_section_t *const *) right;
    return ld_elf_compare_output_order(a, b);
}

static int ld_elf_compare_array_placements(const void *left,
                                           const void *right) {
    const ld_elf_input_placement_t *a =
            *(ld_elf_input_placement_t *const *) left;
    const ld_elf_input_placement_t *b =
            *(ld_elf_input_placement_t *const *) right;
    if (a->array_kind != b->array_kind)
        return a->array_kind < b->array_kind ? -1 : 1;
    if (a->priority != b->priority)
        return a->priority < b->priority ? -1 : 1;
    if (a->sequence != b->sequence)
        return a->sequence < b->sequence ? -1 : 1;
    return 0;
}

static bool ld_elf_placement_is_link_order(
        const ld_elf_input_placement_t *placement) {
    return (placement->section->header.sh_flags &
            LD_ELF_SHF_LINK_ORDER) != 0U;
}

static int ld_elf_compare_link_order_placements(const void *left,
                                                const void *right) {
    const ld_elf_input_placement_t *a =
            *(ld_elf_input_placement_t *const *) left;
    const ld_elf_input_placement_t *b =
            *(ld_elf_input_placement_t *const *) right;
    const ld_elf_input_placement_t *at = a->link_order_target;
    const ld_elf_input_placement_t *bt = b->link_order_target;
    uint64_t tie_sequence_a = a->sequence;
    uint64_t tie_sequence_b = b->sequence;

    /*
     * Compare associated sections in their final output order.  If an
     * associated section is itself SHF_LINK_ORDER, walk both dependency
     * chains iteratively.  Input validation guarantees that the walks are
     * finite, avoiding recursion on objects with very deep section tables.
     */
    while (at != bt) {
        if (!at) return -1;
        if (!bt) return 1;
        int output_order = ld_elf_compare_output_order(
                at->section->output, bt->section->output);
        if (output_order != 0) return output_order;

        bool at_link_order = ld_elf_placement_is_link_order(at);
        bool bt_link_order = ld_elf_placement_is_link_order(bt);
        if (at_link_order && bt_link_order) {
            tie_sequence_a = at->sequence;
            tie_sequence_b = bt->sequence;
            at = at->link_order_target;
            bt = bt->link_order_target;
            continue;
        }
        if (!at_link_order && !bt_link_order) {
            if (at->section->output_offset != bt->section->output_offset) {
                return at->section->output_offset <
                                       bt->section->output_offset
                               ? -1
                               : 1;
            }
        }
        if (at->sequence != bt->sequence)
            return at->sequence < bt->sequence ? -1 : 1;
        break;
    }
    if (tie_sequence_a != tie_sequence_b)
        return tie_sequence_a < tie_sequence_b ? -1 : 1;
    if (a->sequence != b->sequence)
        return a->sequence < b->sequence ? -1 : 1;
    return 0;
}

static int ld_elf_place_one_input(ld_elf_backend_t *backend,
                                  ld_elf_input_placement_t *placement) {
    ld_elf_section_t *section = placement->section;
    ld_elf_output_section_t *output = section->output;
    uint64_t offset;
    if (!ld_elf_align(output->size, section->header.sh_addralign, &offset) ||
        ld_elf_add_overflow(offset, placement->emitted_size, &output->size)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF section layout overflow for '%s' in '%s'",
                           section->name, placement->object->display_name);
    }
    section->output_offset = offset;
    if (!section->nobits || output->type != LD_ELF_SHT_NOBITS)
        output->file_size = output->size;
    return LD_OK;
}

static int ld_elf_place_input_sections(ld_elf_backend_t *backend) {
    uint64_t sequence = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            section->output = NULL;
            section->output_offset = 0U;
            if (section->discarded) continue;
            if (strcmp(section->name, ".note.gnu.property") == 0) continue;
            switch (section->header.sh_type) {
                case LD_ELF_SHT_PROGBITS:
                case LD_ELF_SHT_NOBITS:
                case LD_ELF_SHT_INIT_ARRAY:
                case LD_ELF_SHT_FINI_ARRAY:
                case LD_ELF_SHT_PREINIT_ARRAY:
                case LD_ELF_SHT_NOTE:
                case LD_ELF_SHT_X86_64_UNWIND:
                    break;
                default:
                    return ld_elf_fail(backend->ctx, LD_UNSUPPORTED,
                                       "unsupported allocated ELF section type %u "
                                       "for '%s' in '%s'",
                                       section->header.sh_type, section->name,
                                       object->display_name);
            }
            bool link_order =
                    (section->header.sh_flags &
                     LD_ELF_SHF_LINK_ORDER) != 0U;
            /* Deduplication would destroy contribution order, so a section
               carrying both flags keeps its SHF_MERGE entry boundaries but
               uses the ordinary link-order placement path. */
            bool merge = !link_order &&
                         ld_elf_merge_section_eligible(section);
            uint64_t merge_flags =
                    section->header.sh_flags &
                    (LD_ELF_SHF_WRITE | LD_ELF_SHF_ALLOC |
                     LD_ELF_SHF_EXECINSTR | LD_ELF_SHF_MERGE |
                     LD_ELF_SHF_STRINGS | LD_ELF_SHF_LINK_ORDER |
                     LD_ELF_SHF_TLS);
            ld_elf_output_section_t *output;
            if (link_order || merge) {
                output = ld_elf_output_get(
                        backend, section->name, section->header.sh_type,
                        merge_flags, section->header.sh_addralign);
            } else {
                output = ld_elf_classify_section(backend, section);
            }
            if (!output) {
                return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                                   "out of memory creating output section for "
                                   "'%s' in '%s'",
                                   section->name, object->display_name);
            }
            section->output = output;
            if (link_order &&
                (section->header.sh_flags & LD_ELF_SHF_MERGE) != 0U) {
                uint64_t entry_size = section->header.sh_entsize;
                if (entry_size == 0U &&
                    (section->header.sh_flags &
                     LD_ELF_SHF_STRINGS) != 0U) {
                    entry_size = 1U;
                }
                if (entry_size != 0U &&
                    (output->entry_size == 0U ||
                     entry_size < output->entry_size)) {
                    output->entry_size = entry_size;
                }
            }
            if (merge) {
                int status = ld_elf_merge_add_section(
                        &backend->merge_plan, section, output);
                if (status != LD_OK) return status;
            }
            bool legacy;
            ld_elf_array_kind_t array_kind =
                    ld_elf_array_kind(section, &legacy);
            uint64_t emitted_size = merge ? 0U : section->header.sh_size;
            if (output == backend->eh_frame)
                emitted_size = section->eh_output_size;
            ld_elf_input_placement_t placement = {
                    .object = object,
                    .section = section,
                    .emitted_size = emitted_size,
                    .sequence = sequence++,
                    .priority = ld_elf_array_priority(section, array_kind,
                                                      legacy),
                    .array_kind = array_kind,
                    .aarch64_thunk_group_index =
                            LD_ELF_AARCH64_NO_THUNK,
            };
            if (ld_elf_placement_push(backend, placement) != LD_OK) {
                return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                                   "out of memory recording ELF input section "
                                   "'%s' in '%s'",
                                   section->name, object->display_name);
            }
        }
    }

    size_t link_order_count = 0U;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        if (!ld_elf_placement_is_link_order(placement)) continue;
        link_order_count++;
        uint32_t target_index = placement->section->header.sh_link;
        if (target_index != LD_ELF_SHN_UNDEF) {
            ld_elf_section_t *target =
                    &placement->object->sections[target_index];
            placement->link_order_target =
                    ld_elf_placement_find(backend, target);
            if (!placement->link_order_target) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "live ELF SHF_LINK_ORDER section '%s' in '%s' has "
                        "no live placement for associated section %u '%s'",
                        placement->section->name,
                        placement->object->display_name, target_index,
                        target->name);
            }
        }
        ld_elf_output_section_t *output = placement->section->output;
        if ((output->flags & LD_ELF_SHF_LINK_ORDER) != 0U &&
            !output->link_order_target_recorded) {
            output->link_order_target_recorded = true;
            output->link_order_target =
                    placement->link_order_target
                            ? placement->link_order_target->section->output
                            : NULL;
        }
    }

    size_t array_count = 0U;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        if (placement->section->merge_input) continue;
        if (ld_elf_placement_is_link_order(placement)) continue;
        if (placement->array_kind == LD_ELF_ARRAY_NONE) {
            int status = ld_elf_place_one_input(backend, placement);
            if (status != LD_OK) return status;
        } else {
            array_count++;
        }
    }
    if (array_count) {
        size_t bytes;
        if (ld_elf_mul_overflow(array_count,
                                sizeof(ld_elf_input_placement_t *), &bytes)) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "too many ELF init/fini array sections");
        }
        ld_elf_input_placement_t **arrays = malloc(bytes);
        if (!arrays) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "out of memory sorting ELF init/fini arrays");
        }
        size_t index = 0U;
        for (size_t i = 0; i < backend->placements.count; i++) {
            if (!ld_elf_placement_is_link_order(
                        &backend->placements.items[i]) &&
                backend->placements.items[i].array_kind != LD_ELF_ARRAY_NONE)
                arrays[index++] = &backend->placements.items[i];
        }
        qsort(arrays, array_count, sizeof(*arrays),
              ld_elf_compare_array_placements);
        for (size_t i = 0; i < array_count; i++) {
            int status = ld_elf_place_one_input(backend, arrays[i]);
            if (status != LD_OK) {
                free(arrays);
                return status;
            }
        }
        free(arrays);
    }

    int merge_status = ld_elf_merge_finalize(&backend->merge_plan);
    if (merge_status != LD_OK) return merge_status;

    if (link_order_count) {
        size_t bytes;
        if (ld_elf_mul_overflow(link_order_count,
                                sizeof(ld_elf_input_placement_t *), &bytes)) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "too many ELF SHF_LINK_ORDER sections");
        }
        ld_elf_input_placement_t **ordered = malloc(bytes);
        if (!ordered) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory sorting ELF SHF_LINK_ORDER sections");
        }
        size_t index = 0U;
        for (size_t i = 0; i < backend->placements.count; i++) {
            if (ld_elf_placement_is_link_order(
                        &backend->placements.items[i])) {
                ordered[index++] = &backend->placements.items[i];
            }
        }
        qsort(ordered, link_order_count, sizeof(*ordered),
              ld_elf_compare_link_order_placements);
        for (size_t i = 0; i < link_order_count; i++) {
            int status = ld_elf_place_one_input(backend, ordered[i]);
            if (status != LD_OK) {
                free(ordered);
                return status;
            }
        }
        free(ordered);
    }

    if (backend->eh_frame &&
        ld_elf_add_overflow(backend->eh_frame->size, 4U,
                            &backend->eh_frame->size)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF .eh_frame terminator overflows address space");
    }
    if (backend->eh_frame)
        backend->eh_frame->file_size = backend->eh_frame->size;
    return LD_OK;
}

static int ld_elf_prepare_eh_frame_hdr(ld_elf_backend_t *backend) {
    if (!backend->eh_frame) return LD_OK;

    size_t fde_count = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        const ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            const ld_elf_section_t *section = &object->sections[j];
            if (section->output != backend->eh_frame) continue;
            for (size_t k = 0; k < section->eh_record_count; k++) {
                const ld_elf_eh_record_t *record = &section->eh_records[k];
                if (record->alive && !record->cie) {
                    if (fde_count == SIZE_MAX) {
                        return ld_elf_fail(
                                backend->ctx, LD_OUTPUT_ERROR,
                                "too many live ELF .eh_frame FDE records");
                    }
                    fde_count++;
                }
            }
        }
    }

    size_t encoded_size = 0U;
    ld_elf_eh_frame_hdr_result_t result =
            ld_elf_eh_frame_hdr_size(fde_count, &encoded_size);
    if (result != LD_ELF_EH_FRAME_HDR_OK) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "cannot size ELF .eh_frame_hdr: %s",
                           ld_elf_eh_frame_hdr_result_string(result));
    }
    backend->eh_frame_hdr = ld_elf_output_get(
            backend, ".eh_frame_hdr", LD_ELF_SHT_PROGBITS,
            LD_ELF_SHF_ALLOC, 4U);
    if (!backend->eh_frame_hdr) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory creating ELF .eh_frame_hdr");
    }
    backend->eh_frame_hdr->size = encoded_size;
    backend->eh_frame_hdr->file_size = encoded_size;
    backend->eh_frame_fde_count = fde_count;
    return LD_OK;
}

static int ld_elf_allocate_one_common(ld_elf_backend_t *backend,
                                      const char *name, uint64_t size,
                                      uint64_t align, bool tls,
                                      uint64_t *offset) {
    ld_elf_output_section_t **slot = tls ? &backend->tbss : &backend->bss;
    if (!*slot) {
        *slot = ld_elf_output_get(
                backend, tls ? ".tbss" : ".bss", LD_ELF_SHT_NOBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE |
                        (tls ? LD_ELF_SHF_TLS : 0U),
                align);
        if (!*slot) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "out of memory allocating common symbol '%s'",
                               name);
        }
    }
    if (!ld_elf_align((*slot)->size, align, offset) ||
        ld_elf_add_overflow(*offset, size, &(*slot)->size)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "common symbol '%s' overflows ELF address space",
                           name);
    }
    if (align > (*slot)->align) (*slot)->align = align;
    return LD_OK;
}

static int ld_elf_allocate_common(ld_elf_backend_t *backend) {
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (!global->common || global->definition) continue;
        int status = ld_elf_allocate_one_common(
                backend, global->name, global->common_size,
                global->common_align, global->common_tls,
                &global->common_offset);
        if (status != LD_OK) return status;
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->first_global_symbol; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->entry.st_shndx != LD_ELF_SHN_COMMON) continue;
            uint64_t align = symbol->entry.st_value
                                     ? symbol->entry.st_value
                                     : 1U;
            int status = ld_elf_allocate_one_common(
                    backend, symbol->name, symbol->entry.st_size, align,
                    symbol->type == LD_ELF_STT_TLS, &symbol->common_offset);
            if (status != LD_OK) return status;
        }
    }
    return LD_OK;
}

static ld_elf_symbol_t *ld_elf_ifunc_definition(
        ld_elf_symbol_t *symbol) {
    if (!symbol) return NULL;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved || !symbol->resolved->definition)
            return NULL;
        symbol = symbol->resolved->definition;
    }
    if (symbol->type != LD_ELF_STT_GNU_IFUNC ||
        symbol->entry.st_shndx == LD_ELF_SHN_UNDEF ||
        (symbol->section && (symbol->section->group_discarded ||
                             symbol->section->discarded))) {
        return NULL;
    }
    return symbol;
}

static bool ld_elf_symbol_needs_base_relocation(
        const ld_elf_symbol_t *symbol) {
    if (!symbol) return false;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        const ld_elf_global_t *global = symbol->resolved;
        if (!global) return false;
        if (global->common) return true;
        if (global->definition) symbol = global->definition;
        else {
            return global->synthetic != LD_ELF_SYNTHETIC_NONE &&
                   global->synthetic != LD_ELF_SYNTHETIC_ZERO;
        }
    }
    if (symbol->entry.st_shndx == LD_ELF_SHN_ABS ||
        symbol->entry.st_shndx == LD_ELF_SHN_UNDEF) {
        return false;
    }
    if (symbol->entry.st_shndx == LD_ELF_SHN_COMMON) return true;
    return symbol->section && symbol->section->output &&
           (symbol->section->output->flags & LD_ELF_SHF_ALLOC) != 0U;
}

static bool ld_elf_pie_dynamic_absolute_relocation(ld_arch_t arch,
                                                   uint32_t type) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return type == LD_ELF_R_X86_64_64;
        case LD_ARCH_ARM64:
            return type == LD_ELF_R_AARCH64_ABS64;
        case LD_ARCH_RISCV64:
            return type == LD_ELF_R_RISCV_64;
        default:
            return false;
    }
}

static bool ld_elf_pie_forbidden_absolute_relocation(ld_arch_t arch,
                                                     uint32_t type) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return type == LD_ELF_R_X86_64_8 ||
                   type == LD_ELF_R_X86_64_16 ||
                   type == LD_ELF_R_X86_64_32 ||
                   type == LD_ELF_R_X86_64_32S;
        case LD_ARCH_ARM64:
            return type == LD_ELF_R_AARCH64_ABS16 ||
                   type == LD_ELF_R_AARCH64_ABS32;
        case LD_ARCH_RISCV64:
            return type == LD_ELF_R_RISCV_32 ||
                   type == LD_ELF_R_RISCV_HI20;
        default:
            return false;
    }
}

static bool ld_elf_pie_pc_relocation_rejects_absolute(ld_arch_t arch,
                                                      uint32_t type) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return type == LD_ELF_R_X86_64_PC32;
        case LD_ARCH_ARM64:
            return type == LD_ELF_R_AARCH64_ADR_PREL_PG_HI21;
        case LD_ARCH_RISCV64:
        default:
            return false;
    }
}

static bool ld_elf_symbol_resolves_to_absolute(
        const ld_elf_symbol_t *symbol) {
    if (!symbol) return false;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        const ld_elf_global_t *global = symbol->resolved;
        if (!global) return false;
        if (!global->definition) {
            /*
             * Zig claims unresolved weak symbols in an executable as the
             * absolute value zero.  A linker-defined or common symbol still
             * has image-relative storage and must not take this path.
             */
            return symbol->binding == LD_ELF_STB_WEAK && !global->common &&
                   global->synthetic == LD_ELF_SYNTHETIC_NONE;
        }
        symbol = global->definition;
    }
    return symbol->entry.st_shndx == LD_ELF_SHN_ABS;
}

static bool ld_elf_ifunc_absolute_relocation(ld_arch_t arch,
                                             uint32_t type) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return type == LD_ELF_R_X86_64_64;
        case LD_ARCH_ARM64:
            return type == LD_ELF_R_AARCH64_ABS64;
        case LD_ARCH_RISCV64:
            return type == LD_ELF_R_RISCV_64;
        default:
            return false;
    }
}

static int ld_elf_allocate_ifunc_indirection(
        ld_elf_backend_t *backend, ld_elf_symbol_t *symbol,
        ld_elf_relocation_t *relocation) {
    ld_elf_symbol_t *definition = ld_elf_ifunc_definition(symbol);
    if (!definition) return LD_OK;

    uint32_t *got_index = &definition->got_index;
    uint32_t *pltgot_index = &definition->pltgot_index;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        got_index = &symbol->resolved->got_index;
        pltgot_index = &symbol->resolved->pltgot_index;
    }
    if (*got_index == LD_ELF_NO_GOT) {
        if (backend->got_count == UINT32_MAX) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "too many ELF GOT entries for IFUNC '%s'",
                               symbol->name);
        }
        *got_index = backend->got_count++;
    }
    if (*pltgot_index == LD_ELF_NO_GOT) {
        if (backend->pltgot_count == UINT32_MAX) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "too many ELF PLT-GOT entries for IFUNC '%s'",
                    symbol->name);
        }
        *pltgot_index = backend->pltgot_count++;
    }
    if (ld_elf_ifunc_absolute_relocation(
                backend->ctx->options->arch, relocation->type)) {
        if (backend->ifunc_input_relocation_count == SIZE_MAX) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "too many ELF IFUNC dynamic relocations");
        }
        relocation->ifunc_irelative = true;
        backend->ifunc_input_relocation_count++;
    }
    return LD_OK;
}

static bool ld_elf_x86_gotpcrelx_symbol_relaxable(
        const ld_elf_symbol_t *symbol) {
    if (!symbol) return false;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        const ld_elf_global_t *global = symbol->resolved;
        if (!global) return false;
        if (global->common) return true;
        symbol = global->definition;
        if (!symbol) return false;
    }
    if (symbol->type == LD_ELF_STT_GNU_IFUNC ||
        symbol->entry.st_shndx == LD_ELF_SHN_UNDEF ||
        symbol->entry.st_shndx == LD_ELF_SHN_ABS) {
        return false;
    }
    return symbol->entry.st_shndx == LD_ELF_SHN_COMMON ||
           (symbol->section && !symbol->section->group_discarded &&
            !symbol->section->discarded);
}

static int ld_elf_allocate_got_entry(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section, ld_elf_relocation_t *relocation,
        ld_elf_symbol_t *symbol, ld_elf_reloc_got_kind_t kind) {
    if ((kind == LD_ELF_RELOC_GOT_TP ||
         kind == LD_ELF_RELOC_GOT_TLSGD) &&
        symbol->type != LD_ELF_STT_TLS) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "%s in section '%s' in '%s' requires an STT_TLS symbol, "
                "but '%s' has type %u",
                ld_elf_relocation_name(backend->ctx->options->arch,
                                       relocation->type),
                section->name, object->display_name, symbol->name,
                symbol->type);
    }

    uint32_t *index;
    if (kind == LD_ELF_RELOC_GOT_TP) {
        index = &symbol->gottp_index;
    } else if (kind == LD_ELF_RELOC_GOT_TLSGD) {
        index = &symbol->tlsgd_index;
    } else {
        index = &symbol->got_index;
    }
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "unresolved GOT symbol '%s' in '%s'",
                               symbol->name, object->display_name);
        }
        if (kind == LD_ELF_RELOC_GOT_TP) {
            index = &symbol->resolved->gottp_index;
        } else if (kind == LD_ELF_RELOC_GOT_TLSGD) {
            index = &symbol->resolved->tlsgd_index;
        } else {
            index = &symbol->resolved->got_index;
        }
    }
    if (*index != LD_ELF_NO_GOT) return LD_OK;

    uint32_t *count;
    const char *kind_name;
    if (kind == LD_ELF_RELOC_GOT_TP) {
        count = &backend->gottp_count;
        kind_name = "GOTTP";
    } else if (kind == LD_ELF_RELOC_GOT_TLSGD) {
        count = &backend->tlsgd_count;
        kind_name = "TLSGD";
    } else {
        count = &backend->got_count;
        kind_name = "GOT";
    }
    if (*count == UINT32_MAX) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many ELF %s entries", kind_name);
    }
    *index = (*count)++;
    return LD_OK;
}

static bool ld_elf_global_needs_base_relocation(
        const ld_elf_global_t *global) {
    if (!global) return false;
    if (global->common) return true;
    if (global->definition) {
        return global->definition->entry.st_shndx != LD_ELF_SHN_ABS &&
               global->definition->entry.st_shndx != LD_ELF_SHN_UNDEF;
    }
    return global->synthetic != LD_ELF_SYNTHETIC_NONE &&
           global->synthetic != LD_ELF_SYNTHETIC_ZERO;
}

static int ld_elf_count_pie_got_relocations(ld_elf_backend_t *backend) {
    backend->pie_got_relocation_count = 0U;
    if (!backend->ctx->options->pie || backend->got_count == 0U)
        return LD_OK;

    bool *seen = calloc(backend->got_count, sizeof(*seen));
    if (!seen) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory counting static PIE GOT "
                           "relocations");
    }
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (global->got_index == LD_ELF_NO_GOT) continue;
        if (global->got_index >= backend->got_count ||
            seen[global->got_index]) {
            free(seen);
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "invalid or duplicate ELF GOT index for '%s'",
                               global->name);
        }
        seen[global->got_index] = true;
        if (global->definition &&
            global->definition->type == LD_ELF_STT_GNU_IFUNC) {
            continue;
        }
        if (ld_elf_global_needs_base_relocation(global))
            backend->pie_got_relocation_count++;
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->first_global_symbol; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->got_index == LD_ELF_NO_GOT) continue;
            if (symbol->got_index >= backend->got_count ||
                seen[symbol->got_index]) {
                free(seen);
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "invalid or duplicate ELF GOT index for local "
                        "symbol '%s' in '%s'",
                        symbol->name, object->display_name);
            }
            seen[symbol->got_index] = true;
            if (symbol->type != LD_ELF_STT_GNU_IFUNC &&
                ld_elf_symbol_needs_base_relocation(symbol)) {
                backend->pie_got_relocation_count++;
            }
        }
    }
    free(seen);
    return LD_OK;
}

static int ld_elf_prepare_static_pie_outputs(
        ld_elf_backend_t *backend, size_t dynamic_relocation_count) {
    if (!backend->ctx->options->pie) return LD_OK;

    backend->dynstr = ld_elf_output_get(
            backend, ".dynstr", LD_ELF_SHT_STRTAB, LD_ELF_SHF_ALLOC, 1U);
    backend->dynsym = ld_elf_output_get(
            backend, ".dynsym", LD_ELF_SHT_DYNSYM, LD_ELF_SHF_ALLOC, 8U);
    backend->hash = ld_elf_output_get(
            backend, ".hash", LD_ELF_SHT_HASH, LD_ELF_SHF_ALLOC, 4U);
    backend->gnu_hash = ld_elf_output_get(
            backend, ".gnu.hash", LD_ELF_SHT_GNU_HASH, LD_ELF_SHF_ALLOC,
            8U);
    backend->dynamic = ld_elf_output_get(
            backend, ".dynamic", LD_ELF_SHT_DYNAMIC,
            LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, 8U);
    if (!backend->dynstr || !backend->dynsym || !backend->hash ||
        !backend->gnu_hash || !backend->dynamic || !backend->rela_dyn) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory creating ELF static PIE dynamic "
                           "metadata");
    }

    backend->dynstr->size = LD_ELF_DYNAMIC_MIN_DYNSTR_SIZE;
    backend->dynstr->file_size = backend->dynstr->size;
    backend->dynstr->entry_size = 1U;
    backend->dynsym->size = LD_ELF_DYNAMIC_MIN_DYNSYM_SIZE;
    backend->dynsym->file_size = backend->dynsym->size;
    backend->dynsym->entry_size = LD_ELF64_SYM_SIZE;
    backend->dynsym->header_link = backend->dynstr;
    backend->dynsym->header_info = 1U;
    backend->hash->size = LD_ELF_DYNAMIC_MIN_HASH_SIZE;
    backend->hash->file_size = backend->hash->size;
    backend->hash->entry_size = 4U;
    backend->hash->header_link = backend->dynsym;
    backend->gnu_hash->size = LD_ELF_DYNAMIC_MIN_GNU_HASH_SIZE;
    backend->gnu_hash->file_size = backend->gnu_hash->size;
    backend->gnu_hash->header_link = backend->dynsym;
    backend->dynamic->entry_size = LD_ELF64_DYN_SIZE;
    backend->dynamic->header_link = backend->dynstr;
    backend->rela_dyn->header_link = backend->dynsym;

    ld_elf_dynamic_metadata_t metadata = {
            .has_init = backend->init != NULL,
            .has_fini = backend->fini != NULL,
            .has_init_array = backend->init_array != NULL,
            .has_fini_array = backend->fini_array != NULL,
            .has_rela = dynamic_relocation_count != 0U,
            .has_static_tls = backend->gottp_count != 0U,
    };
    size_t dynamic_size;
    ld_elf_dynamic_result_t result =
            ld_elf_dynamic_metadata_size(&metadata, &dynamic_size);
    if (result != LD_ELF_DYNAMIC_OK) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "cannot size ELF static PIE .dynamic section: %s",
                ld_elf_dynamic_result_string(result));
    }
    backend->dynamic->size = dynamic_size;
    backend->dynamic->file_size = dynamic_size;
    return LD_OK;
}

static int ld_elf_prepare_got_outputs(ld_elf_backend_t *backend) {
    uint64_t scalar_count = (uint64_t) backend->got_count +
                            (uint64_t) backend->gottp_count;
    if (scalar_count != 0U || backend->tlsgd_count != 0U ||
        backend->needs_got_base) {
        backend->got = ld_elf_output_get(
                backend, ".got", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_WRITE, 8U);
        if (!backend->got) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "out of memory creating ELF GOT");
        }
        if (scalar_count > UINT64_MAX / 8U) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF GOT size overflows the address space");
        }
        uint64_t got_size = scalar_count * 8U;
        if ((uint64_t) backend->tlsgd_count >
            (UINT64_MAX - got_size) / 16U) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF TLSGD GOT size overflows the address space");
        }
        backend->got->size =
                got_size + (uint64_t) backend->tlsgd_count * 16U;
        backend->got->file_size = backend->got->size;
    }

    if (backend->pltgot_count != 0U) {
        backend->plt_got = ld_elf_output_get(
                backend, ".plt.got", LD_ELF_SHT_PROGBITS,
                LD_ELF_SHF_ALLOC | LD_ELF_SHF_EXECINSTR, 16U);
        if (!backend->plt_got) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory creating ELF IFUNC PLT-GOT section");
        }
        backend->plt_got->size = (uint64_t) backend->pltgot_count *
                                 LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE;
        backend->plt_got->file_size = backend->plt_got->size;
    }

    int status = ld_elf_count_pie_got_relocations(backend);
    if (status != LD_OK) return status;
    size_t relative_count = backend->pie_input_relocation_count;
    if (relative_count > SIZE_MAX - backend->pie_got_relocation_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF static PIE relative relocation count "
                           "overflows");
    }
    relative_count += backend->pie_got_relocation_count;
    backend->relative_relocation_count = relative_count;

    size_t relocation_count = relative_count;
    if (relocation_count >
        SIZE_MAX - backend->ifunc_input_relocation_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF dynamic relocation count overflows");
    }
    relocation_count += backend->ifunc_input_relocation_count;
    if (relocation_count > SIZE_MAX - backend->pltgot_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF dynamic relocation count overflows");
    }
    relocation_count += backend->pltgot_count;
    if (relocation_count > UINT64_MAX / LD_ELF_IFUNC_RELA_SIZE) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF dynamic relocation section is too large");
    }
    if (relocation_count != 0U || backend->ctx->options->pie) {
        backend->rela_dyn = ld_elf_output_get(
                backend, ".rela.dyn", LD_ELF_SHT_RELA, LD_ELF_SHF_ALLOC,
                8U);
        if (!backend->rela_dyn) {
            return ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory creating ELF dynamic relocation section");
        }
        backend->rela_dyn->entry_size = LD_ELF64_RELA_SIZE;
    }
    if (backend->rela_dyn) {
        backend->rela_dyn->size =
                (uint64_t) relocation_count * LD_ELF_IFUNC_RELA_SIZE;
        backend->rela_dyn->file_size = backend->rela_dyn->size;
    }
    return ld_elf_prepare_static_pie_outputs(backend, relocation_count);
}

static int ld_elf_scan_got(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output ||
                (section->output->flags & LD_ELF_SHF_ALLOC) == 0U) {
                continue;
            }
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                if (relocation->x86_tls_pair_follower) continue;
                if (!ld_elf_relocation_is_live(section, relocation)) continue;
                ld_elf_symbol_t *symbol =
                        &object->symbols[relocation->symbol_index];
                relocation->x86_gotpcrelx_relax =
                        backend->ctx->options->arch == LD_ARCH_AMD64 &&
                        ld_elf_x86_gotpcrelx_symbol_relaxable(symbol) &&
                        ld_elf_relocation_can_relax_x86_gotpcrelx(
                                section, relocation);
                int status = ld_elf_allocate_ifunc_indirection(
                        backend, symbol, relocation);
                if (status != LD_OK) return status;
                if (backend->ctx->options->pie &&
                    (section->output->flags & LD_ELF_SHF_ALLOC) != 0U &&
                    !ld_elf_relocation_supported_in_static_pie(
                            backend->ctx->options->arch,
                            relocation->type)) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "%s for symbol '%s' at offset 0x%llx in "
                            "section '%s' in '%s' is not supported in an "
                            "ELF static PIE",
                            ld_elf_relocation_name(
                                    backend->ctx->options->arch,
                                    relocation->type),
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                if (relocation->ifunc_irelative &&
                    (section->output->flags & LD_ELF_SHF_WRITE) == 0U) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "%s for IFUNC symbol '%s' at offset 0x%llx in "
                            "read-only section '%s' in '%s' would require "
                            "a runtime IRELATIVE write into read-only memory",
                            ld_elf_relocation_name(
                                    backend->ctx->options->arch,
                                    relocation->type),
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                if (backend->ctx->options->pie &&
                    (section->output->flags & LD_ELF_SHF_ALLOC) != 0U &&
                    ld_elf_symbol_resolves_to_absolute(symbol) &&
                    ld_elf_pie_pc_relocation_rejects_absolute(
                            backend->ctx->options->arch,
                            relocation->type)) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "%s against absolute symbol '%s' in section "
                            "'%s' in '%s' cannot be used in an ELF static "
                            "PIE",
                            ld_elf_relocation_name(
                                    backend->ctx->options->arch,
                                    relocation->type),
                            symbol->name, section->name,
                            object->display_name);
                }
                if (backend->ctx->options->pie &&
                    (section->output->flags & LD_ELF_SHF_ALLOC) != 0U &&
                    !relocation->ifunc_irelative &&
                    ld_elf_symbol_needs_base_relocation(symbol)) {
                    if (ld_elf_pie_dynamic_absolute_relocation(
                                backend->ctx->options->arch,
                                relocation->type)) {
                        if ((section->output->flags & LD_ELF_SHF_WRITE) ==
                            0U) {
                            return ld_elf_fail(
                                    backend->ctx, LD_RELOCATION_ERROR,
                                    "%s for symbol '%s' at offset 0x%llx "
                                    "in read-only section '%s' in '%s' "
                                    "would require a static PIE text "
                                    "relocation",
                                    ld_elf_relocation_name(
                                            backend->ctx->options->arch,
                                            relocation->type),
                                    symbol->name,
                                    (unsigned long long) relocation->offset,
                                    section->name, object->display_name);
                        }
                        if (backend->pie_input_relocation_count == SIZE_MAX) {
                            return ld_elf_fail(
                                    backend->ctx, LD_OUTPUT_ERROR,
                                    "too many ELF static PIE relative "
                                    "relocations");
                        }
                        relocation->pie_relative = true;
                        backend->pie_input_relocation_count++;
                    } else if (ld_elf_pie_forbidden_absolute_relocation(
                                       backend->ctx->options->arch,
                                       relocation->type)) {
                        return ld_elf_fail(
                                backend->ctx, LD_RELOCATION_ERROR,
                                "%s against non-absolute symbol '%s' in "
                                "section '%s' in '%s' cannot be used in "
                                "an ELF static PIE",
                                ld_elf_relocation_name(
                                        backend->ctx->options->arch,
                                        relocation->type),
                                symbol->name, section->name,
                                object->display_name);
                    }
                }
                if (ld_elf_relocation_needs_got_base(
                            backend->ctx->options->arch,
                            relocation->type)) {
                    backend->needs_got_base = true;
                }
                ld_elf_reloc_got_kind_t kind = ld_elf_relocation_got_kind(
                        backend->ctx->options->arch, relocation->type);
                if (kind == LD_ELF_RELOC_GOT_NONE) continue;
                if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
                    relocation->type == LD_ELF_R_X86_64_GOTTPOFF &&
                    relocation->x86_gottpoff_relax) {
                    continue;
                }
                if (kind == LD_ELF_RELOC_GOT_ORDINARY &&
                    relocation->x86_gotpcrelx_relax) {
                    continue;
                }
                status = ld_elf_allocate_got_entry(
                        backend, object, section, relocation, symbol, kind);
                if (status != LD_OK) return status;
            }
        }
    }
    return ld_elf_prepare_got_outputs(backend);
}

static int ld_elf_finalize_relro_layout(ld_elf_backend_t *backend,
                                        uint64_t *file_cursor,
                                        uint64_t *address_cursor,
                                        bool advance_file_cursor) {
    uint64_t next_file_cursor;
    uint64_t next_address_cursor;
    ld_elf_relro_result_t result = ld_elf_relro_finalize(
            &backend->relro_plan, backend->page_size, *file_cursor,
            *address_cursor, &next_file_cursor, &next_address_cursor);
    if (result != LD_ELF_RELRO_OK) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "cannot finalize ELF GNU RELRO layout: %s",
                           ld_elf_relro_result_string(result));
    }
    if (advance_file_cursor) *file_cursor = next_file_cursor;
    *address_cursor = next_address_cursor;
    return LD_OK;
}

static int ld_elf_compare_riscv_relax_placements(const void *left_pointer,
                                                 const void *right_pointer) {
    const ld_elf_input_placement_t *left =
            *(ld_elf_input_placement_t *const *) left_pointer;
    const ld_elf_input_placement_t *right =
            *(ld_elf_input_placement_t *const *) right_pointer;
    if (left->section->output_offset < right->section->output_offset)
        return -1;
    if (left->section->output_offset > right->section->output_offset)
        return 1;
    if (left->sequence < right->sequence) return -1;
    if (left->sequence > right->sequence) return 1;
    return 0;
}

/*
 * The final address of an output section is known while the program-header
 * layout walks output sections in order.  Reflowing all of that output's
 * input contributions at this point makes each R_RISCV_ALIGN decision final:
 * shrinking the current output can move later outputs, but never its own
 * start address.  This is the same forward dependency used by GNU ld's
 * section relaxation passes and avoids a guessed-address fixed point.
 */
static int ld_elf_relayout_riscv_output(ld_elf_backend_t *backend,
                                        ld_elf_output_section_t *output) {
    if (backend->ctx->options->arch != LD_ARCH_RISCV64) return LD_OK;

    bool active = false;
    size_t placement_count = 0U;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        if (placement->section->output != output) continue;
        placement_count++;
        if (ld_elf_riscv_relax_plan_active(
                    &placement->section->riscv_relax_plan)) {
            active = true;
        }
    }
    if (!active) return LD_OK;
    if ((output->flags & LD_ELF_SHF_EXECINSTR) == 0U ||
        output->type != LD_ELF_SHT_PROGBITS || placement_count == 0U) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "invalid output section '%s' for RISC-V alignment "
                "relaxation",
                output->name);
    }
    if (placement_count >
        SIZE_MAX / sizeof(ld_elf_input_placement_t *)) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "too many input placements in RISC-V output section '%s'",
                output->name);
    }
    ld_elf_input_placement_t **placements =
            malloc(placement_count * sizeof(*placements));
    if (!placements) {
        return ld_elf_fail(
                backend->ctx, LD_IO_ERROR,
                "out of memory reflowing RISC-V output section '%s'",
                output->name);
    }

    size_t placement_index = 0U;
    uint64_t original_end = 0U;
    int status = LD_OK;
    for (size_t i = 0; i < backend->placements.count; i++) {
        ld_elf_input_placement_t *placement = &backend->placements.items[i];
        if (placement->section->output != output) continue;
        if (placement->section->merge_input || placement->section->nobits ||
            ld_elf_is_eh_frame_section(placement->section) ||
            placement->array_kind != LD_ELF_ARRAY_NONE ||
            ld_elf_placement_is_link_order(placement)) {
            status = ld_elf_fail(
                    backend->ctx, LD_UNSUPPORTED,
                    "RISC-V alignment relaxation cannot reflow special "
                    "section '%s' from '%s'",
                    placement->section->name,
                    placement->object->display_name);
            break;
        }
        placements[placement_index++] = placement;
        uint64_t end;
        if (ld_elf_add_overflow(placement->section->output_offset,
                                placement->emitted_size, &end)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "RISC-V input placement end overflows in section '%s' "
                    "from '%s'",
                    placement->section->name,
                    placement->object->display_name);
            break;
        }
        if (end > original_end) original_end = end;
    }
    if (status == LD_OK && original_end != output->size) {
        status = ld_elf_fail(
                backend->ctx, LD_UNSUPPORTED,
                "RISC-V alignment relaxation cannot preserve synthetic "
                "content in output section '%s'",
                output->name);
    }
    if (status != LD_OK) {
        free(placements);
        return status;
    }

    qsort(placements, placement_count, sizeof(*placements),
          ld_elf_compare_riscv_relax_placements);
    uint64_t cursor = 0U;
    for (size_t i = 0; i < placement_count; i++) {
        ld_elf_input_placement_t *placement = placements[i];
        ld_elf_section_t *section = placement->section;
        if (!ld_elf_align(cursor, section->header.sh_addralign, &cursor)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "RISC-V relaxed placement alignment overflows for "
                    "section '%s' in '%s'",
                    section->name, placement->object->display_name);
            break;
        }
        section->output_offset = cursor;
        if (ld_elf_riscv_relax_plan_active(
                    &section->riscv_relax_plan)) {
            uint64_t section_address;
            if (ld_elf_add_overflow(output->addr, cursor,
                                    &section_address)) {
                status = ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "RISC-V relaxed section address overflows for '%s' "
                        "in '%s'",
                        section->name, placement->object->display_name);
                break;
            }
            size_t error_relocation_index = SIZE_MAX;
            ld_elf_riscv_relax_result_t result =
                    ld_elf_riscv_relax_plan_layout(
                            &section->riscv_relax_plan, section_address,
                            &error_relocation_index);
            if (result != LD_ELF_RISCV_RELAX_OK) {
                if (error_relocation_index < section->relocation_count) {
                    const ld_elf_relocation_t *relocation =
                            &section->relocations[error_relocation_index];
                    status = ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "cannot relax R_RISCV_ALIGN at offset 0x%llx "
                            "with addend %lld in section '%s' in '%s': %s",
                            (unsigned long long) relocation->offset,
                            (long long) relocation->addend, section->name,
                            placement->object->display_name,
                            ld_elf_riscv_relax_result_string(result));
                } else {
                    status = ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "cannot lay out RISC-V alignment padding in "
                            "section '%s' in '%s': %s",
                            section->name,
                            placement->object->display_name,
                            ld_elf_riscv_relax_result_string(result));
                }
                break;
            }
            placement->emitted_size =
                    section->riscv_relax_plan.output_size;
        }
        if (ld_elf_add_overflow(cursor, placement->emitted_size, &cursor)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "RISC-V relaxed output size overflows for section '%s' "
                    "in '%s'",
                    section->name, placement->object->display_name);
            break;
        }
    }
    free(placements);
    if (status != LD_OK) return status;
    output->size = cursor;
    output->file_size = cursor;
    return LD_OK;
}

static int ld_elf_layout_outputs(ld_elf_backend_t *backend) {
    /* x86 GOTPCRELX fallback and RISC-V relaxation may repeat layout. */
    ld_elf_relro_plan_init(&backend->relro_plan);
    if (backend->outputs.count > (size_t) UINT32_MAX - 1U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many ELF output sections for 32-bit section "
                           "indices");
    }
    if (backend->outputs.count > (size_t) UINT16_MAX - 4U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many ELF output sections for the ELF64 "
                           "header");
    }
    if (backend->outputs.count + 4U >= LD_ELF_SHN_LORESERVE) {
        return ld_elf_fail(backend->ctx, LD_UNSUPPORTED,
                           "ELF extended section numbering is not supported");
    }
    qsort(backend->outputs.items, backend->outputs.count,
          sizeof(*backend->outputs.items), ld_elf_compare_outputs);
    for (size_t i = 0; i < backend->outputs.count; i++) {
        backend->outputs.items[i]->index = (uint32_t) i + 1U;
    }
    bool has_tls = backend->tdata || backend->tbss;
    bool has_relro = false;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        const ld_elf_output_section_t *output = backend->outputs.items[i];
        if (ld_elf_relro_section_is_protected(
                    output->name, output->type, output->flags,
                    output->size)) {
            has_relro = true;
            break;
        }
    }
    backend->phnum =
            (uint16_t) (4U + (has_tls ? 1U : 0U) +
                        (backend->dynamic ? 1U : 0U) +
                        (backend->eh_frame_hdr ? 1U : 0U) +
                        (backend->gnu_property ? 2U : 0U) +
                        (has_relro ? 1U : 0U));
    backend->header_size = LD_ELF64_EHDR_SIZE +
                           (uint64_t) backend->phnum * LD_ELF64_PHDR_SIZE;
    uint64_t rx_cursor = backend->header_size;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if ((output->flags & LD_ELF_SHF_ALLOC) == 0U) continue;
        if ((output->flags & LD_ELF_SHF_WRITE) != 0U) continue;
        if (!ld_elf_align(rx_cursor, output->align, &rx_cursor))
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF read-only section layout overflow");
        output->file_offset = rx_cursor;
        output->addr = backend->image_base + rx_cursor;
        int relax_status =
                ld_elf_relayout_riscv_output(backend, output);
        if (relax_status != LD_OK) return relax_status;
        if (ld_elf_add_overflow(rx_cursor, output->file_size, &rx_cursor))
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF read-only image is too large");
    }
    backend->rx_file_size = rx_cursor;
    if (!ld_elf_align(rx_cursor, backend->page_size,
                      &backend->rw_file_offset)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF writable segment layout overflow");
    }
    backend->rw_addr = backend->image_base + backend->rw_file_offset;
    uint64_t file_cursor = backend->rw_file_offset;
    uint64_t addr_cursor = backend->rw_addr;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if ((output->flags & LD_ELF_SHF_ALLOC) == 0U) continue;
        if ((output->flags & LD_ELF_SHF_WRITE) == 0U) continue;
        bool protected = ld_elf_relro_section_is_protected(
                output->name, output->type, output->flags, output->size);
        bool transparent = ld_elf_relro_section_is_transparent(
                output->name, output->type, output->flags, output->size);
        if (!protected && !transparent && output->size != 0U &&
            backend->relro_plan.present &&
            !backend->relro_plan.finalized) {
            int status = ld_elf_finalize_relro_layout(
                    backend, &file_cursor, &addr_cursor, true);
            if (status != LD_OK) return status;
        } else if (protected && backend->relro_plan.finalized) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF GNU RELRO section '%s' is not in the writable "
                    "segment prefix",
                    output->name);
        }
        uint64_t aligned_addr, aligned_file;
        if (!ld_elf_align(addr_cursor, output->align, &aligned_addr) ||
            !ld_elf_align(file_cursor, output->align, &aligned_file)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF writable section layout overflow");
        }
        output->addr = aligned_addr;
        output->file_offset = aligned_file;
        int relax_status =
                ld_elf_relayout_riscv_output(backend, output);
        if (relax_status != LD_OK) return relax_status;
        if (output == backend->tbss) {
            backend->tls_addr = backend->tdata
                                        ? backend->tdata->addr
                                        : output->addr;
            backend->tls_file_offset = backend->tdata
                                               ? backend->tdata->file_offset
                                               : output->file_offset;
            backend->tls_file_size = backend->tdata
                                             ? backend->tdata->file_size
                                             : 0U;
            uint64_t tls_end;
            if (ld_elf_add_overflow(output->addr, output->size, &tls_end) ||
                tls_end < backend->tls_addr) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "ELF TLS memory layout overflow");
            }
            backend->tls_mem_size = tls_end - backend->tls_addr;
            if (output->align > backend->tls_align)
                backend->tls_align = output->align;
            continue;
        }
        if (output == backend->tdata) {
            backend->tls_addr = output->addr;
            backend->tls_file_offset = output->file_offset;
            backend->tls_file_size = output->file_size;
            backend->tls_mem_size = output->size;
            backend->tls_align = output->align;
        }
        if (output->type == LD_ELF_SHT_NOBITS) {
            if (ld_elf_add_overflow(aligned_addr, output->size, &addr_cursor))
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "ELF BSS layout overflow");
            file_cursor = aligned_file;
        } else {
            if (ld_elf_add_overflow(aligned_addr, output->size, &addr_cursor) ||
                ld_elf_add_overflow(aligned_file, output->file_size,
                                    &file_cursor)) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "ELF writable image is too large");
            }
        }
        if (protected) {
            ld_elf_relro_result_t result = ld_elf_relro_add_section(
                    &backend->relro_plan, output->name, output->type,
                    output->flags, output->file_offset, output->addr,
                    output->size, output->file_size);
            if (result != LD_ELF_RELRO_OK) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "cannot add ELF section '%s' to GNU RELRO: %s",
                        output->name,
                        ld_elf_relro_result_string(result));
            }
        }
    }
    if (backend->relro_plan.present &&
        !backend->relro_plan.finalized) {
        int status = ld_elf_finalize_relro_layout(
                backend, &file_cursor, &addr_cursor, false);
        if (status != LD_OK) return status;
    }
    backend->rw_file_size = file_cursor - backend->rw_file_offset;
    backend->rw_mem_size = addr_cursor - backend->rw_addr;
    backend->image_end = addr_cursor > backend->image_base + backend->rx_file_size
                                 ? addr_cursor
                                 : backend->image_base + backend->rx_file_size;

    uint64_t nonalloc_cursor = backend->rx_file_size;
    uint64_t rw_file_end;
    if (ld_elf_add_overflow(backend->rw_file_offset,
                            backend->rw_file_size, &rw_file_end)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF loaded file size overflows address space");
    }
    if (rw_file_end > nonalloc_cursor) nonalloc_cursor = rw_file_end;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if ((output->flags & LD_ELF_SHF_ALLOC) != 0U) continue;
        if (!ld_elf_align(nonalloc_cursor, output->align,
                          &nonalloc_cursor)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF non-allocated section layout overflow");
        }
        output->addr = 0U;
        output->file_offset = nonalloc_cursor;
        if (output->type != LD_ELF_SHT_NOBITS &&
            ld_elf_add_overflow(nonalloc_cursor, output->file_size,
                                &nonalloc_cursor)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF non-allocated section '%s' is too large",
                               output->name);
        }
    }
    if (has_tls) {
        if (backend->tls_align == 0U) backend->tls_align = 1U;
        if (backend->ctx->options->arch == LD_ARCH_ARM64) {
            if (backend->tls_addr < 16U) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "ELF TLS address underflow");
            }
            backend->tp_addr = (backend->tls_addr - 16U) &
                               ~(backend->tls_align - 1U);
        } else if (backend->ctx->options->arch == LD_ARCH_RISCV64) {
            /* musl's riscv64 ABI uses TLS_ABOVE_TP with GAP_ABOVE_TP=0. */
            backend->tp_addr = backend->tls_addr;
        } else {
            uint64_t tls_end;
            if (ld_elf_add_overflow(backend->tls_addr,
                                    backend->tls_mem_size, &tls_end) ||
                !ld_elf_align(tls_end, backend->tls_align,
                              &backend->tp_addr)) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "ELF TLS layout overflow");
            }
        }
    }
    return LD_OK;
}

static int ld_elf_copy_eh_frame(ld_elf_backend_t *backend,
                                ld_elf_object_t *object,
                                ld_elf_section_t *section,
                                ld_elf_input_placement_t *placement) {
    ld_elf_output_section_t *output = section->output;
    for (size_t i = 0; i < section->eh_record_count; i++) {
        const ld_elf_eh_record_t *record = &section->eh_records[i];
        if (!record->alive) continue;
        uint64_t source_end;
        uint64_t record_output_end;
        uint64_t destination_offset;
        if (ld_elf_add_overflow(record->input_offset, record->size,
                                &source_end) ||
            source_end > section->data_size ||
            ld_elf_add_overflow(record->output_offset, record->size,
                                &record_output_end) ||
            record_output_end > placement->emitted_size ||
            ld_elf_add_overflow(section->output_offset,
                                record->output_offset,
                                &destination_offset) ||
            destination_offset > output->file_size ||
            record->size > output->file_size - destination_offset) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    ".eh_frame record at offset 0x%llx from '%s' does not fit "
                    "the output section",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }
        uint8_t *destination = output->data + destination_offset;
        memcpy(destination, section->data + record->input_offset,
               (size_t) record->size);
        if (record->cie) continue;

        if (record->cie_record_index >= section->eh_record_count) {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx in '%s' has an invalid CIE",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }
        const ld_elf_eh_record_t *input_cie =
                &section->eh_records[record->cie_record_index];
        ld_elf_section_t *cie_section =
                input_cie->canonical_cie_section;
        if (!input_cie->cie || !cie_section ||
            input_cie->canonical_cie_record_index >=
                    cie_section->eh_record_count) {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx in '%s' cannot reference "
                    "its output CIE",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }
        const ld_elf_eh_record_t *cie =
                &cie_section->eh_records
                         [input_cie->canonical_cie_record_index];
        uint64_t fde_output;
        uint64_t pointer_output;
        uint64_t cie_output;
        if (!cie->cie || !cie->alive ||
            cie->offset_size != record->offset_size ||
            ld_elf_add_overflow(section->output_offset,
                                record->output_offset, &fde_output) ||
            ld_elf_add_overflow(fde_output, record->length_field_size,
                                &pointer_output) ||
            ld_elf_add_overflow(cie_section->output_offset,
                                cie->output_offset, &cie_output) ||
            cie_output > pointer_output) {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    ".eh_frame FDE at offset 0x%llx in '%s' cannot reference "
                    "its canonical output CIE",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }
        uint64_t delta = pointer_output - cie_output;
        if (record->offset_size != 4U) {
            return ld_elf_fail(backend->ctx, LD_INVALID_INPUT,
                               "invalid .eh_frame offset width in '%s'",
                               object->display_name);
        }
        if (delta > UINT32_MAX) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    ".eh_frame CIE pointer for FDE at offset 0x%llx in '%s' "
                    "exceeds 32 bits",
                    (unsigned long long) record->input_offset,
                    object->display_name);
        }
        ld_elf_write_u32(destination + record->length_field_size,
                         (uint32_t) delta);
    }
    return LD_OK;
}

static int ld_elf_allocate_output_data(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if (output->file_size == 0U) continue;
        if (output->file_size > SIZE_MAX) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF output section '%s' is too large",
                               output->name);
        }
        output->data = calloc(1, (size_t) output->file_size);
        if (!output->data) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "out of memory allocating ELF output section '%s'",
                               output->name);
        }
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output || section->nobits) {
                continue;
            }
            ld_elf_input_placement_t *placement =
                    ld_elf_placement_find(backend, section);
            if (!placement) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "missing ELF placement for section '%s' "
                                   "in '%s'",
                                   section->name, object->display_name);
            }
            uint64_t copy_size = placement->emitted_size;
            ld_elf_output_section_t *output = section->output;
            if (ld_elf_is_eh_frame_section(section)) {
                int status = ld_elf_copy_eh_frame(backend, object, section,
                                                  placement);
                if (status != LD_OK) return status;
                continue;
            }
            if (ld_elf_riscv_relax_plan_active(
                        &section->riscv_relax_plan)) {
                if (!section->data ||
                    section->output_offset > output->file_size ||
                    copy_size > output->file_size -
                                        section->output_offset ||
                    copy_size > SIZE_MAX) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "RISC-V relaxed section '%s' from '%s' does not "
                            "fit its output section",
                            section->name, object->display_name);
                }
                ld_elf_riscv_relax_result_t result =
                        ld_elf_riscv_relax_emit(
                                &section->riscv_relax_plan,
                                output->data + section->output_offset,
                                (size_t) copy_size, section->data,
                                section->data_size);
                if (result != LD_ELF_RISCV_RELAX_OK) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "cannot emit relaxed RISC-V section '%s' from "
                            "'%s': %s",
                            section->name, object->display_name,
                            ld_elf_riscv_relax_result_string(result));
                }
                continue;
            }
            if (copy_size == 0U) continue;
            if (!section->data ||
                section->output_offset > output->file_size ||
                copy_size > output->file_size - section->output_offset ||
                copy_size > section->data_size) {
                return ld_elf_fail(backend->ctx, LD_INVALID_INPUT,
                                   "ELF section '%s' from '%s' does not fit its "
                                   "output section",
                                   section->name, object->display_name);
            }
            memcpy(output->data + section->output_offset, section->data,
                   (size_t) copy_size);
        }
    }
    if (backend->gnu_property) {
        if (!backend->gnu_property->data ||
            backend->gnu_property->file_size !=
                    backend->property_plan.output_size) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF .note.gnu.property output allocation is inconsistent");
        }
        memcpy(backend->gnu_property->data,
               backend->property_plan.output,
               backend->property_plan.output_size);
    }
    return ld_elf_merge_emit(&backend->merge_plan);
}

static uint64_t ld_elf_output_start(ld_elf_output_section_t *output) {
    return output ? output->addr : 0U;
}

static uint64_t ld_elf_output_end(ld_elf_output_section_t *output) {
    return output ? output->addr + output->size : 0U;
}

static int ld_elf_symbol_value(ld_elf_backend_t *backend,
                               ld_elf_object_t *object,
                               ld_elf_symbol_t *symbol, uint64_t *result);

static int ld_elf_global_value(ld_elf_backend_t *backend,
                               ld_elf_global_t *global, uint64_t *result) {
    if (global->definition) {
        return ld_elf_symbol_value(backend, global->object,
                                   global->definition, result);
    }
    if (global->common) {
        ld_elf_output_section_t *output =
                global->common_tls ? backend->tbss : backend->bss;
        if (!output) {
            return ld_elf_fail(
                    backend->ctx, LD_SYMBOL_ERROR,
                    "common symbol '%s' has no %s allocation", global->name,
                    global->common_tls ? "TLS BSS" : "BSS");
        }
        *result = output->addr + global->common_offset;
        return LD_OK;
    }
    switch (global->synthetic) {
        case LD_ELF_SYNTHETIC_NONE:
        case LD_ELF_SYNTHETIC_ZERO:
            *result = 0U;
            return LD_OK;
        case LD_ELF_SYNTHETIC_DYNAMIC:
            if (!backend->dynamic) {
                return ld_elf_fail(
                        backend->ctx, LD_SYMBOL_ERROR,
                        "linker-defined symbol '_DYNAMIC' is unavailable "
                        "without static PIE dynamic metadata");
            }
            *result = ld_elf_output_start(backend->dynamic);
            return LD_OK;
        case LD_ELF_SYNTHETIC_IMAGE_START:
        case LD_ELF_SYNTHETIC_DSO_HANDLE:
            *result = backend->image_base;
            return LD_OK;
        case LD_ELF_SYNTHETIC_INIT_ARRAY_START:
            *result = ld_elf_output_start(backend->init_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_INIT_ARRAY_END:
            *result = ld_elf_output_end(backend->init_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_FINI_ARRAY_START:
            *result = ld_elf_output_start(backend->fini_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_FINI_ARRAY_END:
            *result = ld_elf_output_end(backend->fini_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_PREINIT_ARRAY_START:
            *result = ld_elf_output_start(backend->preinit_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_PREINIT_ARRAY_END:
            *result = ld_elf_output_end(backend->preinit_array);
            return LD_OK;
        case LD_ELF_SYNTHETIC_GOT:
            *result = ld_elf_output_start(backend->got);
            return LD_OK;
        case LD_ELF_SYNTHETIC_BSS_START:
            *result = ld_elf_output_start(backend->bss);
            return LD_OK;
        case LD_ELF_SYNTHETIC_END:
            *result = backend->image_end;
            return LD_OK;
        case LD_ELF_SYNTHETIC_TEXT_END:
            *result = ld_elf_output_end(backend->eh_frame
                                                ? backend->eh_frame
                                                : (backend->rodata
                                                           ? backend->rodata
                                                           : backend->text));
            return LD_OK;
        case LD_ELF_SYNTHETIC_DATA_END:
            *result = ld_elf_output_end(backend->data
                                                ? backend->data
                                                : backend->got);
            return LD_OK;
        case LD_ELF_SYNTHETIC_EH_FRAME_START:
            *result = ld_elf_output_start(backend->eh_frame);
            return LD_OK;
        case LD_ELF_SYNTHETIC_EH_FRAME_HDR:
            if (!backend->eh_frame_hdr) {
                return ld_elf_fail(
                        backend->ctx, LD_SYMBOL_ERROR,
                        "linker-defined symbol '__GNU_EH_FRAME_HDR' is "
                        "unavailable without an ELF .eh_frame section");
            }
            *result = ld_elf_output_start(backend->eh_frame_hdr);
            return LD_OK;
        case LD_ELF_SYNTHETIC_RELA_IPLT_START:
            if (backend->ctx->options->pie) {
                *result = 0U;
                return LD_OK;
            }
            *result = ld_elf_output_start(backend->rela_dyn) +
                      (uint64_t) backend->relative_relocation_count *
                              LD_ELF64_RELA_SIZE;
            return LD_OK;
        case LD_ELF_SYNTHETIC_RELA_IPLT_END:
            if (backend->ctx->options->pie) {
                *result = 0U;
                return LD_OK;
            }
            *result = ld_elf_output_end(backend->rela_dyn);
            return LD_OK;
        case LD_ELF_SYNTHETIC_GLOBAL_POINTER:
            *result = backend->data ? backend->data->addr + 0x800U : 0U;
            return LD_OK;
        case LD_ELF_SYNTHETIC_TLS_MODULE_BASE:
            *result = backend->tls_addr;
            return LD_OK;
        case LD_ELF_SYNTHETIC_SECTION_START:
            if (!global->synthetic_output) {
                return ld_elf_fail(
                        backend->ctx, LD_SYMBOL_ERROR,
                        "ELF linker-defined start symbol '%s' has no output "
                        "section",
                        global->name);
            }
            *result = ld_elf_output_start(global->synthetic_output);
            return LD_OK;
        case LD_ELF_SYNTHETIC_SECTION_STOP:
            if (!global->synthetic_output) {
                return ld_elf_fail(
                        backend->ctx, LD_SYMBOL_ERROR,
                        "ELF linker-defined stop symbol '%s' has no output "
                        "section",
                        global->name);
            }
            *result = ld_elf_output_end(global->synthetic_output);
            return LD_OK;
    }
    return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                       "internal error resolving synthetic ELF symbol '%s'",
                       global->name);
}

static int ld_elf_symbol_value(ld_elf_backend_t *backend,
                               ld_elf_object_t *object,
                               ld_elf_symbol_t *symbol, uint64_t *result) {
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
        symbol->resolved && symbol->resolved->definition != symbol) {
        return ld_elf_global_value(backend, symbol->resolved, result);
    }
    if (symbol->entry.st_shndx == LD_ELF_SHN_UNDEF) {
        if (symbol->resolved)
            return ld_elf_global_value(backend, symbol->resolved, result);
        *result = 0U;
        return LD_OK;
    }
    if (symbol->entry.st_shndx == LD_ELF_SHN_ABS) {
        *result = symbol->entry.st_value;
        return LD_OK;
    }
    if (symbol->entry.st_shndx == LD_ELF_SHN_COMMON) {
        if (symbol->resolved)
            return ld_elf_global_value(backend, symbol->resolved, result);
        ld_elf_output_section_t *output = symbol->type == LD_ELF_STT_TLS
                                                  ? backend->tbss
                                                  : backend->bss;
        if (!output) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "local common symbol '%s' in '%s' has no "
                               "allocation",
                               symbol->name, object->display_name);
        }
        *result = output->addr + symbol->common_offset;
        return LD_OK;
    }
    if (!symbol->section || !symbol->section->output) {
        return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                           "symbol '%s' in '%s' refers to discarded section",
                           symbol->name, object->display_name);
    }
    ld_elf_input_placement_t *placement =
            ld_elf_placement_find(backend, symbol->section);
    if (!placement) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing ELF placement for symbol '%s' in '%s'",
                           symbol->name, object->display_name);
    }
    uint64_t mapped_value = symbol->entry.st_value;
    if (symbol->section->merge_input) {
        bool live = true;
        if (!ld_elf_merge_map_input(symbol->section,
                                    symbol->entry.st_value, 0U,
                                    &mapped_value, &live, NULL) ||
            !live) {
            return ld_elf_fail(
                    backend->ctx, LD_SYMBOL_ERROR,
                    "symbol '%s' value 0x%llx in '%s' is outside an ELF "
                    "merge-section entity",
                    symbol->name,
                    (unsigned long long) symbol->entry.st_value,
                    object->display_name);
        }
    } else if (ld_elf_riscv_relax_plan_active(
                       &symbol->section->riscv_relax_plan)) {
        bool live = true;
        if (!ld_elf_riscv_relax_map(
                    &symbol->section->riscv_relax_plan,
                    symbol->entry.st_value, 0U, &mapped_value, &live,
                    NULL) ||
            !live) {
            return ld_elf_fail(
                    backend->ctx, LD_SYMBOL_ERROR,
                    "symbol '%s' value 0x%llx in '%s' refers to removed "
                    "RISC-V alignment padding",
                    symbol->name,
                    (unsigned long long) symbol->entry.st_value,
                    object->display_name);
        }
    } else if (ld_elf_is_eh_frame_section(symbol->section)) {
        bool live = true;
        if (symbol->type == LD_ELF_STT_SECTION &&
            symbol->entry.st_value == 0U) {
            mapped_value = 0U;
        } else if (!ld_elf_map_input_offset(
                           symbol->section, symbol->entry.st_value, 0U,
                           &mapped_value, &live, NULL) ||
                   !live) {
            return ld_elf_fail(
                    backend->ctx, LD_SYMBOL_ERROR,
                    "symbol '%s' value 0x%llx in '%s' refers to a removed "
                    ".eh_frame record",
                    symbol->name,
                    (unsigned long long) symbol->entry.st_value,
                    object->display_name);
        }
    } else if (placement->emitted_size < symbol->section->header.sh_size &&
               mapped_value > placement->emitted_size) {
        if (mapped_value == symbol->section->header.sh_size &&
            placement->emitted_size < mapped_value) {
            mapped_value = placement->emitted_size;
        } else {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    "symbol '%s' value 0x%llx refers to removed bytes in "
                    "section '%s' in '%s'",
                    symbol->name,
                    (unsigned long long) symbol->entry.st_value,
                    symbol->section->name, object->display_name);
        }
    }
    bool address_anchor = symbol->entry.st_size == 0U &&
                          (symbol->type == LD_ELF_STT_NOTYPE ||
                           symbol->type == LD_ELF_STT_SECTION);
    if (symbol->entry.st_value > symbol->section->header.sh_size &&
        !address_anchor) {
        return ld_elf_fail(backend->ctx, LD_INVALID_INPUT,
                           "symbol '%s' value 0x%llx exceeds section '%s' in '%s'",
                           symbol->name,
                           (unsigned long long) symbol->entry.st_value,
                           symbol->section->name, object->display_name);
    }
    uint64_t value = symbol->section->output->addr;
    if (ld_elf_add_overflow(value, symbol->section->output_offset, &value) ||
        ld_elf_add_overflow(value, mapped_value, &value)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "symbol address overflow for '%s' in '%s'",
                           symbol->name, object->display_name);
    }
    *result = value;
    return LD_OK;
}

static int ld_elf_pltgot_address(ld_elf_backend_t *backend,
                                 uint32_t index, const char *name,
                                 uint64_t *result) {
    if (!backend->plt_got || index == LD_ELF_NO_GOT ||
        index >= backend->pltgot_count) {
        return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                           "missing PLT-GOT entry for ELF IFUNC '%s'",
                           name);
    }
    uint64_t offset = (uint64_t) index * LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE;
    if (ld_elf_add_overflow(backend->plt_got->addr, offset, result)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "PLT-GOT address overflows for ELF IFUNC '%s'",
                           name);
    }
    return LD_OK;
}

static int ld_elf_relocation_symbol_value(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_symbol_t *symbol, uint64_t *result) {
    ld_elf_symbol_t *definition = ld_elf_ifunc_definition(symbol);
    if (!definition) return ld_elf_symbol_value(backend, object, symbol, result);

    uint32_t index = definition->pltgot_index;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "unresolved ELF IFUNC symbol '%s' in '%s'",
                               symbol->name, object->display_name);
        }
        index = symbol->resolved->pltgot_index;
    }
    return ld_elf_pltgot_address(backend, index, symbol->name, result);
}

static int ld_elf_relocation_global_value(ld_elf_backend_t *backend,
                                          ld_elf_global_t *global,
                                          uint64_t *result) {
    if (global && global->definition &&
        global->definition->type == LD_ELF_STT_GNU_IFUNC &&
        global->pltgot_index != LD_ELF_NO_GOT) {
        return ld_elf_pltgot_address(backend, global->pltgot_index,
                                     global->name, result);
    }
    return ld_elf_global_value(backend, global, result);
}

static uint64_t ld_elf_symbol_output_size(const ld_elf_symbol_t *symbol) {
    if (!symbol) return 0U;
    uint64_t size = symbol->entry.st_size;
    if (size == 0U || !symbol->section ||
        !ld_elf_riscv_relax_plan_active(
                &symbol->section->riscv_relax_plan) ||
        symbol->entry.st_value > UINT64_MAX - size) {
        return size;
    }
    uint64_t input_end = symbol->entry.st_value + size;
    uint64_t output_start;
    uint64_t output_end;
    bool start_alive = true;
    bool end_alive = true;
    if (!ld_elf_riscv_relax_map(
                &symbol->section->riscv_relax_plan,
                symbol->entry.st_value, 0U, &output_start, &start_alive,
                NULL) ||
        !ld_elf_riscv_relax_map(
                &symbol->section->riscv_relax_plan, input_end, 0U,
                &output_end, &end_alive, NULL) ||
        !start_alive || !end_alive || output_end < output_start) {
        return size;
    }
    return output_end - output_start;
}

static uint64_t ld_elf_global_size(const ld_elf_global_t *global) {
    if (!global) return 0U;
    if (global->definition)
        return ld_elf_symbol_output_size(global->definition);
    if (global->common) return global->common_size;
    return 0U;
}

static uint64_t ld_elf_symbol_size(const ld_elf_symbol_t *symbol) {
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
        symbol->resolved) {
        return ld_elf_global_size(symbol->resolved);
    }
    return ld_elf_symbol_output_size(symbol);
}

static int ld_elf_aarch64_branch_addresses(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section, ld_elf_relocation_t *relocation,
        ld_elf_input_placement_t **placement_result, uint64_t *place_result,
        uint64_t *target_result) {
    if (relocation->symbol_index >= object->symbol_count) {
        return ld_elf_fail(
                backend->ctx, LD_INVALID_INPUT,
                "AArch64 branch relocation at offset 0x%llx in section '%s' "
                "in '%s' uses invalid symbol index %u",
                (unsigned long long) relocation->offset, section->name,
                object->display_name, relocation->symbol_index);
    }
    ld_elf_input_placement_t *placement =
            ld_elf_placement_find(backend, section);
    if (!placement) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "missing AArch64 text placement for branch at offset 0x%llx "
                "in section '%s' in '%s'",
                (unsigned long long) relocation->offset, section->name,
                object->display_name);
    }
    uint64_t mapped_offset;
    bool live = true;
    if (!ld_elf_map_input_offset(section, relocation->offset, 4U,
                                 &mapped_offset, &live, NULL) ||
        !live) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "AArch64 branch relocation offset 0x%llx is outside live "
                "section '%s' in '%s'",
                (unsigned long long) relocation->offset, section->name,
                object->display_name);
    }
    uint64_t output_offset;
    uint64_t place;
    if (ld_elf_add_overflow(section->output_offset, mapped_offset,
                            &output_offset) ||
        ld_elf_add_overflow(section->output->addr, output_offset, &place)) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "AArch64 branch place address overflows at offset 0x%llx in "
                "section '%s' in '%s'",
                (unsigned long long) relocation->offset, section->name,
                object->display_name);
    }

    ld_elf_symbol_t *symbol = &object->symbols[relocation->symbol_index];
    uint64_t symbol_address;
    int status = ld_elf_relocation_symbol_value(
            backend, object, symbol, &symbol_address);
    if (status != LD_OK) return status;
    uint64_t target;
    if (!ld_elf_add_signed(symbol_address, relocation->addend, &target)) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "%s target S + A overflows for symbol '%s' at offset "
                "0x%llx in section '%s' in '%s'",
                ld_elf_relocation_name(LD_ARCH_ARM64, relocation->type),
                symbol->name, (unsigned long long) relocation->offset,
                section->name, object->display_name);
    }
    if (((place | target) & 3U) != 0U) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "%s target/place is not 4-byte aligned for symbol '%s' at "
                "offset 0x%llx in section '%s' in '%s'",
                ld_elf_relocation_name(LD_ARCH_ARM64, relocation->type),
                symbol->name, (unsigned long long) relocation->offset,
                section->name, object->display_name);
    }
    *placement_result = placement;
    *place_result = place;
    *target_result = target;
    return LD_OK;
}

static ld_elf_aarch64_thunk_key_t ld_elf_aarch64_thunk_key(
        ld_elf_object_t *object, ld_elf_relocation_t *relocation) {
    ld_elf_symbol_t *symbol = &object->symbols[relocation->symbol_index];
    ld_elf_aarch64_thunk_key_t key = {
            .object = object,
            .symbol_index = relocation->symbol_index,
            .addend = relocation->addend,
    };
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
        symbol->resolved) {
        key.global = symbol->resolved;
        key.object = NULL;
        key.symbol_index = 0U;
    }
    return key;
}

static int ld_elf_aarch64_thunk_address(
        ld_elf_backend_t *backend, uint32_t group_index,
        uint32_t entry_index, uint64_t *result) {
    if (!backend->text ||
        group_index >= backend->aarch64_thunk_plan.group_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing AArch64 branch thunk group");
    }
    ld_elf_aarch64_thunk_group_t *group =
            &backend->aarch64_thunk_plan.groups[group_index];
    if (entry_index >= group->entry_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing AArch64 branch thunk entry");
    }
    uint64_t entry_offset =
            (uint64_t) entry_index * LD_ELF_AARCH64_THUNK_SIZE;
    uint64_t output_offset;
    if (ld_elf_add_overflow(group->thunk_output_offset, entry_offset,
                            &output_offset) ||
        ld_elf_add_overflow(backend->text->addr, output_offset, result)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "AArch64 branch thunk address overflows");
    }
    return LD_OK;
}

static int ld_elf_validate_aarch64_thunks(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->aarch64_thunk_plan.group_count; i++) {
        ld_elf_aarch64_thunk_group_t *group =
                &backend->aarch64_thunk_plan.groups[i];
        for (size_t j = 0; j < group->entry_count; j++)
            group->entries[j].used = false;
    }

    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output) continue;
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                if (!ld_elf_is_aarch64_branch_relocation(relocation->type) ||
                    !ld_elf_relocation_is_live(section, relocation)) {
                    continue;
                }
                ld_elf_input_placement_t *placement;
                uint64_t place, target;
                int status = ld_elf_aarch64_branch_addresses(
                        backend, object, section, relocation, &placement,
                        &place, &target);
                if (status != LD_OK) return status;
                if (ld_elf_aarch64_branch26_fits(target, place, NULL))
                    continue;
                if (placement->aarch64_thunk_group_index >=
                            backend->aarch64_thunk_plan.group_count ||
                    relocation->aarch64_thunk_entry_index ==
                            LD_ELF_AARCH64_NO_THUNK) {
                    ld_elf_symbol_t *symbol =
                            &object->symbols[relocation->symbol_index];
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "missing AArch64 range-extension thunk for "
                            "symbol '%s' at offset 0x%llx in section '%s' "
                            "in '%s'",
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                ld_elf_aarch64_thunk_group_t *group =
                        &backend->aarch64_thunk_plan.groups
                                 [placement->aarch64_thunk_group_index];
                if (relocation->aarch64_thunk_entry_index >=
                    group->entry_count) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "invalid AArch64 range-extension thunk index");
                }
                uint64_t thunk_address;
                status = ld_elf_aarch64_thunk_address(
                        backend, placement->aarch64_thunk_group_index,
                        relocation->aarch64_thunk_entry_index,
                        &thunk_address);
                if (status != LD_OK) return status;
                if (!ld_elf_aarch64_branch26_fits(thunk_address, place,
                                                  NULL)) {
                    ld_elf_symbol_t *symbol =
                            &object->symbols[relocation->symbol_index];
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "AArch64 range-extension thunk for symbol '%s' "
                            "is not reachable from offset 0x%llx in section "
                            "'%s' in '%s'",
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                group->entries[relocation->aarch64_thunk_entry_index].used =
                        true;
            }
        }
    }
    return LD_OK;
}

static int ld_elf_plan_aarch64_thunks(ld_elf_backend_t *backend) {
    int status = ld_elf_build_aarch64_thunk_groups(backend);
    if (status != LD_OK) return status;

    if (backend->aarch64_branch_relocation_count == SIZE_MAX) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many AArch64 branch relocations to plan");
    }
    size_t iteration_limit =
            backend->aarch64_branch_relocation_count + 1U;
    for (size_t iteration = 0; iteration < iteration_limit; iteration++) {
        status = ld_elf_relayout_aarch64_text(backend);
        if (status != LD_OK) return status;
        status = ld_elf_layout_outputs(backend);
        if (status != LD_OK) return status;

        bool added_any = false;
        for (size_t i = 0; i < backend->ctx->objects.count; i++) {
            ld_elf_object_t *object = backend->ctx->objects.items[i];
            if (!object->selected) continue;
            for (size_t j = 1; j < object->section_count; j++) {
                ld_elf_section_t *section = &object->sections[j];
                if (!section->output) continue;
                for (size_t k = 0; k < section->relocation_count; k++) {
                    ld_elf_relocation_t *relocation =
                            &section->relocations[k];
                    if (!ld_elf_is_aarch64_branch_relocation(
                                relocation->type) ||
                        !ld_elf_relocation_is_live(section, relocation)) {
                        continue;
                    }
                    ld_elf_input_placement_t *placement;
                    uint64_t place, target;
                    status = ld_elf_aarch64_branch_addresses(
                            backend, object, section, relocation, &placement,
                            &place, &target);
                    if (status != LD_OK) return status;
                    if (ld_elf_aarch64_branch26_fits(target, place, NULL))
                        continue;
                    if (placement->aarch64_thunk_group_index >=
                        backend->aarch64_thunk_plan.group_count) {
                        ld_elf_symbol_t *symbol =
                                &object->symbols[relocation->symbol_index];
                        return ld_elf_fail(
                                backend->ctx, LD_UNSUPPORTED,
                                "AArch64 branch to symbol '%s' at offset "
                                "0x%llx in non-text section '%s' in '%s' is "
                                "out of range",
                                symbol->name,
                                (unsigned long long) relocation->offset,
                                section->name, object->display_name);
                    }
                    ld_elf_aarch64_thunk_group_t *group =
                            &backend->aarch64_thunk_plan.groups
                                     [placement
                                              ->aarch64_thunk_group_index];
                    ld_elf_aarch64_thunk_key_t key =
                            ld_elf_aarch64_thunk_key(object, relocation);
                    uint32_t entry_index;
                    bool added;
                    if (!ld_elf_aarch64_thunk_entry_find_or_add(
                                group, &key, &entry_index, &added)) {
                        return ld_elf_fail(
                                backend->ctx, LD_IO_ERROR,
                                "out of memory allocating AArch64 branch "
                                "thunk entry");
                    }
                    relocation->aarch64_thunk_entry_index = entry_index;
                    added_any |= added;
                }
            }
        }
        if (!added_any) return ld_elf_validate_aarch64_thunks(backend);
    }
    return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                       "AArch64 branch thunk planning did not converge");
}

static int ld_elf_aarch64_thunk_target(
        ld_elf_backend_t *backend,
        const ld_elf_aarch64_thunk_entry_t *entry, uint64_t *target,
        const char **symbol_name) {
    uint64_t symbol_address;
    int status;
    if (entry->key.global) {
        *symbol_name = entry->key.global->name;
        status = ld_elf_relocation_global_value(
                backend, entry->key.global, &symbol_address);
    } else {
        if (!entry->key.object ||
            entry->key.symbol_index >= entry->key.object->symbol_count) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "invalid local symbol in AArch64 thunk");
        }
        ld_elf_symbol_t *symbol =
                &entry->key.object->symbols[entry->key.symbol_index];
        *symbol_name = symbol->name;
        status = ld_elf_relocation_symbol_value(
                backend, entry->key.object, symbol, &symbol_address);
    }
    if (status != LD_OK) return status;
    if (!ld_elf_add_signed(symbol_address, entry->key.addend, target)) {
        return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                           "AArch64 thunk target S + A overflows for symbol "
                           "'%s'",
                           *symbol_name);
    }
    return LD_OK;
}

static int ld_elf_emit_aarch64_thunks(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_ARM64 ||
        backend->aarch64_thunk_plan.group_count == 0U) {
        return LD_OK;
    }
    if (!backend->text || (backend->text->file_size != 0U &&
                           !backend->text->data)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing AArch64 text data for branch thunks");
    }
    for (size_t i = 0; i < backend->aarch64_thunk_plan.group_count; i++) {
        ld_elf_aarch64_thunk_group_t *group =
                &backend->aarch64_thunk_plan.groups[i];
        for (size_t j = 0; j < group->entry_count; j++) {
            uint64_t entry_offset =
                    (uint64_t) j * LD_ELF_AARCH64_THUNK_SIZE;
            uint64_t output_offset;
            if (ld_elf_add_overflow(group->thunk_output_offset, entry_offset,
                                    &output_offset) ||
                output_offset > backend->text->file_size ||
                LD_ELF_AARCH64_THUNK_SIZE >
                        backend->text->file_size - output_offset) {
                return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                   "AArch64 thunk does not fit .text");
            }
            uint8_t *output = backend->text->data + output_offset;
            ld_elf_aarch64_thunk_entry_t *entry = &group->entries[j];
            if (!entry->used) {
                ld_elf_write_u32(output, 0xd503201fU);
                ld_elf_write_u32(output + 4U, 0xd503201fU);
                ld_elf_write_u32(output + 8U, 0xd503201fU);
                continue;
            }
            uint64_t thunk_address;
            int status = ld_elf_aarch64_thunk_address(
                    backend, (uint32_t) i, (uint32_t) j, &thunk_address);
            if (status != LD_OK) return status;
            uint64_t target;
            const char *symbol_name;
            status = ld_elf_aarch64_thunk_target(
                    backend, entry, &target, &symbol_name);
            if (status != LD_OK) return status;
            ld_elf_aarch64_thunk_encode_result_t encode =
                    ld_elf_aarch64_thunk_encode(output, thunk_address,
                                                target);
            if (encode != LD_ELF_AARCH64_THUNK_ENCODE_OK) {
                return ld_elf_fail(
                        backend->ctx, LD_RELOCATION_ERROR,
                        "cannot encode AArch64 range-extension thunk for "
                        "symbol '%s': %s",
                        symbol_name,
                        encode == LD_ELF_AARCH64_THUNK_ENCODE_UNALIGNED
                                ? "target or thunk is not 4-byte aligned"
                                : "ADRP target is outside the signed 21-page "
                                  "range");
            }
        }
    }
    return LD_OK;
}

static int ld_elf_got_address(ld_elf_backend_t *backend,
                              ld_elf_object_t *object,
                              ld_elf_symbol_t *symbol, uint64_t *result) {
    uint32_t index = symbol->got_index;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "unresolved GOT symbol '%s' in '%s'",
                               symbol->name, object->display_name);
        }
        index = symbol->resolved->got_index;
    }
    if (!backend->got || index == LD_ELF_NO_GOT ||
        index >= backend->got_count) {
        return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                           "missing GOT entry for symbol '%s' in '%s'",
                           symbol->name, object->display_name);
    }
    *result = backend->got->addr + (uint64_t) index * 8U;
    return LD_OK;
}

static int ld_elf_gottp_address(ld_elf_backend_t *backend,
                                ld_elf_object_t *object,
                                ld_elf_symbol_t *symbol, uint64_t *result) {
    uint32_t index = symbol->gottp_index;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "unresolved GOTTP symbol '%s' in '%s'",
                               symbol->name, object->display_name);
        }
        index = symbol->resolved->gottp_index;
    }
    if (!backend->got || index == LD_ELF_NO_GOT ||
        index >= backend->gottp_count) {
        return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                           "missing GOTTP entry for symbol '%s' in '%s'",
                           symbol->name, object->display_name);
    }
    *result = backend->got->addr +
              ((uint64_t) backend->got_count + (uint64_t) index) * 8U;
    return LD_OK;
}

static int ld_elf_tlsgd_address(ld_elf_backend_t *backend,
                                ld_elf_object_t *object,
                                ld_elf_symbol_t *symbol, uint64_t *result) {
    uint32_t index = symbol->tlsgd_index;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0]) {
        if (!symbol->resolved) {
            return ld_elf_fail(backend->ctx, LD_SYMBOL_ERROR,
                               "unresolved TLSGD symbol '%s' in '%s'",
                               symbol->name, object->display_name);
        }
        index = symbol->resolved->tlsgd_index;
    }
    if (!backend->got || index == LD_ELF_NO_GOT ||
        index >= backend->tlsgd_count) {
        return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                           "missing TLSGD entry for symbol '%s' in '%s'",
                           symbol->name, object->display_name);
    }
    *result = backend->got->addr +
              ((uint64_t) backend->got_count +
               (uint64_t) backend->gottp_count) *
                      8U +
              (uint64_t) index * 16U;
    return LD_OK;
}

static int ld_elf_fill_got(ld_elf_backend_t *backend) {
    if (!backend->got) return LD_OK;
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (global->got_index == LD_ELF_NO_GOT &&
            global->gottp_index == LD_ELF_NO_GOT &&
            global->tlsgd_index == LD_ELF_NO_GOT) {
            continue;
        }
        uint64_t value;
        int status = ld_elf_global_value(backend, global, &value);
        if (status != LD_OK) return status;
        if (global->got_index != LD_ELF_NO_GOT) {
            ld_elf_write_u64(backend->got->data +
                                     (size_t) global->got_index * 8U,
                             value);
        }
        if (global->gottp_index != LD_ELF_NO_GOT) {
            size_t offset =
                    ((size_t) backend->got_count + global->gottp_index) * 8U;
            ld_elf_write_u64(backend->got->data + offset,
                             value - backend->tp_addr);
        }
        if (global->tlsgd_index != LD_ELF_NO_GOT) {
            size_t offset =
                    ((size_t) backend->got_count + backend->gottp_count) *
                            8U +
                    (size_t) global->tlsgd_index * 16U;
            ld_elf_write_u64(backend->got->data + offset, 1U);
            uint64_t dtp_offset =
                    backend->ctx->options->arch == LD_ARCH_RISCV64
                            ? LD_ELF_RISCV_DTP_OFFSET
                            : 0U;
            ld_elf_write_u64(backend->got->data + offset + 8U,
                             value - backend->tls_addr - dtp_offset);
        }
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 0; j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (ld_elf_symbol_is_global(symbol) ||
                (symbol->got_index == LD_ELF_NO_GOT &&
                 symbol->gottp_index == LD_ELF_NO_GOT &&
                 symbol->tlsgd_index == LD_ELF_NO_GOT)) {
                continue;
            }
            uint64_t value;
            int status = ld_elf_symbol_value(backend, object, symbol, &value);
            if (status != LD_OK) return status;
            if (symbol->got_index != LD_ELF_NO_GOT) {
                ld_elf_write_u64(backend->got->data +
                                         (size_t) symbol->got_index * 8U,
                                 value);
            }
            if (symbol->gottp_index != LD_ELF_NO_GOT) {
                size_t offset =
                        ((size_t) backend->got_count + symbol->gottp_index) *
                        8U;
                ld_elf_write_u64(backend->got->data + offset,
                                 value - backend->tp_addr);
            }
            if (symbol->tlsgd_index != LD_ELF_NO_GOT) {
                size_t offset =
                        ((size_t) backend->got_count +
                         backend->gottp_count) *
                                8U +
                        (size_t) symbol->tlsgd_index * 16U;
                ld_elf_write_u64(backend->got->data + offset, 1U);
                uint64_t dtp_offset =
                        backend->ctx->options->arch == LD_ARCH_RISCV64
                                ? LD_ELF_RISCV_DTP_OFFSET
                                : 0U;
                ld_elf_write_u64(backend->got->data + offset + 8U,
                                 value - backend->tls_addr - dtp_offset);
            }
        }
    }
    return LD_OK;
}

static int ld_elf_ifunc_reference_for_index(
        ld_elf_backend_t *backend, uint32_t index,
        ld_elf_ifunc_reference_t *result) {
    ld_elf_ifunc_reference_t found = {0};
    size_t match_count = 0U;
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (global->pltgot_index != index) continue;
        if (!global->definition ||
            global->definition->type != LD_ELF_STT_GNU_IFUNC) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "PLT-GOT index %u belongs to non-IFUNC symbol '%s'",
                    index, global->name);
        }
        found = (ld_elf_ifunc_reference_t) {
                .global = global,
                .object = global->object,
                .symbol = global->definition,
                .name = global->name,
                .got_index = global->got_index,
                .pltgot_index = global->pltgot_index,
        };
        match_count++;
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->first_global_symbol; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->pltgot_index != index) continue;
            if (symbol->type != LD_ELF_STT_GNU_IFUNC) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "PLT-GOT index %u belongs to non-IFUNC local symbol "
                        "'%s' in '%s'",
                        index, symbol->name, object->display_name);
            }
            found = (ld_elf_ifunc_reference_t) {
                    .object = object,
                    .symbol = symbol,
                    .name = symbol->name,
                    .got_index = symbol->got_index,
                    .pltgot_index = symbol->pltgot_index,
            };
            match_count++;
        }
    }
    if (match_count != 1U) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                match_count == 0U
                        ? "missing ELF IFUNC for PLT-GOT index %u"
                        : "duplicate ELF IFUNC PLT-GOT index %u",
                index);
    }
    *result = found;
    return LD_OK;
}

static int ld_elf_ifunc_resolver_value(
        ld_elf_backend_t *backend,
        const ld_elf_ifunc_reference_t *reference, uint64_t *result) {
    if (reference->global)
        return ld_elf_global_value(backend, reference->global, result);
    return ld_elf_symbol_value(backend, reference->object,
                               reference->symbol, result);
}

static int ld_elf_ifunc_got_address(
        ld_elf_backend_t *backend,
        const ld_elf_ifunc_reference_t *reference, uint64_t *result) {
    if (!backend->got || reference->got_index == LD_ELF_NO_GOT ||
        reference->got_index >= backend->got_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing GOT slot for ELF IFUNC '%s'",
                           reference->name);
    }
    uint64_t offset = (uint64_t) reference->got_index * 8U;
    if (ld_elf_add_overflow(backend->got->addr, offset, result)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "GOT address overflows for ELF IFUNC '%s'",
                           reference->name);
    }
    return LD_OK;
}

static int ld_elf_compare_irelative(const void *left, const void *right) {
    const ld_elf_irelative_t *a = left;
    const ld_elf_irelative_t *b = right;
    if (a->target_address != b->target_address)
        return a->target_address < b->target_address ? -1 : 1;
    if (a->resolver_addend != b->resolver_addend)
        return a->resolver_addend < b->resolver_addend ? -1 : 1;
    return 0;
}

static int ld_elf_emit_ifunc(ld_elf_backend_t *backend) {
    if (backend->pltgot_count == 0U) return LD_OK;
    if (!backend->got || !backend->plt_got || !backend->rela_dyn ||
        !backend->got->data || !backend->plt_got->data ||
        !backend->rela_dyn->data) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing ELF IFUNC synthetic section data");
    }

    size_t relocation_count = backend->ifunc_input_relocation_count;
    if (relocation_count > SIZE_MAX - backend->pltgot_count) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF IFUNC relocation count overflows");
    }
    relocation_count += backend->pltgot_count;
    size_t relocation_bytes;
    if (ld_elf_mul_overflow(relocation_count,
                            sizeof(ld_elf_irelative_t),
                            &relocation_bytes)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF IFUNC relocation allocation overflows");
    }
    ld_elf_irelative_t *relocations =
            relocation_bytes ? malloc(relocation_bytes) : NULL;
    if (relocation_bytes && !relocations) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory collecting ELF IFUNC relocations");
    }

    int status = LD_OK;
    size_t relocation_index = 0U;
    for (uint32_t i = 0; i < backend->pltgot_count; i++) {
        ld_elf_ifunc_reference_t reference;
        status = ld_elf_ifunc_reference_for_index(backend, i, &reference);
        if (status != LD_OK) break;
        uint64_t resolver_address, got_address, pltgot_address;
        status = ld_elf_ifunc_resolver_value(
                backend, &reference, &resolver_address);
        if (status != LD_OK) break;
        status = ld_elf_ifunc_got_address(backend, &reference, &got_address);
        if (status != LD_OK) break;
        status = ld_elf_pltgot_address(backend, i, reference.name,
                                       &pltgot_address);
        if (status != LD_OK) break;
        size_t output_offset =
                (size_t) i * LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE;
        if (output_offset > backend->plt_got->file_size ||
            LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE >
                    backend->plt_got->file_size - output_offset) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "PLT-GOT entry for ELF IFUNC '%s' exceeds .plt.got",
                    reference.name);
            break;
        }
        ld_elf_ifunc_result_t encode = ld_elf_ifunc_encode_pltgot(
                backend->ctx->options->arch,
                backend->plt_got->data + output_offset,
                pltgot_address, got_address);
        if (encode != LD_ELF_IFUNC_OK) {
            status = ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "cannot encode PLT-GOT entry for ELF IFUNC '%s': %s",
                    reference.name, ld_elf_ifunc_result_string(encode));
            break;
        }
        if (resolver_address > (uint64_t) INT64_MAX) {
            status = ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "ELF IFUNC resolver address for '%s' exceeds signed "
                    "ELF64 addend range",
                    reference.name);
            break;
        }
        relocations[relocation_index++] = (ld_elf_irelative_t) {
                .target_address = got_address,
                .resolver_addend = (int64_t) resolver_address,
        };
    }

    for (size_t i = 0;
         i < backend->ctx->objects.count && status == LD_OK; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count && status == LD_OK; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output) continue;
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                if (!relocation->ifunc_irelative ||
                    !ld_elf_relocation_is_live(section, relocation)) {
                    continue;
                }
                if (relocation->symbol_index >= object->symbol_count) {
                    status = ld_elf_fail(
                            backend->ctx, LD_INVALID_INPUT,
                            "ELF IFUNC relocation at offset 0x%llx in '%s' "
                            "uses invalid symbol index",
                            (unsigned long long) relocation->offset,
                            object->display_name);
                    break;
                }
                uint64_t mapped_offset, available;
                bool live = true;
                if (!ld_elf_map_input_offset(
                            section, relocation->offset, 8U,
                            &mapped_offset, &live, &available) ||
                    !live || available < 8U) {
                    status = ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "ELF IFUNC relocation offset 0x%llx is outside "
                            "live section '%s' in '%s'",
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                    break;
                }
                uint64_t output_offset, place_address;
                if (ld_elf_add_overflow(section->output_offset,
                                        mapped_offset, &output_offset) ||
                    ld_elf_add_overflow(section->output->addr,
                                        output_offset, &place_address)) {
                    status = ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "ELF IFUNC relocation place overflows at offset "
                            "0x%llx in section '%s' in '%s'",
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                    break;
                }
                ld_elf_symbol_t *symbol =
                        &object->symbols[relocation->symbol_index];
                uint64_t resolver_address;
                status = ld_elf_symbol_value(
                        backend, object, symbol, &resolver_address);
                if (status != LD_OK) break;
                int64_t resolver_addend;
                if (!ld_elf_add_signed_to_i64(
                            resolver_address, relocation->addend,
                            &resolver_addend)) {
                    status = ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "ELF IFUNC resolver S + A overflows for symbol "
                            "'%s' at offset 0x%llx in section '%s' in '%s'",
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                    break;
                }
                if (relocation_index >= relocation_count) {
                    status = ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "ELF IFUNC relocation count changed after scan");
                    break;
                }
                relocations[relocation_index++] = (ld_elf_irelative_t) {
                        .target_address = place_address,
                        .resolver_addend = resolver_addend,
                };
            }
        }
    }

    if (status == LD_OK && relocation_index != relocation_count) {
        status = ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "ELF IFUNC relocation count changed from %zu to %zu",
                relocation_count, relocation_index);
    }
    if (status == LD_OK) {
        qsort(relocations, relocation_count, sizeof(*relocations),
              ld_elf_compare_irelative);
        size_t relative_bytes;
        if (ld_elf_mul_overflow(backend->relative_relocation_count,
                                LD_ELF_IFUNC_RELA_SIZE,
                                &relative_bytes) ||
            relative_bytes > backend->rela_dyn->file_size) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF relative relocation prefix exceeds .rela.dyn");
        }
        for (size_t i = 0; i < relocation_count; i++) {
            if (status != LD_OK) break;
            size_t entry_offset;
            if (ld_elf_mul_overflow(i, LD_ELF_IFUNC_RELA_SIZE,
                                    &entry_offset) ||
                entry_offset > SIZE_MAX - relative_bytes) {
                status = ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "ELF IFUNC relocation offset overflows");
                break;
            }
            entry_offset += relative_bytes;
            ld_elf_ifunc_result_t encode = ld_elf_ifunc_encode_irelative(
                    backend->ctx->options->arch,
                    backend->rela_dyn->data + entry_offset,
                    relocations[i].target_address,
                    relocations[i].resolver_addend);
            if (encode != LD_ELF_IFUNC_OK) {
                status = ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "cannot encode ELF IFUNC IRELATIVE relocation %zu: %s",
                        i, ld_elf_ifunc_result_string(encode));
                break;
            }
        }
    }
    free(relocations);
    return status;
}

static bool ld_elf_riscv_is_pcrel_lo12(uint32_t type) {
    return type == LD_ELF_R_RISCV_PCREL_LO12_I ||
           type == LD_ELF_R_RISCV_PCREL_LO12_S;
}

static bool ld_elf_riscv_is_paired_hi20(uint32_t type) {
    return type == LD_ELF_R_RISCV_PCREL_HI20 ||
           type == LD_ELF_R_RISCV_GOT_HI20 ||
           type == LD_ELF_R_RISCV_TLS_GOT_HI20 ||
           type == LD_ELF_R_RISCV_TLS_GD_HI20;
}

static int ld_elf_resolve_riscv_pair(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section, const ld_elf_relocation_t *relocation,
        ld_elf_symbol_t *lo_symbol, uint64_t lo_symbol_value,
        ld_elf_reloc_values_t *values) {
    if (backend->ctx->options->arch != LD_ARCH_RISCV64 ||
        !ld_elf_riscv_is_pcrel_lo12(relocation->type)) {
        return LD_OK;
    }
    if (lo_symbol->section != section) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "%s in section '%s' in '%s' uses symbol '%s' from a "
                "different section",
                ld_elf_relocation_name(LD_ARCH_RISCV64, relocation->type),
                section->name, object->display_name, lo_symbol->name);
    }

    if (lo_symbol->entry.st_value > section->header.sh_size) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "invalid RISC-V HI20 pair label '%s' for relocation at "
                "offset 0x%llx in section '%s' in '%s'",
                lo_symbol->name, (unsigned long long) relocation->offset,
                section->name, object->display_name);
    }
    uint64_t pair_offset = lo_symbol->entry.st_value;
    const ld_elf_relocation_t *pair = NULL;
    for (size_t i = 0; i < section->relocation_count; i++) {
        const ld_elf_relocation_t *candidate = &section->relocations[i];
        if (candidate->offset != pair_offset ||
            !ld_elf_riscv_is_paired_hi20(candidate->type)) {
            continue;
        }
        if (pair) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "ambiguous RISC-V HI20 pair at offset 0x%llx in "
                    "section '%s' in '%s'",
                    (unsigned long long) pair_offset, section->name,
                    object->display_name);
        }
        pair = candidate;
    }
    if (!pair) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "%s at offset 0x%llx in section '%s' in '%s' cannot find "
                "its HI20 pair at offset 0x%llx",
                ld_elf_relocation_name(LD_ARCH_RISCV64, relocation->type),
                (unsigned long long) relocation->offset, section->name,
                object->display_name, (unsigned long long) pair_offset);
    }
    if (pair->symbol_index >= object->symbol_count) {
        return ld_elf_fail(
                backend->ctx, LD_INVALID_INPUT,
                "RISC-V HI20 pair at offset 0x%llx in section '%s' in '%s' "
                "uses invalid symbol index %u",
                (unsigned long long) pair_offset, section->name,
                object->display_name, pair->symbol_index);
    }

    ld_elf_symbol_t *pair_symbol = &object->symbols[pair->symbol_index];
    uint64_t pair_symbol_value;
    int status = ld_elf_symbol_value(backend, object, pair_symbol,
                                     &pair_symbol_value);
    if (status != LD_OK) return status;
    uint64_t pair_got_value = 0U;
    ld_elf_reloc_got_kind_t pair_kind = ld_elf_relocation_got_kind(
            LD_ARCH_RISCV64, pair->type);
    if (pair_kind == LD_ELF_RELOC_GOT_ORDINARY) {
        status = ld_elf_got_address(backend, object, pair_symbol,
                                    &pair_got_value);
        if (status != LD_OK) return status;
    } else if (pair_kind == LD_ELF_RELOC_GOT_TP) {
        status = ld_elf_gottp_address(backend, object, pair_symbol,
                                      &pair_got_value);
        if (status != LD_OK) return status;
    } else if (pair_kind == LD_ELF_RELOC_GOT_TLSGD) {
        status = ld_elf_tlsgd_address(backend, object, pair_symbol,
                                      &pair_got_value);
        if (status != LD_OK) return status;
    }

    values->paired_hi.present = true;
    values->paired_hi.type = pair->type;
    values->paired_hi.place_address = lo_symbol_value;
    values->paired_hi.symbol_address = pair_symbol_value;
    values->paired_hi.got_entry_address = pair_got_value;
    values->paired_hi.addend = pair->addend;
    values->paired_hi.symbol_name = pair_symbol->name;
    return LD_OK;
}

static int ld_elf_adjust_eh_frame_section_addend(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        const ld_elf_relocation_t *relocation, ld_elf_symbol_t *symbol,
        uint64_t *symbol_value) {
    if (symbol->type != LD_ELF_STT_SECTION || !symbol->section ||
        !ld_elf_is_eh_frame_section(symbol->section)) {
        return LD_OK;
    }

    uint64_t original_target = symbol->entry.st_value;
    uint64_t magnitude;
    if (relocation->addend >= 0) {
        magnitude = (uint64_t) relocation->addend;
        if (original_target > UINT64_MAX - magnitude) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    ".eh_frame section-symbol addend overflows at relocation "
                    "offset 0x%llx in '%s'",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
        original_target += magnitude;
    } else {
        magnitude = (uint64_t) (-(relocation->addend + 1)) + 1U;
        if (magnitude > original_target) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    ".eh_frame section-symbol addend underflows at relocation "
                    "offset 0x%llx in '%s'",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
        original_target -= magnitude;
    }

    /*
     * crtbegin emits references to the zero-valued .eh_frame section
     * symbol for __EH_FRAME_BEGIN__.  The input CIE at offset zero may be
     * removed when it is deduplicated against an earlier object's CIE, but
     * the section anchor itself remains valid and denotes the beginning of
     * the combined output section.  This is the same special case used by
     * Symbol.address in Zig commit 738d2be9 for a dead .eh_frame atom's
     * STT_SECTION symbol.
     */
    if (symbol->entry.st_value == 0U && relocation->addend == 0) {
        *symbol_value = symbol->section->output->addr;
        return LD_OK;
    }

    uint64_t mapped_target;
    bool live = true;
    if (!ld_elf_map_input_offset(symbol->section, original_target, 0U,
                                 &mapped_target, &live, NULL) ||
        !live) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                ".eh_frame section-symbol addend at relocation offset 0x%llx "
                "in '%s' refers to a removed record",
                (unsigned long long) relocation->offset,
                object->display_name);
    }
    uint64_t desired;
    if (ld_elf_add_overflow(symbol->section->output->addr,
                            symbol->section->output_offset, &desired) ||
        ld_elf_add_overflow(desired, mapped_target, &desired)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           ".eh_frame reference address overflows in '%s'",
                           object->display_name);
    }

    if (relocation->addend >= 0) {
        if (magnitude > desired) {
            return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                               ".eh_frame mapped addend underflows in '%s'",
                               object->display_name);
        }
        *symbol_value = desired - magnitude;
    } else {
        if (desired > UINT64_MAX - magnitude) {
            return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                               ".eh_frame mapped addend overflows in '%s'",
                               object->display_name);
        }
        *symbol_value = desired + magnitude;
    }
    return LD_OK;
}

static int ld_elf_adjust_merge_section_addend(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        const ld_elf_relocation_t *relocation, ld_elf_symbol_t *symbol,
        uint64_t *symbol_value) {
    if (symbol->type != LD_ELF_STT_SECTION || !symbol->section ||
        !symbol->section->merge_input) {
        return LD_OK;
    }

    uint64_t original_target;
    if (!ld_elf_add_signed(symbol->entry.st_value, relocation->addend,
                           &original_target)) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "ELF merge-section addend overflows at relocation offset "
                "0x%llx in '%s'",
                (unsigned long long) relocation->offset,
                object->display_name);
    }
    uint64_t mapped_target;
    bool live = true;
    if (!ld_elf_merge_map_input(symbol->section, original_target, 0U,
                                &mapped_target, &live, NULL) ||
        !live) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "ELF merge-section addend at relocation offset 0x%llx in "
                "'%s' does not select an entity",
                (unsigned long long) relocation->offset,
                object->display_name);
    }
    uint64_t desired;
    if (ld_elf_add_overflow(symbol->section->output->addr,
                            symbol->section->output_offset, &desired) ||
        ld_elf_add_overflow(desired, mapped_target, &desired)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF merge-section address overflows in '%s'",
                           object->display_name);
    }

    if (relocation->addend >= 0) {
        uint64_t magnitude = (uint64_t) relocation->addend;
        if (magnitude > desired) {
            return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                               "ELF merge-section mapped addend underflows "
                               "in '%s'",
                               object->display_name);
        }
        *symbol_value = desired - magnitude;
    } else {
        uint64_t magnitude =
                (uint64_t) (-(relocation->addend + 1)) + 1U;
        if (desired > UINT64_MAX - magnitude) {
            return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                               "ELF merge-section mapped addend overflows in "
                               "'%s'",
                               object->display_name);
        }
        *symbol_value = desired + magnitude;
    }
    return LD_OK;
}

static int ld_elf_adjust_riscv_relaxed_section_addend(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        const ld_elf_relocation_t *relocation, ld_elf_symbol_t *symbol,
        uint64_t *symbol_value) {
    if (backend->ctx->options->arch != LD_ARCH_RISCV64 ||
        symbol->type != LD_ELF_STT_SECTION || !symbol->section ||
        !ld_elf_riscv_relax_plan_active(
                &symbol->section->riscv_relax_plan)) {
        return LD_OK;
    }

    uint64_t original_target;
    if (!ld_elf_add_signed(symbol->entry.st_value, relocation->addend,
                           &original_target)) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "RISC-V relaxed section-symbol addend overflows at "
                "relocation offset 0x%llx in '%s'",
                (unsigned long long) relocation->offset,
                object->display_name);
    }
    uint64_t mapped_target;
    bool live = true;
    if (!ld_elf_riscv_relax_map(
                &symbol->section->riscv_relax_plan, original_target, 0U,
                &mapped_target, &live, NULL) ||
        !live) {
        return ld_elf_fail(
                backend->ctx, LD_RELOCATION_ERROR,
                "RISC-V relaxed section-symbol addend at relocation offset "
                "0x%llx in '%s' refers to removed alignment padding",
                (unsigned long long) relocation->offset,
                object->display_name);
    }
    uint64_t desired;
    if (ld_elf_add_overflow(symbol->section->output->addr,
                            symbol->section->output_offset, &desired) ||
        ld_elf_add_overflow(desired, mapped_target, &desired)) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "RISC-V relaxed section-symbol address overflows in '%s'",
                object->display_name);
    }

    if (relocation->addend >= 0) {
        uint64_t magnitude = (uint64_t) relocation->addend;
        if (magnitude > desired) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "RISC-V relaxed section-symbol addend underflows in "
                    "'%s'",
                    object->display_name);
        }
        *symbol_value = desired - magnitude;
    } else {
        uint64_t magnitude =
                (uint64_t) (-(relocation->addend + 1)) + 1U;
        if (desired > UINT64_MAX - magnitude) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "RISC-V relaxed section-symbol addend overflows in "
                    "'%s'",
                    object->display_name);
        }
        *symbol_value = desired + magnitude;
    }
    return LD_OK;
}

static bool ld_elf_x86_gotpcrelx_direct_fits(
        uint64_t symbol_address, int64_t addend, uint64_t place_address) {
    uint64_t target;
    if (!ld_elf_add_signed(symbol_address, addend, &target)) return false;
    if (target >= place_address) {
        return target - place_address <= (uint64_t) INT32_MAX;
    }
    return place_address - target <= (uint64_t) INT32_MAX + 1U;
}

static int ld_elf_finalize_x86_gotpcrelx(ld_elf_backend_t *backend) {
    if (backend->ctx->options->arch != LD_ARCH_AMD64) return LD_OK;

    for (;;) {
        bool changed = false;
        for (size_t i = 0; i < backend->ctx->objects.count; i++) {
            ld_elf_object_t *object = backend->ctx->objects.items[i];
            if (!object->selected) continue;
            for (size_t j = 1; j < object->section_count; j++) {
                ld_elf_section_t *section = &object->sections[j];
                if (!section->output) continue;
                for (size_t k = 0; k < section->relocation_count; k++) {
                    ld_elf_relocation_t *relocation =
                            &section->relocations[k];
                    if (!relocation->x86_gotpcrelx_relax ||
                        !ld_elf_relocation_is_live(section, relocation)) {
                        continue;
                    }

                    uint64_t mapped_offset;
                    bool live = true;
                    if (!ld_elf_map_input_offset(
                                section, relocation->offset, 4U,
                                &mapped_offset, &live, NULL) ||
                        !live) {
                        return ld_elf_fail(
                                backend->ctx, LD_RELOCATION_ERROR,
                                "GOTPCRELX relocation offset 0x%llx refers "
                                "to removed or out-of-range bytes in section "
                                "'%s' in '%s'",
                                (unsigned long long) relocation->offset,
                                section->name, object->display_name);
                    }
                    uint64_t output_offset;
                    uint64_t place_address;
                    if (ld_elf_add_overflow(section->output_offset,
                                            mapped_offset,
                                            &output_offset) ||
                        ld_elf_add_overflow(section->output->addr,
                                            output_offset,
                                            &place_address)) {
                        return ld_elf_fail(
                                backend->ctx, LD_OUTPUT_ERROR,
                                "GOTPCRELX place address overflows in "
                                "section '%s' in '%s'",
                                section->name, object->display_name);
                    }
                    if (relocation->symbol_index >= object->symbol_count) {
                        return ld_elf_fail(
                                backend->ctx, LD_INVALID_INPUT,
                                "GOTPCRELX relocation uses invalid symbol "
                                "index in '%s'",
                                object->display_name);
                    }
                    ld_elf_symbol_t *symbol =
                            &object->symbols[relocation->symbol_index];
                    uint64_t symbol_address;
                    int status = ld_elf_relocation_symbol_value(
                            backend, object, symbol, &symbol_address);
                    if (status != LD_OK) return status;
                    status = ld_elf_adjust_eh_frame_section_addend(
                            backend, object, relocation, symbol,
                            &symbol_address);
                    if (status != LD_OK) return status;
                    status = ld_elf_adjust_merge_section_addend(
                            backend, object, relocation, symbol,
                            &symbol_address);
                    if (status != LD_OK) return status;
                    if (ld_elf_x86_gotpcrelx_direct_fits(
                                symbol_address, relocation->addend,
                                place_address)) {
                        continue;
                    }

                    /*
                     * Zig 738d2be9 reserves a GOT entry while scanning every
                     * GOTPCRELX relocation, so failed instruction relaxation
                     * naturally retains the indirect form.  Nature normally
                     * suppresses that entry, but restores it here after final
                     * layout proves S + A - P cannot fit signed 32 bits.
                     */
                    relocation->x86_gotpcrelx_relax = false;
                    status = ld_elf_allocate_got_entry(
                            backend, object, section, relocation, symbol,
                            LD_ELF_RELOC_GOT_ORDINARY);
                    if (status != LD_OK) return status;
                    changed = true;
                }
            }
        }
        if (!changed) return LD_OK;

        int status = ld_elf_prepare_got_outputs(backend);
        if (status != LD_OK) return status;
        status = ld_elf_layout_outputs(backend);
        if (status != LD_OK) return status;
    }
}

static uint16_t ld_elf_debug_machine(ld_arch_t arch) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return LD_ELF_EM_X86_64;
        case LD_ARCH_ARM64:
            return LD_ELF_EM_AARCH64;
        case LD_ARCH_RISCV64:
            return LD_ELF_EM_RISCV;
        default:
            return 0U;
    }
}

static bool ld_elf_debug_target_discarded(
        const ld_elf_symbol_t *symbol) {
    if (!symbol || !symbol->section || !symbol->section->discarded)
        return false;
    if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
        symbol->resolved &&
        (symbol->resolved->definition != symbol ||
         symbol->resolved->common ||
         symbol->resolved->synthetic != LD_ELF_SYNTHETIC_NONE)) {
        return false;
    }
    return true;
}

static int ld_elf_debug_values_for_symbol(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, ld_elf_symbol_t *symbol,
        ld_elf_debug_reloc_values_t *values, int64_t *effective_addend) {
    memset(values, 0, sizeof(*values));
    *effective_addend = relocation->addend;
    values->symbol_size = ld_elf_symbol_size(symbol);
    values->got_address = backend->got ? backend->got->addr : 0U;
    values->dynamic_thread_pointer = backend->tls_addr;

    bool discarded = ld_elf_debug_target_discarded(symbol);
    ld_elf_debug_tombstone_t tombstone = ld_elf_debug_tombstone(
            section->name, discarded);
    if (tombstone != LD_ELF_DEBUG_TOMBSTONE_NONE) {
        values->has_tombstone = true;
        values->tombstone_value =
                tombstone == LD_ELF_DEBUG_TOMBSTONE_ONE ? 1U : 0U;
        return LD_OK;
    }

    /* These x86-64 formulae use GOT or symbol size, not S. */
    if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
        (relocation->type == LD_ELF_R_X86_64_GOTPC64 ||
         relocation->type == LD_ELF_R_X86_64_SIZE32 ||
         relocation->type == LD_ELF_R_X86_64_SIZE64)) {
        values->symbol_value = 0U;
        return LD_OK;
    }

    /*
     * A relocation against an STT_SECTION symbol in an SHF_MERGE section
     * names the input entity through S + A.  Once duplicate strings or
     * constants are folded, that entity can move to an output offset smaller
     * than the original addend.  Allocated sections can represent the
     * adjustment as a reduced (but still positive) S because they have a
     * virtual-address base.  Non-allocated DWARF sections have sh_addr zero,
     * so trying to manufacture S' = mapped(S + A) - A can underflow.
     *
     * Resolve the complete expression here and pass it to the narrow
     * non-alloc relocation helper with an effective addend of zero.  This is
     * equivalent to Zig's merge-fragment synthetic symbol address and keeps
     * .debug_str/.debug_line_str references valid after deduplication.
     */
    if (symbol->type == LD_ELF_STT_SECTION && symbol->section &&
        symbol->section->merge_input) {
        uint64_t original_target;
        if (!ld_elf_add_signed(symbol->entry.st_value,
                               relocation->addend, &original_target)) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "ELF merge-section addend overflows at non-allocated "
                    "relocation offset 0x%llx in '%s'",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
        uint64_t mapped_target;
        bool live = true;
        if (!ld_elf_merge_map_input(symbol->section, original_target, 0U,
                                    &mapped_target, &live, NULL) ||
            !live) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "ELF merge-section addend at non-allocated relocation "
                    "offset 0x%llx in '%s' does not select an entity",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
        uint64_t value;
        if (ld_elf_add_overflow(symbol->section->output->addr,
                                symbol->section->output_offset, &value) ||
            ld_elf_add_overflow(value, mapped_target, &value)) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF merge-section address overflows at non-allocated "
                    "relocation offset 0x%llx in '%s'",
                    (unsigned long long) relocation->offset,
                    object->display_name);
        }
        if (backend->ctx->options->arch == LD_ARCH_RISCV64 &&
            relocation->type == LD_ELF_R_RISCV_SUB_ULEB128) {
            /*
             * Zig rewrites the section symbol to S' = mapped(S + A) - A,
             * while SUB_ULEB128 itself evaluates S' - A.  Preserve that
             * observable mapped(S + A) - 2*A result without representing a
             * possibly negative intermediate synthetic symbol in uint64_t.
             */
            uint64_t adjusted;
            if (!ld_elf_subtract_signed(value, relocation->addend,
                                        &adjusted) ||
                !ld_elf_subtract_signed(adjusted, relocation->addend,
                                        &value)) {
                return ld_elf_fail(
                        backend->ctx, LD_RELOCATION_ERROR,
                        "R_RISCV_SUB_ULEB128 merge-section expression "
                        "overflows at offset 0x%llx in '%s'",
                        (unsigned long long) relocation->offset,
                        object->display_name);
            }
        }
        values->symbol_value = value;
        *effective_addend = 0;
        return LD_OK;
    }
    int status;
    ld_elf_symbol_t *ifunc = ld_elf_ifunc_definition(symbol);
    if (ifunc) {
        uint32_t index = ifunc->pltgot_index;
        if (ld_elf_symbol_is_global(symbol) && symbol->name[0] &&
            symbol->resolved) {
            index = symbol->resolved->pltgot_index;
        }
        if (backend->plt_got && index != LD_ELF_NO_GOT &&
            index < backend->pltgot_count) {
            status = ld_elf_pltgot_address(
                    backend, index, symbol->name,
                    &values->symbol_value);
        } else {
            /* Non-alloc relocations must not create a PLT-GOT entry. */
            status = ld_elf_symbol_value(
                    backend, object, symbol, &values->symbol_value);
        }
    } else {
        status = ld_elf_relocation_symbol_value(
                backend, object, symbol, &values->symbol_value);
    }
    if (status != LD_OK) return status;
    status = ld_elf_adjust_eh_frame_section_addend(
            backend, object, relocation, symbol, &values->symbol_value);
    if (status != LD_OK) return status;
    status = ld_elf_adjust_merge_section_addend(
            backend, object, relocation, symbol, &values->symbol_value);
    if (status != LD_OK) return status;
    return ld_elf_adjust_riscv_relaxed_section_addend(
            backend, object, relocation, symbol, &values->symbol_value);
}

static int ld_elf_nonalloc_relocation_error(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section,
        const ld_elf_relocation_t *relocation, const char *symbol_name,
        ld_elf_debug_reloc_result_t result) {
    int code = result == LD_ELF_DEBUG_RELOC_UNSUPPORTED_MACHINE ||
                               result ==
                                       LD_ELF_DEBUG_RELOC_UNSUPPORTED_RELOCATION
                       ? LD_UNSUPPORTED
                       : LD_RELOCATION_ERROR;
    return ld_elf_fail(
            backend->ctx, code,
            "cannot apply non-allocated ELF relocation %s for symbol '%s' "
            "at offset 0x%llx in section '%s' in '%s': %s",
            ld_elf_relocation_name(backend->ctx->options->arch,
                                   relocation->type),
            symbol_name && *symbol_name ? symbol_name : "<anonymous>",
            (unsigned long long) relocation->offset, section->name,
            object->display_name,
            ld_elf_debug_reloc_result_string(result));
}

static bool ld_elf_nonalloc_relocation_is_none(ld_arch_t arch,
                                               uint32_t type) {
    switch (arch) {
        case LD_ARCH_AMD64:
            return type == LD_ELF_R_X86_64_NONE;
        case LD_ARCH_ARM64:
            return type == LD_ELF_R_AARCH64_NONE;
        case LD_ARCH_RISCV64:
            return type == LD_ELF_R_RISCV_NONE;
        default:
            return false;
    }
}

static int ld_elf_apply_nonalloc_relocations(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_section_t *section) {
    ld_elf_input_placement_t *placement =
            ld_elf_placement_find(backend, section);
    if (!placement || !section->output) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "missing non-allocated ELF output for section '%s' in '%s'",
                section->name, object->display_name);
    }
    uint16_t machine = ld_elf_debug_machine(
            backend->ctx->options->arch);
    for (size_t i = 0; i < section->relocation_count; i++) {
        ld_elf_relocation_t *relocation = &section->relocations[i];
        if (ld_elf_nonalloc_relocation_is_none(
                    backend->ctx->options->arch, relocation->type)) {
            continue;
        }
        if (relocation->x86_tls_pair_follower) {
            continue;
        }
        if (!section->output->data) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "missing non-allocated ELF output data for section '%s' "
                    "in '%s'",
                    section->name, object->display_name);
        }
        uint64_t mapped_offset;
        uint64_t available;
        bool live = true;
        if (!ld_elf_map_input_offset(section, relocation->offset, 0U,
                                     &mapped_offset, &live, &available) ||
            !live || mapped_offset > placement->emitted_size ||
            available > placement->emitted_size - mapped_offset) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "non-allocated ELF relocation offset 0x%llx is outside "
                    "section '%s' in '%s'",
                    (unsigned long long) relocation->offset, section->name,
                    object->display_name);
        }
        if (relocation->symbol_index >= object->symbol_count) {
            return ld_elf_fail(
                    backend->ctx, LD_INVALID_INPUT,
                    "non-allocated ELF relocation uses invalid symbol index "
                    "in '%s'",
                    object->display_name);
        }
        uint64_t output_offset;
        if (ld_elf_add_overflow(section->output_offset, mapped_offset,
                                &output_offset) ||
            output_offset > section->output->file_size ||
            available > section->output->file_size - output_offset ||
            available > SIZE_MAX) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "non-allocated ELF relocation output range overflows in "
                    "section '%s' in '%s'",
                    section->name, object->display_name);
        }
        ld_elf_symbol_t *symbol =
                &object->symbols[relocation->symbol_index];
        ld_elf_debug_reloc_values_t values;
        int64_t effective_addend;
        int status = ld_elf_debug_values_for_symbol(
                backend, object, section, relocation, symbol, &values,
                &effective_addend);
        if (status != LD_OK) return status;

        uint8_t *place = section->output->data + output_offset;
        size_t place_size = (size_t) available;
        size_t written = 0U;
        const ld_elf_debug_relocation_t input = {
                .type = relocation->type,
                .addend = effective_addend,
        };
        ld_elf_debug_reloc_result_t result =
                ld_elf_debug_apply_nonalloc_relocation(
                        machine, &input, &values, place, place_size,
                        &written);
        if (result != LD_ELF_DEBUG_RELOC_OK) {
            return ld_elf_nonalloc_relocation_error(
                    backend, object, section, relocation, symbol->name,
                    result);
        }
        if (written > place_size) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "non-allocated ELF relocation wrote past section '%s' "
                    "in '%s'",
                    section->name, object->display_name);
        }
    }
    return LD_OK;
}

static int ld_elf_apply_relocations(ld_elf_backend_t *backend) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output || section->relocation_count == 0U) continue;
            if ((section->output->flags & LD_ELF_SHF_ALLOC) == 0U) {
                int status = ld_elf_apply_nonalloc_relocations(
                        backend, object, section);
                if (status != LD_OK) return status;
                continue;
            }
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                if (relocation->x86_tls_pair_follower) {
                    continue;
                }
                size_t write_width = ld_elf_relocation_write_width(
                        backend->ctx->options->arch, relocation->type);
                uint64_t mapped_offset;
                uint64_t available;
                bool live = true;
                uint64_t mapping_width =
                        write_width == SIZE_MAX ? 0U : (uint64_t) write_width;
                if (!ld_elf_map_input_offset(
                            section, relocation->offset, mapping_width,
                            &mapped_offset, &live, &available)) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "relocation offset 0x%llx refers to removed, "
                            "cross-record, or out-of-range bytes in section "
                            "'%s' in '%s'",
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                if (!live) continue;
                if (backend->ctx->options->arch == LD_ARCH_RISCV64 &&
                    relocation->type == LD_ELF_R_RISCV_ALIGN &&
                    ld_elf_riscv_relax_plan_active(
                            &section->riscv_relax_plan)) {
                    continue;
                }
                if (relocation->symbol_index >= object->symbol_count) {
                    return ld_elf_fail(backend->ctx, LD_INVALID_INPUT,
                                       "relocation uses invalid symbol index in '%s'",
                                       object->display_name);
                }
                ld_elf_symbol_t *symbol =
                        &object->symbols[relocation->symbol_index];
                uint64_t symbol_value, got_value = 0U;
                int status = relocation->ifunc_irelative
                                     ? ld_elf_symbol_value(
                                               backend, object, symbol,
                                               &symbol_value)
                                     : ld_elf_relocation_symbol_value(
                                               backend, object, symbol,
                                               &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_eh_frame_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_merge_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_riscv_relaxed_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                ld_elf_reloc_got_kind_t got_kind =
                        ld_elf_relocation_got_kind(
                                backend->ctx->options->arch,
                                relocation->type);
                if (got_kind == LD_ELF_RELOC_GOT_ORDINARY &&
                    !relocation->x86_gotpcrelx_relax) {
                    status = ld_elf_got_address(backend, object, symbol,
                                                &got_value);
                } else if (got_kind == LD_ELF_RELOC_GOT_TP) {
                    if (!(backend->ctx->options->arch == LD_ARCH_AMD64 &&
                          relocation->type ==
                                  LD_ELF_R_X86_64_GOTTPOFF &&
                          relocation->x86_gottpoff_relax)) {
                        status = ld_elf_gottp_address(
                                backend, object, symbol, &got_value);
                    }
                } else if (got_kind == LD_ELF_RELOC_GOT_TLSGD) {
                    status = ld_elf_tlsgd_address(backend, object, symbol,
                                                  &got_value);
                }
                if (got_kind != LD_ELF_RELOC_GOT_NONE) {
                    if (status != LD_OK) return status;
                }
                bool needs_got_base = ld_elf_relocation_needs_got_base(
                        backend->ctx->options->arch, relocation->type);
                if (needs_got_base && !backend->got) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "missing ELF GOT base for %s in section '%s' "
                            "in '%s'",
                            ld_elf_relocation_name(
                                    backend->ctx->options->arch,
                                    relocation->type),
                            section->name, object->display_name);
                }
                ld_elf_input_placement_t *placement =
                        ld_elf_placement_find(backend, section);
                if (!placement) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "missing ELF placement for relocation in section "
                            "'%s' in '%s'",
                            section->name, object->display_name);
                }
                if (mapped_offset > placement->emitted_size ||
                    available > placement->emitted_size - mapped_offset) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "mapped relocation offset for input 0x%llx is "
                            "outside section '%s' in '%s'",
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                uint64_t output_offset;
                if (ld_elf_add_overflow(section->output_offset,
                                        mapped_offset, &output_offset) ||
                    output_offset > section->output->file_size) {
                    return ld_elf_fail(backend->ctx, LD_RELOCATION_ERROR,
                                       "relocation offset 0x%llx is outside section "
                                       "'%s' in '%s'",
                                       (unsigned long long) relocation->offset,
                                       section->name, object->display_name);
                }
                uint64_t place = section->output->addr + output_offset;
                ld_elf_reloc_values_t values = {
                        .place_address = place,
                        .symbol_address = symbol_value,
                        .got_entry_address = got_value,
                        .got_base_address = backend->got
                                                    ? backend->got->addr
                                                    : 0U,
                        .symbol_size = ld_elf_symbol_size(symbol),
                        .thread_pointer_address = backend->tp_addr,
                        .tls_block_address = backend->tls_addr,
                        .image_base_address = backend->image_base,
                        .symbol_name = symbol->name,
                };
                status = ld_elf_resolve_riscv_pair(
                        backend, object, section, relocation, symbol,
                        symbol_value, &values);
                if (status != LD_OK) return status;
                if (placement->emitted_size > SIZE_MAX) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "input placement for section '%s' in '%s' is too "
                            "large for relocation processing",
                            section->name, object->display_name);
                }
                uint8_t *section_data =
                        section->output->data + section->output_offset;
                if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
                    ld_elf_relocation_is_x86_tls_pair_start(
                            relocation->type)) {
                    if (relocation->x86_tls_pair_index >=
                        section->relocation_count) {
                        return ld_elf_fail(
                                backend->ctx, LD_RELOCATION_ERROR,
                                "missing prepared x86-64 TLS relocation pair "
                                "in section '%s' in '%s' at offset 0x%llx",
                                section->name, object->display_name,
                                (unsigned long long) relocation->offset);
                    }
                    status = ld_elf_relocation_apply_x86_tls_pair(
                            backend->ctx, object, section, relocation,
                            &section->relocations
                                     [relocation->x86_tls_pair_index],
                            section_data, (size_t) placement->emitted_size,
                            mapped_offset, &values);
                    if (status != LD_OK) return status;
                    continue;
                }
                if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
                    relocation->type == LD_ELF_R_X86_64_GOTTPOFF &&
                    relocation->x86_gottpoff_relax) {
                    status = ld_elf_relocation_apply_x86_gottpoff(
                            backend->ctx, object, section, relocation,
                            section_data, (size_t) placement->emitted_size,
                            mapped_offset, &values);
                    if (status != LD_OK) return status;
                    continue;
                }
                if (backend->ctx->options->arch == LD_ARCH_AMD64 &&
                    relocation->x86_gotpcrelx_relax) {
                    status = ld_elf_relocation_apply_x86_gotpcrelx(
                            backend->ctx, object, section, relocation,
                            section_data, (size_t) placement->emitted_size,
                            mapped_offset, &values);
                    if (status != LD_OK) return status;
                    continue;
                }
                if (backend->ctx->options->arch == LD_ARCH_ARM64 &&
                    ld_elf_is_aarch64_branch_relocation(
                            relocation->type)) {
                    uint64_t direct_target;
                    if (!ld_elf_add_signed(symbol_value,
                                           relocation->addend,
                                           &direct_target)) {
                        return ld_elf_fail(
                                backend->ctx, LD_RELOCATION_ERROR,
                                "%s target S + A overflows for symbol '%s' "
                                "at offset 0x%llx in section '%s' in '%s'",
                                ld_elf_relocation_name(
                                        LD_ARCH_ARM64,
                                        relocation->type),
                                symbol->name,
                                (unsigned long long) relocation->offset,
                                section->name, object->display_name);
                    }
                    if (!ld_elf_aarch64_branch26_fits(
                                direct_target, place, NULL)) {
                        uint32_t group_index =
                                placement->aarch64_thunk_group_index;
                        uint32_t entry_index =
                                relocation->aarch64_thunk_entry_index;
                        if (group_index >=
                                    backend->aarch64_thunk_plan.group_count ||
                            entry_index == LD_ELF_AARCH64_NO_THUNK) {
                            return ld_elf_fail(
                                    backend->ctx, LD_OUTPUT_ERROR,
                                    "missing planned AArch64 thunk for symbol "
                                    "'%s' at offset 0x%llx in section '%s' "
                                    "in '%s'",
                                    symbol->name,
                                    (unsigned long long) relocation->offset,
                                    section->name, object->display_name);
                        }
                        uint64_t thunk_address;
                        status = ld_elf_aarch64_thunk_address(
                                backend, group_index, entry_index,
                                &thunk_address);
                        if (status != LD_OK) return status;
                        ld_elf_relocation_t thunk_relocation = *relocation;
                        thunk_relocation.addend = 0;
                        ld_elf_reloc_values_t thunk_values = values;
                        thunk_values.symbol_address = thunk_address;
                        status = ld_elf_relocation_apply(
                                backend->ctx, object, section,
                                &thunk_relocation,
                                section->output->data + output_offset,
                                (size_t) available, &thunk_values);
                        if (status != LD_OK) return status;
                        continue;
                    }
                }
                status = ld_elf_relocation_apply(
                        backend->ctx, object, section, relocation,
                        section->output->data + output_offset,
                        (size_t) available,
                        &values);
                if (status != LD_OK) return status;
            }
        }
    }
    return LD_OK;
}

static int ld_elf_append_relative_relocation(
        ld_elf_backend_t *backend,
        ld_elf_dynamic_relocation_t *relocations, size_t capacity,
        size_t *count, uint64_t offset, int64_t addend,
        const char *symbol_name) {
    if (*count >= capacity) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "ELF static PIE relative relocation count changed while "
                "encoding symbol '%s'",
                symbol_name && *symbol_name ? symbol_name : "<anonymous>");
    }
    uint32_t type = ld_elf_dynamic_relative_type(
            backend->ctx->options->arch);
    if (type == 0U) {
        return ld_elf_fail(backend->ctx, LD_UNSUPPORTED,
                           "ELF static PIE relative relocations are not "
                           "supported for this architecture");
    }
    relocations[(*count)++] = (ld_elf_dynamic_relocation_t) {
            .offset = offset,
            .type = type,
            .addend = addend,
            .kind = LD_ELF_DYNAMIC_RELOC_RELATIVE,
    };
    return LD_OK;
}

static int ld_elf_collect_input_relative_relocations(
        ld_elf_backend_t *backend,
        ld_elf_dynamic_relocation_t *relocations, size_t capacity,
        size_t *count) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (!section->output) continue;
            for (size_t k = 0; k < section->relocation_count; k++) {
                ld_elf_relocation_t *relocation = &section->relocations[k];
                if (!relocation->pie_relative ||
                    !ld_elf_relocation_is_live(section, relocation)) {
                    continue;
                }
                if (relocation->symbol_index >= object->symbol_count) {
                    return ld_elf_fail(
                            backend->ctx, LD_INVALID_INPUT,
                            "ELF static PIE relocation uses invalid symbol "
                            "index in '%s'",
                            object->display_name);
                }
                uint64_t mapped_offset;
                uint64_t available;
                bool live = true;
                if (!ld_elf_map_input_offset(
                            section, relocation->offset, 8U, &mapped_offset,
                            &live, &available) ||
                    !live || available < 8U) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "ELF static PIE relocation offset 0x%llx is "
                            "outside live section '%s' in '%s'",
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                uint64_t output_offset;
                uint64_t place_address;
                if (ld_elf_add_overflow(section->output_offset,
                                        mapped_offset, &output_offset) ||
                    ld_elf_add_overflow(section->output->addr,
                                        output_offset, &place_address)) {
                    return ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "ELF static PIE relocation place overflows in "
                            "section '%s' in '%s'",
                            section->name, object->display_name);
                }
                ld_elf_symbol_t *symbol =
                        &object->symbols[relocation->symbol_index];
                uint64_t symbol_value;
                int status = ld_elf_relocation_symbol_value(
                        backend, object, symbol, &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_eh_frame_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_merge_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                status = ld_elf_adjust_riscv_relaxed_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) return status;
                int64_t target;
                if (!ld_elf_add_signed_to_i64(
                            symbol_value, relocation->addend, &target)) {
                    return ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            "ELF static PIE S + A overflows for symbol '%s' "
                            "at offset 0x%llx in section '%s' in '%s'",
                            symbol->name,
                            (unsigned long long) relocation->offset,
                            section->name, object->display_name);
                }
                status = ld_elf_append_relative_relocation(
                        backend, relocations, capacity, count, place_address,
                        target, symbol->name);
                if (status != LD_OK) return status;
            }
        }
    }
    return LD_OK;
}

static int ld_elf_collect_got_relative_relocations(
        ld_elf_backend_t *backend,
        ld_elf_dynamic_relocation_t *relocations, size_t capacity,
        size_t *count) {
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (global->got_index == LD_ELF_NO_GOT ||
            !ld_elf_global_needs_base_relocation(global) ||
            (global->definition &&
             global->definition->type == LD_ELF_STT_GNU_IFUNC)) {
            continue;
        }
        uint64_t value;
        int status = ld_elf_global_value(backend, global, &value);
        if (status != LD_OK) return status;
        uint64_t offset;
        if (ld_elf_add_overflow(backend->got->addr,
                                (uint64_t) global->got_index * 8U,
                                &offset)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF GOT address overflows for symbol '%s'",
                               global->name);
        }
        if (value > (uint64_t) INT64_MAX) {
            return ld_elf_fail(
                    backend->ctx, LD_RELOCATION_ERROR,
                    "ELF GOT relative relocation addend 0x%llx for "
                    "symbol '%s' exceeds signed ELF64 range",
                    (unsigned long long) value, global->name);
        }
        status = ld_elf_append_relative_relocation(
                backend, relocations, capacity, count, offset,
                (int64_t) value,
                global->name);
        if (status != LD_OK) return status;
    }
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->first_global_symbol; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->got_index == LD_ELF_NO_GOT ||
                symbol->type == LD_ELF_STT_GNU_IFUNC ||
                !ld_elf_symbol_needs_base_relocation(symbol)) {
                continue;
            }
            uint64_t value;
            int status = ld_elf_symbol_value(
                    backend, object, symbol, &value);
            if (status != LD_OK) return status;
            uint64_t offset;
            if (ld_elf_add_overflow(backend->got->addr,
                                    (uint64_t) symbol->got_index * 8U,
                                    &offset)) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "ELF GOT address overflows for local symbol '%s' "
                        "in '%s'",
                        symbol->name, object->display_name);
            }
            if (value > (uint64_t) INT64_MAX) {
                return ld_elf_fail(
                        backend->ctx, LD_RELOCATION_ERROR,
                        "ELF GOT relative relocation addend 0x%llx for "
                        "local symbol '%s' in '%s' exceeds signed ELF64 "
                        "range",
                        (unsigned long long) value, symbol->name,
                        object->display_name);
            }
            status = ld_elf_append_relative_relocation(
                    backend, relocations, capacity, count, offset,
                    (int64_t) value,
                    symbol->name);
            if (status != LD_OK) return status;
        }
    }
    return LD_OK;
}

static int ld_elf_emit_pie_relative_relocations(
        ld_elf_backend_t *backend) {
    if (!backend->ctx->options->pie ||
        backend->relative_relocation_count == 0U) {
        return LD_OK;
    }
    if (!backend->rela_dyn || !backend->rela_dyn->data ||
        (backend->pie_got_relocation_count != 0U && !backend->got)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing ELF static PIE relocation output");
    }
    size_t bytes;
    if (ld_elf_mul_overflow(backend->relative_relocation_count,
                            sizeof(ld_elf_dynamic_relocation_t), &bytes)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF static PIE relocation allocation overflows");
    }
    ld_elf_dynamic_relocation_t *relocations = malloc(bytes);
    if (!relocations) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory collecting ELF static PIE "
                           "relocations");
    }
    size_t count = 0U;
    int status = ld_elf_collect_input_relative_relocations(
            backend, relocations, backend->relative_relocation_count,
            &count);
    if (status == LD_OK) {
        status = ld_elf_collect_got_relative_relocations(
                backend, relocations, backend->relative_relocation_count,
                &count);
    }
    if (status == LD_OK && count != backend->relative_relocation_count) {
        status = ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "ELF static PIE relative relocation count changed from %zu "
                "to %zu",
                backend->relative_relocation_count, count);
    }
    if (status == LD_OK) {
        ld_elf_dynamic_sort_relocations(relocations, count);
        size_t output_size;
        if (ld_elf_mul_overflow(count, LD_ELF64_RELA_SIZE, &output_size) ||
            output_size > backend->rela_dyn->file_size) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF static PIE relative relocations exceed .rela.dyn");
        } else {
            ld_elf_dynamic_result_t result =
                    ld_elf_dynamic_encode_relocations(
                            relocations, count, backend->rela_dyn->data,
                            output_size);
            if (result != LD_ELF_DYNAMIC_OK) {
                status = ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "cannot encode ELF static PIE relative "
                        "relocations: %s",
                        ld_elf_dynamic_result_string(result));
            }
        }
    }
    free(relocations);
    return status;
}

static int ld_elf_emit_static_pie_metadata(ld_elf_backend_t *backend) {
    if (!backend->ctx->options->pie) return LD_OK;
    if (!backend->dynsym || !backend->dynstr || !backend->hash ||
        !backend->gnu_hash || !backend->dynamic || !backend->rela_dyn ||
        !backend->dynsym->data || !backend->dynstr->data ||
        !backend->hash->data || !backend->gnu_hash->data ||
        !backend->dynamic->data) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "missing ELF static PIE dynamic metadata output");
    }
    ld_elf_dynamic_result_t result = ld_elf_dynamic_encode_minimal_tables(
            backend->dynsym->data, (size_t) backend->dynsym->file_size,
            backend->dynstr->data, (size_t) backend->dynstr->file_size,
            backend->hash->data, (size_t) backend->hash->file_size,
            backend->gnu_hash->data,
            (size_t) backend->gnu_hash->file_size);
    if (result != LD_ELF_DYNAMIC_OK) {
        return ld_elf_fail(
                backend->ctx, LD_OUTPUT_ERROR,
                "cannot encode ELF static PIE symbol/hash tables: %s",
                ld_elf_dynamic_result_string(result));
    }

    ld_elf_dynamic_metadata_t metadata = {
            .has_init = backend->init != NULL,
            .has_fini = backend->fini != NULL,
            .has_init_array = backend->init_array != NULL,
            .has_fini_array = backend->fini_array != NULL,
            .has_rela = backend->rela_dyn->size != 0U,
            .has_static_tls = backend->gottp_count != 0U,
            .init_address = ld_elf_output_start(backend->init),
            .fini_address = ld_elf_output_start(backend->fini),
            .init_array_address = ld_elf_output_start(backend->init_array),
            .init_array_size = backend->init_array
                                       ? backend->init_array->size
                                       : 0U,
            .fini_array_address = ld_elf_output_start(backend->fini_array),
            .fini_array_size = backend->fini_array
                                       ? backend->fini_array->size
                                       : 0U,
            .rela_address = ld_elf_output_start(backend->rela_dyn),
            .rela_size = backend->rela_dyn->size,
            .hash_address = ld_elf_output_start(backend->hash),
            .gnu_hash_address = ld_elf_output_start(backend->gnu_hash),
            .dynsym_address = ld_elf_output_start(backend->dynsym),
            .dynstr_address = ld_elf_output_start(backend->dynstr),
            .dynstr_size = backend->dynstr->size,
    };
    result = ld_elf_dynamic_encode_metadata(
            &metadata, backend->dynamic->data,
            (size_t) backend->dynamic->file_size);
    if (result != LD_ELF_DYNAMIC_OK) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "cannot encode ELF static PIE .dynamic: %s",
                           ld_elf_dynamic_result_string(result));
    }
    return LD_OK;
}

typedef struct {
    const ld_elf_object_t *object;
    const ld_elf_eh_record_t *record;
    const char *symbol_name;
} ld_elf_eh_frame_hdr_entry_context_t;

static int ld_elf_build_eh_frame_hdr(ld_elf_backend_t *backend) {
    if (!backend->eh_frame_hdr) return LD_OK;
    if (!backend->eh_frame || !backend->eh_frame_hdr->data ||
        backend->eh_frame_hdr->file_size > SIZE_MAX) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "invalid ELF .eh_frame_hdr output allocation");
    }

    size_t entry_bytes = 0U;
    size_t context_bytes = 0U;
    if (ld_elf_mul_overflow(backend->eh_frame_fde_count,
                            sizeof(ld_elf_eh_frame_hdr_entry_t),
                            &entry_bytes) ||
        ld_elf_mul_overflow(backend->eh_frame_fde_count,
                            sizeof(ld_elf_eh_frame_hdr_entry_context_t),
                            &context_bytes)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF .eh_frame_hdr entry allocation overflows");
    }
    ld_elf_eh_frame_hdr_entry_t *entries =
            entry_bytes ? malloc(entry_bytes) : NULL;
    ld_elf_eh_frame_hdr_entry_context_t *contexts =
            context_bytes ? malloc(context_bytes) : NULL;
    if ((entry_bytes && !entries) || (context_bytes && !contexts)) {
        free(entries);
        free(contexts);
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory collecting ELF .eh_frame_hdr FDEs");
    }

    int status = LD_OK;
    size_t entry_index = 0U;
    for (size_t i = 0; i < backend->ctx->objects.count && status == LD_OK;
         i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count && status == LD_OK; j++) {
            ld_elf_section_t *section = &object->sections[j];
            if (section->output != backend->eh_frame) continue;
            for (size_t k = 0; k < section->eh_record_count; k++) {
                const ld_elf_eh_record_t *record = &section->eh_records[k];
                if (!record->alive || record->cie) continue;
                if (entry_index >= backend->eh_frame_fde_count) {
                    status = ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "ELF .eh_frame_hdr live FDE count changed after "
                            "layout");
                    break;
                }
                if (record->owner_relocation_index ==
                            LD_ELF_EH_RELOCATION_NONE ||
                    record->owner_relocation_index >=
                            section->relocation_count) {
                    status = ld_elf_fail(
                            backend->ctx, LD_INVALID_INPUT,
                            ".eh_frame FDE at offset 0x%llx in '%s' has no "
                            "valid initial-PC relocation",
                            (unsigned long long) record->input_offset,
                            object->display_name);
                    break;
                }
                const ld_elf_relocation_t *relocation =
                        &section->relocations
                                 [record->owner_relocation_index];
                if (relocation->symbol_index >= object->symbol_count) {
                    status = ld_elf_fail(
                            backend->ctx, LD_INVALID_INPUT,
                            ".eh_frame FDE at offset 0x%llx in '%s' has an "
                            "invalid initial-PC symbol index",
                            (unsigned long long) record->input_offset,
                            object->display_name);
                    break;
                }
                ld_elf_symbol_t *symbol =
                        &object->symbols[relocation->symbol_index];
                uint64_t symbol_value = 0U;
                status = ld_elf_symbol_value(backend, object, symbol,
                                             &symbol_value);
                if (status != LD_OK) break;
                status = ld_elf_adjust_eh_frame_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) break;
                status = ld_elf_adjust_riscv_relaxed_section_addend(
                        backend, object, relocation, symbol, &symbol_value);
                if (status != LD_OK) break;

                uint64_t first_pc = 0U;
                if (!ld_elf_add_signed(symbol_value, relocation->addend,
                                       &first_pc)) {
                    status = ld_elf_fail(
                            backend->ctx, LD_RELOCATION_ERROR,
                            ".eh_frame FDE first PC S + A overflows for "
                            "symbol '%s' at offset 0x%llx in '%s'",
                            symbol->name,
                            (unsigned long long) record->input_offset,
                            object->display_name);
                    break;
                }
                uint64_t fde_address = backend->eh_frame->addr;
                if (ld_elf_add_overflow(fde_address,
                                        section->output_offset,
                                        &fde_address) ||
                    ld_elf_add_overflow(fde_address,
                                        record->output_offset,
                                        &fde_address)) {
                    status = ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            ".eh_frame FDE address overflows for record at "
                            "offset 0x%llx in '%s'",
                            (unsigned long long) record->input_offset,
                            object->display_name);
                    break;
                }
                entries[entry_index] = (ld_elf_eh_frame_hdr_entry_t) {
                        .first_pc = first_pc,
                        .fde_address = fde_address,
                };
                contexts[entry_index] =
                        (ld_elf_eh_frame_hdr_entry_context_t) {
                                .object = object,
                                .record = record,
                                .symbol_name = symbol->name,
                        };
                entry_index++;
            }
        }
    }

    if (status == LD_OK && entry_index != backend->eh_frame_fde_count) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF .eh_frame_hdr expected %zu live FDEs but "
                             "collected %zu",
                             backend->eh_frame_fde_count, entry_index);
    }
    if (status == LD_OK) {
        size_t error_entry_index = SIZE_MAX;
        ld_elf_eh_frame_hdr_result_t result = ld_elf_eh_frame_hdr_encode(
                backend->eh_frame_hdr->data,
                (size_t) backend->eh_frame_hdr->file_size,
                backend->eh_frame_hdr->addr, backend->eh_frame->addr,
                entries, entry_index, &error_entry_index);
        if (result != LD_ELF_EH_FRAME_HDR_OK) {
            int code = result == LD_ELF_EH_FRAME_HDR_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : LD_OUTPUT_ERROR;
            if (error_entry_index < entry_index) {
                const ld_elf_eh_frame_hdr_entry_context_t *context =
                        &contexts[error_entry_index];
                status = ld_elf_fail(
                        backend->ctx, code,
                        "cannot encode ELF .eh_frame_hdr FDE for symbol '%s' "
                        "at record offset 0x%llx in '%s': %s",
                        context->symbol_name,
                        (unsigned long long) context->record->input_offset,
                        context->object->display_name,
                        ld_elf_eh_frame_hdr_result_string(result));
            } else {
                status = ld_elf_fail(
                        backend->ctx, code,
                        "cannot encode ELF .eh_frame_hdr: %s",
                        ld_elf_eh_frame_hdr_result_string(result));
            }
        }
    }

    free(entries);
    free(contexts);
    return status;
}

static void ld_elf_write_header(uint8_t *image, uint16_t type,
                                uint16_t machine, uint64_t entry,
                                uint32_t flags,
                                uint16_t phnum, uint64_t shoff,
                                uint16_t shnum, uint16_t shstrndx) {
    memset(image, 0, LD_ELF64_EHDR_SIZE);
    image[0] = LD_ELF_MAGIC_0;
    image[1] = LD_ELF_MAGIC_1;
    image[2] = LD_ELF_MAGIC_2;
    image[3] = LD_ELF_MAGIC_3;
    image[LD_ELF_EI_CLASS] = LD_ELF_CLASS_64;
    image[LD_ELF_EI_DATA] = LD_ELF_DATA_LSB;
    image[LD_ELF_EI_VERSION] = LD_ELF_VERSION_CURRENT;
    ld_elf_write_u16(image + 16U, type);
    ld_elf_write_u16(image + 18U, machine);
    ld_elf_write_u32(image + 20U, LD_ELF_VERSION_CURRENT);
    ld_elf_write_u64(image + 24U, entry);
    ld_elf_write_u64(image + 32U, LD_ELF64_EHDR_SIZE);
    ld_elf_write_u64(image + 40U, shoff);
    ld_elf_write_u32(image + 48U, flags);
    ld_elf_write_u16(image + 52U, LD_ELF64_EHDR_SIZE);
    ld_elf_write_u16(image + 54U, LD_ELF64_PHDR_SIZE);
    ld_elf_write_u16(image + 56U, phnum);
    ld_elf_write_u16(image + 58U, LD_ELF64_SHDR_SIZE);
    ld_elf_write_u16(image + 60U, shnum);
    ld_elf_write_u16(image + 62U, shstrndx);
}

static void ld_elf_write_phdr(uint8_t *bytes, uint32_t type, uint32_t flags,
                              uint64_t offset, uint64_t address,
                              uint64_t file_size, uint64_t memory_size,
                              uint64_t align) {
    ld_elf_write_u32(bytes, type);
    ld_elf_write_u32(bytes + 4U, flags);
    ld_elf_write_u64(bytes + 8U, offset);
    ld_elf_write_u64(bytes + 16U, address);
    ld_elf_write_u64(bytes + 24U, address);
    ld_elf_write_u64(bytes + 32U, file_size);
    ld_elf_write_u64(bytes + 40U, memory_size);
    ld_elf_write_u64(bytes + 48U, align);
}

static void ld_elf_write_shdr(uint8_t *bytes, uint32_t name, uint32_t type,
                              uint64_t flags, uint64_t address,
                              uint64_t offset, uint64_t size, uint32_t link,
                              uint32_t info, uint64_t align,
                              uint64_t entry_size) {
    memset(bytes, 0, LD_ELF64_SHDR_SIZE);
    ld_elf_write_u32(bytes, name);
    ld_elf_write_u32(bytes + 4U, type);
    ld_elf_write_u64(bytes + 8U, flags);
    ld_elf_write_u64(bytes + 16U, address);
    ld_elf_write_u64(bytes + 24U, offset);
    ld_elf_write_u64(bytes + 32U, size);
    ld_elf_write_u32(bytes + 40U, link);
    ld_elf_write_u32(bytes + 44U, info);
    ld_elf_write_u64(bytes + 48U, align);
    ld_elf_write_u64(bytes + 56U, entry_size);
}

static uint64_t ld_elf_output_entry_size(
        const ld_elf_output_section_t *output) {
    if (output->entry_size != 0U) return output->entry_size;
    switch (output->type) {
        case LD_ELF_SHT_PREINIT_ARRAY:
        case LD_ELF_SHT_INIT_ARRAY:
        case LD_ELF_SHT_FINI_ARRAY:
            return 8U;
        case LD_ELF_SHT_RELA:
            return LD_ELF64_RELA_SIZE;
        default:
            return 0U;
    }
}

static int ld_elf_write_all(int fd, const uint8_t *bytes, size_t size) {
    size_t offset = 0U;
    while (offset < size) {
        ssize_t count = write(fd, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        offset += (size_t) count;
    }
    return 0;
}

static int ld_elf_atomic_output(ld_elf_backend_t *backend,
                                const uint8_t *image, size_t image_size) {
    const char *output = backend->ctx->options->output_path;
    size_t output_length = strlen(output);
    if (output_length > SIZE_MAX - 64U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF output path is too long");
    }
    char *temporary = malloc(output_length + 64U);
    if (!temporary) {
        return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                           "out of memory creating ELF temporary path");
    }
    int fd = -1;
    for (unsigned attempt = 0; attempt < 100U; attempt++) {
        snprintf(temporary, output_length + 64U, "%s.tmp.%ld.%u",
                 output, (long) getpid(), attempt);
        fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) {
        int saved = errno;
        free(temporary);
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "cannot create ELF temporary output for '%s': %s",
                           output, strerror(saved));
    }
    int status = LD_OK;
    if (ld_elf_write_all(fd, image, image_size) != 0 || fsync(fd) != 0 ||
        fchmod(fd, 0755) != 0) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "cannot write ELF output '%s': %s",
                             output, strerror(errno));
    }
    if (close(fd) != 0 && status == LD_OK) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "cannot close ELF output '%s': %s",
                             output, strerror(errno));
    }
    if (status == LD_OK && rename(temporary, output) != 0) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "cannot replace ELF output '%s': %s",
                             output, strerror(errno));
    }
    if (status != LD_OK) unlink(temporary);
    free(temporary);
    return status;
}

static uint16_t ld_elf_machine_for_arch(ld_arch_t arch) {
    switch (arch) {
        case LD_ARCH_ARM64:
            return LD_ELF_EM_AARCH64;
        case LD_ARCH_AMD64:
            return LD_ELF_EM_X86_64;
        case LD_ARCH_RISCV64:
            return LD_ELF_EM_RISCV;
        default:
            return 0U;
    }
}

static bool ld_elf_requires_executable_stack(
        const ld_elf_backend_t *backend) {
    /*
     * GNU assemblers communicate stack-execution requirements through the
     * flags of a zero-sized, non-allocated .note.GNU-stack section.  Such a
     * section is intentionally absent from the output section layout, but
     * its SHF_EXECINSTR bit still controls PT_GNU_STACK.  Only selected
     * objects participate, so an unextracted archive member cannot make the
     * final process stack executable.
     *
     * Zig 738d2be9 skips input note sections and always emits RW here.  This
     * AArch64/x86-64/riscv64 behavior follows the GNU ELF convention because
     * silently emitting RW for a selected executable-stack object can create
     * a binary which links successfully and then faults at runtime.
     */
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        const ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->section_count; j++) {
            const ld_elf_section_t *section = &object->sections[j];
            if (strcmp(section->name, ".note.GNU-stack") == 0 &&
                (section->header.sh_flags & LD_ELF_SHF_EXECINSTR) != 0U) {
                return true;
            }
        }
    }
    return false;
}

static int ld_elf_symtab_entry_push(ld_elf_backend_t *backend,
                                    ld_elf_symtab_entry_list_t *list,
                                    ld_elf_symtab_entry_t entry) {
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2U) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "too many ELF output symbols");
        }
        size_t next = list->capacity ? list->capacity * 2U : 32U;
        size_t bytes;
        if (ld_elf_mul_overflow(next, sizeof(*list->items), &bytes)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF output symbol allocation overflows");
        }
        void *items = realloc(list->items, bytes);
        if (!items) {
            return ld_elf_fail(backend->ctx, LD_IO_ERROR,
                               "out of memory collecting ELF output symbols");
        }
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = entry;
    return LD_OK;
}

static bool ld_elf_symbol_is_output_live(const ld_elf_symbol_t *symbol) {
    if (!symbol || symbol->entry.st_shndx == LD_ELF_SHN_UNDEF) return false;
    if (symbol->entry.st_shndx == LD_ELF_SHN_ABS ||
        symbol->entry.st_shndx == LD_ELF_SHN_COMMON) {
        return true;
    }
    if (!symbol->section || !symbol->section->output ||
        symbol->section->group_discarded || symbol->section->discarded) {
        return false;
    }
    if (symbol->section->merge_input) {
        bool alive = true;
        return ld_elf_merge_map_input(symbol->section,
                                      symbol->entry.st_value, 0U, NULL,
                                      &alive, NULL) &&
               alive;
    }
    if (ld_elf_riscv_relax_plan_active(
                &symbol->section->riscv_relax_plan)) {
        bool alive = true;
        return ld_elf_riscv_relax_map(
                       &symbol->section->riscv_relax_plan,
                       symbol->entry.st_value, 0U, NULL, &alive, NULL) &&
               alive;
    }
    if (!ld_elf_is_eh_frame_section(symbol->section)) return true;
    bool alive = true;
    return ld_elf_map_input_offset(symbol->section, symbol->entry.st_value,
                                   0U, NULL, &alive, NULL) &&
           alive;
}

static int ld_elf_symtab_entry_from_symbol(
        ld_elf_backend_t *backend, ld_elf_object_t *object,
        ld_elf_symbol_t *symbol, ld_elf_symtab_entry_t *entry) {
    uint64_t value = 0U;
    int status = ld_elf_symbol_value(backend, object, symbol, &value);
    if (status != LD_OK) return status;

    ld_elf_output_section_t *output = NULL;
    uint16_t section_index;
    if (symbol->entry.st_shndx == LD_ELF_SHN_ABS) {
        section_index = LD_ELF_SHN_ABS;
    } else if (symbol->entry.st_shndx == LD_ELF_SHN_COMMON) {
        output = symbol->type == LD_ELF_STT_TLS ? backend->tbss
                                                : backend->bss;
        if (!output) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF common symbol '%s' in '%s' has no output section",
                    symbol->name, object->display_name);
        }
        section_index = (uint16_t) output->index;
    } else {
        output = symbol->section ? symbol->section->output : NULL;
        if (!output) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF symbol '%s' in '%s' has no output section",
                    symbol->name, object->display_name);
        }
        section_index = (uint16_t) output->index;
    }
    if (output && (output->flags & LD_ELF_SHF_TLS) != 0U) {
        if (value < backend->tls_addr) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF TLS symbol '%s' in '%s' precedes the TLS block",
                    symbol->name, object->display_name);
        }
        value -= backend->tls_addr;
    }
    *entry = (ld_elf_symtab_entry_t) {
            .name = symbol->name,
            .binding = symbol->binding,
            .type = symbol->type,
            .other = symbol->entry.st_other,
            .section_index = section_index,
            .value = value,
            .size = ld_elf_symbol_output_size(symbol),
    };
    return LD_OK;
}

static ld_elf_output_section_t *ld_elf_synthetic_output_section(
        ld_elf_backend_t *backend, const ld_elf_global_t *global) {
    switch (global->synthetic) {
        case LD_ELF_SYNTHETIC_DYNAMIC:
            return backend->dynamic;
        case LD_ELF_SYNTHETIC_INIT_ARRAY_START:
        case LD_ELF_SYNTHETIC_INIT_ARRAY_END:
            return backend->init_array;
        case LD_ELF_SYNTHETIC_FINI_ARRAY_START:
        case LD_ELF_SYNTHETIC_FINI_ARRAY_END:
            return backend->fini_array;
        case LD_ELF_SYNTHETIC_PREINIT_ARRAY_START:
        case LD_ELF_SYNTHETIC_PREINIT_ARRAY_END:
            return backend->preinit_array;
        case LD_ELF_SYNTHETIC_GOT:
            return backend->got;
        case LD_ELF_SYNTHETIC_BSS_START:
            return backend->bss;
        case LD_ELF_SYNTHETIC_END:
            return backend->bss
                           ? backend->bss
                           : (backend->data ? backend->data : backend->got);
        case LD_ELF_SYNTHETIC_TEXT_END:
            return backend->eh_frame
                           ? backend->eh_frame
                           : (backend->rodata ? backend->rodata
                                              : backend->text);
        case LD_ELF_SYNTHETIC_DATA_END:
            return backend->data ? backend->data : backend->got;
        case LD_ELF_SYNTHETIC_EH_FRAME_START:
            return backend->eh_frame;
        case LD_ELF_SYNTHETIC_EH_FRAME_HDR:
            return backend->eh_frame_hdr;
        case LD_ELF_SYNTHETIC_RELA_IPLT_START:
        case LD_ELF_SYNTHETIC_RELA_IPLT_END:
            return backend->ctx->options->pie ? NULL : backend->rela_dyn;
        case LD_ELF_SYNTHETIC_GLOBAL_POINTER:
            return backend->data;
        case LD_ELF_SYNTHETIC_TLS_MODULE_BASE:
            return backend->tdata ? backend->tdata : backend->tbss;
        case LD_ELF_SYNTHETIC_SECTION_START:
        case LD_ELF_SYNTHETIC_SECTION_STOP:
            return global->synthetic_output;
        case LD_ELF_SYNTHETIC_IMAGE_START:
            /*
             * Zig's linker-defined __ehdr_start symbol is associated with
             * the first output section even though its value denotes the ELF
             * image header immediately before that section.  Keep the same
             * section ownership in the emitted symbol table.
             */
            return backend->outputs.count ? backend->outputs.items[0] : NULL;
        case LD_ELF_SYNTHETIC_NONE:
        case LD_ELF_SYNTHETIC_ZERO:
        case LD_ELF_SYNTHETIC_DSO_HANDLE:
            return NULL;
    }
    return NULL;
}

static ld_elf_symbol_t *ld_elf_find_weak_undefined(
        ld_elf_backend_t *backend, const char *name) {
    for (size_t i = 0; i < backend->ctx->objects.count; i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = object->first_global_symbol;
             j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->binding == LD_ELF_STB_WEAK &&
                symbol->entry.st_shndx == LD_ELF_SHN_UNDEF &&
                strcmp(symbol->name, name) == 0) {
                return symbol;
            }
        }
    }
    return NULL;
}

static int ld_elf_compare_global_names(const void *left, const void *right) {
    const ld_elf_global_t *a = *(ld_elf_global_t *const *) left;
    const ld_elf_global_t *b = *(ld_elf_global_t *const *) right;
    return strcmp(a->name, b->name);
}

static int ld_elf_symtab_entry_from_global(
        ld_elf_backend_t *backend, ld_elf_global_t *global,
        ld_elf_symtab_entry_t *entry, bool *emit) {
    *emit = true;
    if (global->definition) {
        if (!ld_elf_symbol_is_output_live(global->definition)) {
            *emit = false;
            return LD_OK;
        }
        return ld_elf_symtab_entry_from_symbol(
                backend, global->object, global->definition, entry);
    }
    if (global->common) {
        ld_elf_output_section_t *output =
                global->common_tls ? backend->tbss : backend->bss;
        if (!output) {
            return ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF common symbol '%s' has no output section",
                    global->name);
        }
        uint64_t value;
        int status = ld_elf_global_value(backend, global, &value);
        if (status != LD_OK) return status;
        if (global->common_tls) {
            if (value < backend->tls_addr) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "ELF TLS common symbol '%s' precedes the TLS block",
                        global->name);
            }
            value -= backend->tls_addr;
        }
        *entry = (ld_elf_symtab_entry_t) {
                .name = global->name,
                .binding = global->common_reference ? LD_ELF_STB_GLOBAL
                                                    : LD_ELF_STB_WEAK,
                .type = global->common_tls ? LD_ELF_STT_TLS
                                           : LD_ELF_STT_OBJECT,
                .other = LD_ELF_STV_DEFAULT,
                .section_index = (uint16_t) output->index,
                .value = value,
                .size = global->common_size,
        };
        return LD_OK;
    }
    if (global->synthetic != LD_ELF_SYNTHETIC_NONE) {
        uint64_t value;
        int status = ld_elf_global_value(backend, global, &value);
        if (status != LD_OK) return status;
        ld_elf_output_section_t *output =
                ld_elf_synthetic_output_section(backend, global);
        uint16_t section_index = output ? (uint16_t) output->index
                                        : LD_ELF_SHN_UNDEF;
        if (output && (output->flags & LD_ELF_SHF_TLS) != 0U) {
            if (value < backend->tls_addr) {
                return ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "synthetic ELF TLS symbol '%s' precedes the TLS block",
                        global->name);
            }
            value -= backend->tls_addr;
        }
        *entry = (ld_elf_symtab_entry_t) {
                .name = global->name,
                /* Zig's linker-defined fallback participates in resolution
                 * as a weak definition, but Symbol.setOutputSym() emits it
                 * as local unless it was explicitly exported. */
                .binding = LD_ELF_STB_LOCAL,
                .type = LD_ELF_STT_NOTYPE,
                .other = LD_ELF_STV_HIDDEN,
                .section_index = section_index,
                .value = value,
                .size = 0U,
        };
        return LD_OK;
    }

    ld_elf_symbol_t *weak =
            ld_elf_find_weak_undefined(backend, global->name);
    if (!weak) {
        *emit = false;
        return LD_OK;
    }
    *entry = (ld_elf_symtab_entry_t) {
            .name = global->name,
            .binding = LD_ELF_STB_WEAK,
            .type = weak->type,
            .other = weak->entry.st_other,
            .section_index = LD_ELF_SHN_UNDEF,
            .value = 0U,
            .size = weak->entry.st_size,
    };
    return LD_OK;
}

static int ld_elf_build_output_symtab(ld_elf_backend_t *backend,
                                      uint16_t symtab_index,
                                      uint16_t strtab_index,
                                      uint16_t shstrtab_index,
                                      ld_elf_symtab_t *table) {
    ld_elf_symtab_entry_list_t locals = {0};
    ld_elf_symtab_entry_list_t globals = {0};
    char **thunk_symbol_names = NULL;
    size_t thunk_symbol_name_count = 0U;
    char **pltgot_symbol_names = NULL;
    size_t pltgot_symbol_name_count = 0U;
    int status = LD_OK;

    for (size_t i = 0; i < backend->outputs.count && status == LD_OK; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        status = ld_elf_symtab_entry_push(
                backend, &locals,
                (ld_elf_symtab_entry_t) {
                        .name = "",
                        .binding = LD_ELF_STB_LOCAL,
                        .type = LD_ELF_STT_SECTION,
                        .other = LD_ELF_STV_DEFAULT,
                        .section_index = (uint16_t) output->index,
                        .value = output->addr,
                        .size = 0U,
                });
    }
    const uint16_t metadata_indices[] = {
            symtab_index,
            strtab_index,
            shstrtab_index,
    };
    for (size_t i = 0;
         i < sizeof(metadata_indices) / sizeof(metadata_indices[0]) &&
         status == LD_OK;
         i++) {
        status = ld_elf_symtab_entry_push(
                backend, &locals,
                (ld_elf_symtab_entry_t) {
                        .name = "",
                        .binding = LD_ELF_STB_LOCAL,
                        .type = LD_ELF_STT_SECTION,
                        .other = LD_ELF_STV_DEFAULT,
                        .section_index = metadata_indices[i],
                        .value = 0U,
                        .size = 0U,
                });
    }

    for (size_t i = 0; i < backend->ctx->objects.count && status == LD_OK;
         i++) {
        ld_elf_object_t *object = backend->ctx->objects.items[i];
        if (!object->selected) continue;
        for (size_t j = 1; j < object->symbol_count; j++) {
            ld_elf_symbol_t *symbol = &object->symbols[j];
            if (symbol->binding != LD_ELF_STB_LOCAL ||
                symbol->type == LD_ELF_STT_SECTION ||
                !ld_elf_symbol_is_output_live(symbol)) {
                continue;
            }
            ld_elf_symtab_entry_t entry;
            status = ld_elf_symtab_entry_from_symbol(
                    backend, object, symbol, &entry);
            if (status != LD_OK) break;
            status = ld_elf_symtab_entry_push(backend, &locals, entry);
            if (status != LD_OK) break;
        }
    }

    /*
     * Match Zig Elf/Thunk.zig: every emitted AArch64 range-extension thunk
     * is represented by a local STT_FUNC named "<target>$thunk".  Besides
     * making synthetic code visible in nm, the function symbol is required
     * by AArch64-aware disassemblers to classify the bytes as instructions;
     * without it GNU objdump prints the valid ADRP/ADD/BR sequence as .word.
     */
    size_t thunk_symbol_count = 0U;
    if (status == LD_OK &&
        backend->ctx->options->arch == LD_ARCH_ARM64 && backend->text) {
        for (size_t i = 0; i < backend->aarch64_thunk_plan.group_count; i++) {
            ld_elf_aarch64_thunk_group_t *group =
                    &backend->aarch64_thunk_plan.groups[i];
            for (size_t j = 0; j < group->entry_count; j++) {
                if (!group->entries[j].used) continue;
                if (thunk_symbol_count == SIZE_MAX) {
                    status = ld_elf_fail(
                            backend->ctx, LD_OUTPUT_ERROR,
                            "too many AArch64 thunk symbols");
                    break;
                }
                thunk_symbol_count++;
            }
            if (status != LD_OK) break;
        }
    }
    if (status == LD_OK && thunk_symbol_count) {
        size_t bytes;
        if (ld_elf_mul_overflow(thunk_symbol_count,
                                sizeof(*thunk_symbol_names), &bytes)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "AArch64 thunk symbol name allocation overflows");
        } else {
            thunk_symbol_names = calloc(1, bytes);
            if (!thunk_symbol_names) {
                status = ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "out of memory collecting AArch64 thunk symbols");
            }
        }
    }
    for (size_t i = 0;
         i < backend->aarch64_thunk_plan.group_count && status == LD_OK; i++) {
        ld_elf_aarch64_thunk_group_t *group =
                &backend->aarch64_thunk_plan.groups[i];
        for (size_t j = 0; j < group->entry_count; j++) {
            ld_elf_aarch64_thunk_entry_t *entry = &group->entries[j];
            if (!entry->used) continue;
            uint64_t target_address;
            const char *target_name;
            status = ld_elf_aarch64_thunk_target(
                    backend, entry, &target_address, &target_name);
            if (status != LD_OK) break;
            (void) target_address;
            size_t target_name_size = strlen(target_name);
            if (target_name_size > SIZE_MAX - sizeof("$thunk")) {
                status = ld_elf_fail(
                        backend->ctx, LD_OUTPUT_ERROR,
                        "AArch64 thunk symbol name is too long");
                break;
            }
            char *thunk_name =
                    malloc(target_name_size + sizeof("$thunk"));
            if (!thunk_name) {
                status = ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "out of memory naming AArch64 thunk symbol");
                break;
            }
            memcpy(thunk_name, target_name, target_name_size);
            memcpy(thunk_name + target_name_size, "$thunk",
                   sizeof("$thunk"));
            thunk_symbol_names[thunk_symbol_name_count++] = thunk_name;

            uint64_t thunk_address;
            status = ld_elf_aarch64_thunk_address(
                    backend, (uint32_t) i, (uint32_t) j, &thunk_address);
            if (status != LD_OK) break;
            status = ld_elf_symtab_entry_push(
                    backend, &locals,
                    (ld_elf_symtab_entry_t) {
                            .name = thunk_name,
                            .binding = LD_ELF_STB_LOCAL,
                            .type = LD_ELF_STT_FUNC,
                            .other = LD_ELF_STV_DEFAULT,
                            .section_index = (uint16_t) backend->text->index,
                            .value = thunk_address,
                            .size = LD_ELF_AARCH64_THUNK_SIZE,
                    });
            if (status != LD_OK) break;
        }
    }

    /*
     * Zig Elf/PltGotSection writes one local STT_FUNC named
     * "<target>$pltgot" for each non-lazy IFUNC trampoline. Keep the entries
     * in PLT-GOT index order so the output symbol table remains stable even
     * though global resolution uses a hash table.
     */
    if (status == LD_OK && backend->pltgot_count != 0U) {
        size_t bytes;
        if (ld_elf_mul_overflow(backend->pltgot_count,
                                sizeof(*pltgot_symbol_names), &bytes)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF PLT-GOT symbol name allocation overflows");
        } else {
            pltgot_symbol_names = calloc(1, bytes);
            if (!pltgot_symbol_names) {
                status = ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "out of memory collecting ELF PLT-GOT symbols");
            }
        }
    }
    for (uint32_t i = 0;
         i < backend->pltgot_count && status == LD_OK; i++) {
        ld_elf_ifunc_reference_t reference;
        status = ld_elf_ifunc_reference_for_index(backend, i, &reference);
        if (status != LD_OK) break;
        size_t name_size = strlen(reference.name);
        if (name_size > SIZE_MAX - sizeof("$pltgot")) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "ELF IFUNC PLT-GOT symbol name is too long");
            break;
        }
        char *name = malloc(name_size + sizeof("$pltgot"));
        if (!name) {
            status = ld_elf_fail(
                    backend->ctx, LD_IO_ERROR,
                    "out of memory naming ELF IFUNC PLT-GOT symbol");
            break;
        }
        memcpy(name, reference.name, name_size);
        memcpy(name + name_size, "$pltgot", sizeof("$pltgot"));
        pltgot_symbol_names[pltgot_symbol_name_count++] = name;
        uint64_t value;
        status = ld_elf_pltgot_address(backend, i, reference.name, &value);
        if (status != LD_OK) break;
        status = ld_elf_symtab_entry_push(
                backend, &locals,
                (ld_elf_symtab_entry_t) {
                        .name = name,
                        .binding = LD_ELF_STB_LOCAL,
                        .type = LD_ELF_STT_FUNC,
                        .other = LD_ELF_STV_DEFAULT,
                        .section_index =
                                (uint16_t) backend->plt_got->index,
                        .value = value,
                        .size = LD_ELF_IFUNC_PLTGOT_ENTRY_SIZE,
                });
    }

    size_t global_count = 0U;
    ld_elf_global_t *global, *temporary;
    HASH_ITER(hh, backend->globals, global, temporary) {
        if (global_count == SIZE_MAX) {
            status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                 "too many resolved ELF global symbols");
            break;
        }
        global_count++;
    }
    ld_elf_global_t **ordered_globals = NULL;
    if (status == LD_OK && global_count) {
        size_t bytes;
        if (ld_elf_mul_overflow(global_count, sizeof(*ordered_globals),
                                &bytes)) {
            status = ld_elf_fail(
                    backend->ctx, LD_OUTPUT_ERROR,
                    "resolved ELF global symbol allocation overflows");
        } else {
            ordered_globals = malloc(bytes);
            if (!ordered_globals) {
                status = ld_elf_fail(
                        backend->ctx, LD_IO_ERROR,
                        "out of memory sorting resolved ELF global symbols");
            }
        }
    }
    if (status == LD_OK && global_count) {
        size_t index = 0U;
        HASH_ITER(hh, backend->globals, global, temporary) {
            ordered_globals[index++] = global;
        }
        qsort(ordered_globals, global_count, sizeof(*ordered_globals),
              ld_elf_compare_global_names);
        for (size_t i = 0; i < global_count; i++) {
            ld_elf_symtab_entry_t entry;
            bool emit;
            status = ld_elf_symtab_entry_from_global(
                    backend, ordered_globals[i], &entry, &emit);
            if (status != LD_OK) break;
            if (!emit) continue;
            status = ld_elf_symtab_entry_push(
                    backend,
                    entry.binding == LD_ELF_STB_LOCAL ? &locals : &globals,
                    entry);
            if (status != LD_OK) break;
        }
    }

    if (status == LD_OK) {
        size_t error_entry_index = SIZE_MAX;
        ld_elf_symtab_result_t result = ld_elf_symtab_build(
                locals.items, locals.count, globals.items, globals.count,
                table, &error_entry_index);
        if (result != LD_ELF_SYMTAB_OK) {
            int code = result == LD_ELF_SYMTAB_OUT_OF_MEMORY
                               ? LD_IO_ERROR
                               : LD_OUTPUT_ERROR;
            const char *name = NULL;
            if (error_entry_index < locals.count) {
                name = locals.items[error_entry_index].name;
            } else if (error_entry_index != SIZE_MAX) {
                size_t global_index = error_entry_index - locals.count;
                if (error_entry_index >= locals.count &&
                    global_index < globals.count) {
                    name = globals.items[global_index].name;
                }
            }
            status = name && name[0]
                             ? ld_elf_fail(
                                       backend->ctx, code,
                                       "cannot encode ELF output symbol '%s': %s",
                                       name,
                                       ld_elf_symtab_result_string(result))
                             : ld_elf_fail(
                                       backend->ctx, code,
                                       "cannot build ELF output symbol table: %s",
                                       ld_elf_symtab_result_string(result));
        }
    }

    free(ordered_globals);
    for (size_t i = 0; i < thunk_symbol_name_count; i++)
        free(thunk_symbol_names[i]);
    free(thunk_symbol_names);
    for (size_t i = 0; i < pltgot_symbol_name_count; i++)
        free(pltgot_symbol_names[i]);
    free(pltgot_symbol_names);
    free(locals.items);
    free(globals.items);
    return status;
}

static int ld_elf_emit_image(ld_elf_backend_t *backend, uint64_t entry) {
    uint64_t file_end;
    if (ld_elf_add_overflow(backend->rw_file_offset,
                            backend->rw_file_size, &file_end)) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "ELF loaded file size overflows address space");
    }
    if (file_end < backend->rx_file_size) file_end = backend->rx_file_size;
    if (file_end < backend->rw_file_offset) file_end = backend->rw_file_offset;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        const ld_elf_output_section_t *output = backend->outputs.items[i];
        uint64_t output_end;
        if (ld_elf_add_overflow(output->file_offset, output->file_size,
                                &output_end)) {
            return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                               "ELF output section '%s' file range "
                               "overflows",
                               output->name);
        }
        if (output_end > file_end) file_end = output_end;
    }

    if (backend->outputs.count > (size_t) UINT32_MAX - 1U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many ELF output sections for 32-bit section "
                           "indices");
    }
    if (backend->outputs.count > (size_t) UINT16_MAX - 4U) {
        return ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                           "too many ELF output sections for the ELF64 "
                           "header");
    }
    size_t section_count_size = backend->outputs.count + 4U;
    if (section_count_size >= LD_ELF_SHN_LORESERVE) {
        return ld_elf_fail(backend->ctx, LD_UNSUPPORTED,
                           "ELF extended section numbering is not supported");
    }
    uint16_t section_count = (uint16_t) section_count_size;
    uint16_t symtab_index = (uint16_t) (backend->outputs.count + 1U);
    uint16_t strtab_index = (uint16_t) (backend->outputs.count + 2U);
    uint16_t shstrtab_index = (uint16_t) (backend->outputs.count + 3U);

    ld_elf_symtab_t symtab;
    ld_elf_symtab_init(&symtab);
    int status = ld_elf_build_output_symtab(
            backend, symtab_index, strtab_index, shstrtab_index, &symtab);
    if (status != LD_OK) {
        ld_elf_symtab_deinit(&symtab);
        return status;
    }
    uint8_t *image = NULL;

    uint64_t shstrtab_size = 1U;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        uint64_t name_size = (uint64_t) strlen(backend->outputs.items[i]->name) +
                             1U;
        if (name_size > UINT32_MAX || shstrtab_size > UINT32_MAX - name_size) {
            status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                 "ELF section name string table exceeds the "
                                 "32-bit sh_name limit");
            goto cleanup;
        }
        shstrtab_size += name_size;
    }
    uint32_t symtab_name = (uint32_t) shstrtab_size;
    if (shstrtab_size > UINT32_MAX - sizeof(".symtab")) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF section name string table exceeds the "
                             "32-bit sh_name limit");
        goto cleanup;
    }
    shstrtab_size += sizeof(".symtab");
    uint32_t strtab_name = (uint32_t) shstrtab_size;
    if (shstrtab_size > UINT32_MAX - sizeof(".strtab")) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF section name string table exceeds the "
                             "32-bit sh_name limit");
        goto cleanup;
    }
    shstrtab_size += sizeof(".strtab");
    uint32_t shstrtab_name = (uint32_t) shstrtab_size;
    if (shstrtab_size > UINT32_MAX - sizeof(".shstrtab")) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF section name string table exceeds the "
                             "32-bit sh_name limit");
        goto cleanup;
    }
    shstrtab_size += sizeof(".shstrtab");

    /*
     * Like Elf.initShStrtab/writeSymtab/writeShdrTable in Zig commit
     * 738d2be9, the non-allocated symbol/string tables and the aligned
     * section-header table live outside every PT_LOAD. Keeping them after
     * file_end means adding link metadata never changes a load segment's
     * file or memory size.
     */
    uint64_t symtab_offset;
    if (!ld_elf_align(file_end, 8U, &symtab_offset)) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF symbol table layout overflow");
        goto cleanup;
    }
    uint64_t strtab_offset;
    if (ld_elf_add_overflow(symtab_offset, (uint64_t) symtab.symbols_size,
                            &strtab_offset)) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF symbol table layout overflow");
        goto cleanup;
    }
    uint64_t shstrtab_offset;
    if (ld_elf_add_overflow(strtab_offset, (uint64_t) symtab.strings_size,
                            &shstrtab_offset)) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF string table layout overflow");
        goto cleanup;
    }
    uint64_t after_shstrtab;
    uint64_t shdr_offset;
    if (ld_elf_add_overflow(shstrtab_offset, shstrtab_size,
                            &after_shstrtab) ||
        !ld_elf_align(after_shstrtab, 8U, &shdr_offset)) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF section metadata layout overflow");
        goto cleanup;
    }
    if ((uint64_t) section_count > UINT64_MAX / LD_ELF64_SHDR_SIZE) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF section header table size overflow");
        goto cleanup;
    }
    uint64_t shdr_size =
            (uint64_t) section_count * LD_ELF64_SHDR_SIZE;
    uint64_t image_end;
    if (ld_elf_add_overflow(shdr_offset, shdr_size, &image_end) ||
        image_end > SIZE_MAX) {
        status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                             "ELF output exceeds host address space");
        goto cleanup;
    }
    size_t image_size = (size_t) image_end;
    image = calloc(1, image_size);
    if (!image) {
        status = ld_elf_fail(backend->ctx, LD_IO_ERROR,
                             "out of memory creating ELF output image");
        goto cleanup;
    }
    ld_elf_write_header(image,
                        backend->ctx->options->pie ? LD_ELF_ET_DYN
                                                   : LD_ELF_ET_EXEC,
                        ld_elf_machine_for_arch(backend->ctx->options->arch),
                        entry, backend->elf_flags, backend->phnum,
                        shdr_offset, section_count, shstrtab_index);
    uint8_t *phdr = image + LD_ELF64_EHDR_SIZE;
    uint64_t phdr_size = (uint64_t) backend->phnum * LD_ELF64_PHDR_SIZE;
    ld_elf_write_phdr(phdr, LD_ELF_PT_PHDR, LD_ELF_PF_R,
                      LD_ELF64_EHDR_SIZE,
                      backend->image_base + LD_ELF64_EHDR_SIZE,
                      phdr_size, phdr_size, 8U);
    phdr += LD_ELF64_PHDR_SIZE;
    ld_elf_write_phdr(phdr, LD_ELF_PT_LOAD,
                      LD_ELF_PF_R | LD_ELF_PF_X, 0U,
                      backend->image_base, backend->rx_file_size,
                      backend->rx_file_size, backend->page_size);
    phdr += LD_ELF64_PHDR_SIZE;
    ld_elf_write_phdr(phdr, LD_ELF_PT_LOAD,
                      LD_ELF_PF_R | LD_ELF_PF_W,
                      backend->rw_file_offset, backend->rw_addr,
                      backend->rw_file_size, backend->rw_mem_size,
                      backend->page_size);
    phdr += LD_ELF64_PHDR_SIZE;
    if (backend->tdata || backend->tbss) {
        ld_elf_write_phdr(phdr, LD_ELF_PT_TLS, LD_ELF_PF_R,
                          backend->tls_file_offset, backend->tls_addr,
                          backend->tls_file_size, backend->tls_mem_size,
                          backend->tls_align);
        phdr += LD_ELF64_PHDR_SIZE;
    }
    if (backend->dynamic) {
        ld_elf_write_phdr(
                phdr, LD_ELF_PT_DYNAMIC, LD_ELF_PF_R | LD_ELF_PF_W,
                backend->dynamic->file_offset, backend->dynamic->addr,
                backend->dynamic->file_size, backend->dynamic->size,
                backend->dynamic->align);
        phdr += LD_ELF64_PHDR_SIZE;
    }
    if (backend->eh_frame_hdr) {
        ld_elf_write_phdr(
                phdr, LD_ELF_PT_GNU_EH_FRAME, LD_ELF_PF_R,
                backend->eh_frame_hdr->file_offset,
                backend->eh_frame_hdr->addr,
                backend->eh_frame_hdr->file_size,
                backend->eh_frame_hdr->size, backend->eh_frame_hdr->align);
        phdr += LD_ELF64_PHDR_SIZE;
    }
    if (backend->gnu_property) {
        ld_elf_write_phdr(
                phdr, LD_ELF_PT_NOTE, LD_ELF_PF_R,
                backend->gnu_property->file_offset,
                backend->gnu_property->addr,
                backend->gnu_property->file_size,
                backend->gnu_property->size, backend->gnu_property->align);
        phdr += LD_ELF64_PHDR_SIZE;
        ld_elf_write_phdr(
                phdr, LD_ELF_PT_GNU_PROPERTY, LD_ELF_PF_R,
                backend->gnu_property->file_offset,
                backend->gnu_property->addr,
                backend->gnu_property->file_size,
                backend->gnu_property->size, backend->gnu_property->align);
        phdr += LD_ELF64_PHDR_SIZE;
    }
    uint32_t stack_flags = LD_ELF_PF_R | LD_ELF_PF_W;
    if (ld_elf_requires_executable_stack(backend))
        stack_flags |= LD_ELF_PF_X;
    ld_elf_write_phdr(phdr, LD_ELF_PT_GNU_STACK, stack_flags,
                      0U, 0U, 0U, 0U, 16U);
    phdr += LD_ELF64_PHDR_SIZE;
    if (backend->relro_plan.present) {
        ld_elf_write_phdr(
                phdr, LD_ELF_PT_GNU_RELRO, LD_ELF_PF_R,
                backend->relro_plan.file_offset,
                backend->relro_plan.address,
                backend->relro_plan.file_size,
                backend->relro_plan.memory_size, 1U);
    }

    for (size_t i = 0; i < backend->outputs.count; i++) {
        ld_elf_output_section_t *output = backend->outputs.items[i];
        if (output->file_size == 0U) continue;
        if (output->file_offset > file_end ||
            output->file_size > file_end - output->file_offset) {
            status = ld_elf_fail(backend->ctx, LD_OUTPUT_ERROR,
                                 "output section '%s' exceeds ELF file",
                                 output->name);
            goto cleanup;
        }
        memcpy(image + output->file_offset, output->data,
               (size_t) output->file_size);
    }

    memcpy(image + (size_t) symtab_offset, symtab.symbols,
           symtab.symbols_size);
    memcpy(image + (size_t) strtab_offset, symtab.strings,
           symtab.strings_size);

    uint8_t *section_names = image + (size_t) shstrtab_offset;
    uint64_t section_name_cursor = 1U;
    section_names[0] = 0U;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        const char *name = backend->outputs.items[i]->name;
        size_t name_size = strlen(name) + 1U;
        memcpy(section_names + (size_t) section_name_cursor, name,
               name_size);
        section_name_cursor += name_size;
    }
    memcpy(section_names + (size_t) section_name_cursor, ".symtab",
           sizeof(".symtab"));
    section_name_cursor += sizeof(".symtab");
    memcpy(section_names + (size_t) section_name_cursor, ".strtab",
           sizeof(".strtab"));
    section_name_cursor += sizeof(".strtab");
    memcpy(section_names + (size_t) section_name_cursor, ".shstrtab",
           sizeof(".shstrtab"));

    uint8_t *section_headers = image + (size_t) shdr_offset;
    ld_elf_write_shdr(section_headers, 0U, LD_ELF_SHT_NULL, 0U, 0U, 0U,
                      0U, 0U, 0U, 0U, 0U);
    section_name_cursor = 1U;
    for (size_t i = 0; i < backend->outputs.count; i++) {
        const ld_elf_output_section_t *output = backend->outputs.items[i];
        uint32_t name = (uint32_t) section_name_cursor;
        uint32_t link = output->header_link
                                ? output->header_link->index
                                : ((output->flags & LD_ELF_SHF_LINK_ORDER) !=
                                                           0U &&
                                                   output->link_order_target
                                           ? output->link_order_target->index
                                           : 0U);
        ld_elf_write_shdr(
                section_headers + (i + 1U) * LD_ELF64_SHDR_SIZE, name,
                output->type, output->flags, output->addr,
                output->file_offset, output->size, link,
                output->header_info, output->align,
                ld_elf_output_entry_size(output));
        section_name_cursor += strlen(output->name) + 1U;
    }
    ld_elf_write_shdr(
            section_headers +
                    (size_t) symtab_index * LD_ELF64_SHDR_SIZE,
            symtab_name, LD_ELF_SHT_SYMTAB, 0U, 0U, symtab_offset,
            symtab.symbols_size, strtab_index, symtab.first_global, 8U,
            LD_ELF64_SYM_SIZE);
    ld_elf_write_shdr(
            section_headers +
                    (size_t) strtab_index * LD_ELF64_SHDR_SIZE,
            strtab_name, LD_ELF_SHT_STRTAB, 0U, 0U, strtab_offset,
            symtab.strings_size, 0U, 0U, 1U, 0U);
    ld_elf_write_shdr(
            section_headers +
                    (size_t) shstrtab_index * LD_ELF64_SHDR_SIZE,
            shstrtab_name, LD_ELF_SHT_STRTAB, 0U, 0U, shstrtab_offset,
            shstrtab_size, 0U, 0U, 1U, 0U);

    status = ld_elf_atomic_output(backend, image, image_size);

cleanup:
    free(image);
    ld_elf_symtab_deinit(&symtab);
    return status;
}

static bool ld_elf_regular_file(const char *path) {
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static char *ld_elf_library_path(const char *directory, const char *library) {
    size_t directory_length = strlen(directory);
    size_t library_length = strlen(library);
    bool separator = directory_length != 0U &&
                     directory[directory_length - 1U] != '/';
    size_t extra = sizeof("lib.a") - 1U + (separator ? 1U : 0U);
    if (directory_length > SIZE_MAX - library_length - extra - 1U) return NULL;
    size_t size = directory_length + library_length + extra + 1U;
    char *path = malloc(size);
    if (!path) return NULL;
    snprintf(path, size, "%s%slib%s.a", directory,
             separator ? "/" : "", library);
    return path;
}

static int ld_elf_try_library_directory(ld_elf_context_t *ctx,
                                        const char *directory,
                                        const char *library, bool *found) {
    char *path = ld_elf_library_path(directory, library);
    if (!path) {
        return ld_elf_fail(ctx, LD_IO_ERROR,
                           "out of memory resolving ELF library '-l%s'",
                           library);
    }
    int status = LD_OK;
    if (ld_elf_regular_file(path)) {
        *found = true;
        status = ld_elf_load_input(ctx, path);
    }
    free(path);
    return status;
}

static int ld_elf_load_libraries(ld_elf_context_t *ctx) {
    if (ctx->options->frameworks.count != 0U ||
        ctx->options->framework_paths.count != 0U) {
        return ld_elf_fail(ctx, LD_UNSUPPORTED,
                           "framework options are not valid for the ELF linker");
    }
    for (size_t i = 0; i < ctx->options->libraries.count; i++) {
        const char *library = ctx->options->libraries.items[i];
        bool found = false;
        for (size_t j = 0; j < ctx->options->library_paths.count && !found; j++) {
            int status = ld_elf_try_library_directory(
                    ctx, ctx->options->library_paths.items[j], library, &found);
            if (status != LD_OK) return status;
        }
        if (!found && ctx->options->sysroot && *ctx->options->sysroot) {
            const char *suffixes[] = {"/usr/lib", "/lib"};
            for (size_t j = 0; j < sizeof(suffixes) / sizeof(suffixes[0]) &&
                               !found;
                 j++) {
                size_t root_length = strlen(ctx->options->sysroot);
                size_t suffix_length = strlen(suffixes[j]);
                if (root_length > SIZE_MAX - suffix_length - 1U) {
                    return ld_elf_fail(ctx, LD_IO_ERROR,
                                       "ELF sysroot path is too long");
                }
                char *directory = malloc(root_length + suffix_length + 1U);
                if (!directory) {
                    return ld_elf_fail(ctx, LD_IO_ERROR,
                                       "out of memory resolving ELF library '-l%s'",
                                       library);
                }
                memcpy(directory, ctx->options->sysroot, root_length);
                memcpy(directory + root_length, suffixes[j],
                       suffix_length + 1U);
                int status = ld_elf_try_library_directory(
                        ctx, directory, library, &found);
                free(directory);
                if (status != LD_OK) return status;
            }
        }
        if (!found) {
            return ld_elf_fail(ctx, LD_IO_ERROR,
                               "cannot find static ELF library '-l%s'",
                               library);
        }
    }
    return LD_OK;
}

static void ld_elf_backend_deinit(ld_elf_backend_t *backend) {
    if (!backend) return;
    ld_elf_aarch64_thunk_plan_deinit(&backend->aarch64_thunk_plan);
    ld_elf_merge_plan_deinit(&backend->merge_plan);
    ld_elf_property_plan_deinit(&backend->property_plan);
    ld_elf_globals_clear(backend);
    for (size_t i = 0; i < backend->outputs.count; i++) {
        free(backend->outputs.items[i]->data);
        free(backend->outputs.items[i]->name);
        free(backend->outputs.items[i]);
    }
    free(backend->outputs.items);
    free(backend->placements.items);
}

static int ld_elf_link_loaded(ld_elf_context_t *ctx) {
    ld_elf_backend_t backend;
    memset(&backend, 0, sizeof(backend));
    ld_elf_aarch64_thunk_plan_init(&backend.aarch64_thunk_plan);
    backend.ctx = ctx;
    ld_elf_merge_plan_init(&backend.merge_plan, ctx);
    ld_elf_property_plan_init(&backend.property_plan);
    ld_elf_relro_plan_init(&backend.relro_plan);
    backend.image_base = ctx->options->pie ? 0U : LD_ELF_IMAGE_BASE;
    backend.page_size = ctx->options->arch == LD_ARCH_ARM64
                                ? LD_ELF_PAGE_SIZE_AARCH64
                                : LD_ELF_PAGE_SIZE_X86_64;
    ctx->backend_state = &backend;

    int status = ld_elf_extract_archives(&backend);
    if (status == LD_OK) status = ld_elf_merge_object_flags(&backend);
    if (status == LD_OK)
        status = ld_elf_prepare_riscv_relaxations(&backend);
    if (status == LD_OK) status = ld_elf_prepare_gnu_property(&backend);
    if (status == LD_OK) status = ld_elf_place_input_sections(&backend);
    if (status == LD_OK) status = ld_elf_init_start_stop_symbols(&backend);
    if (status == LD_OK) status = ld_elf_check_undefined(&backend);
    if (status == LD_OK) status = ld_elf_prepare_eh_frame_hdr(&backend);
    if (status == LD_OK) status = ld_elf_allocate_common(&backend);
    if (status == LD_OK) status = ld_elf_scan_got(&backend);
    if (status == LD_OK) {
        status = ctx->options->arch == LD_ARCH_ARM64
                         ? ld_elf_plan_aarch64_thunks(&backend)
                         : ld_elf_layout_outputs(&backend);
    }
    if (status == LD_OK)
        status = ld_elf_finalize_x86_gotpcrelx(&backend);
    if (status == LD_OK) status = ld_elf_allocate_output_data(&backend);
    if (status == LD_OK) status = ld_elf_emit_aarch64_thunks(&backend);
    if (status == LD_OK) status = ld_elf_fill_got(&backend);
    if (status == LD_OK) status = ld_elf_apply_relocations(&backend);
    if (status == LD_OK)
        status = ld_elf_emit_pie_relative_relocations(&backend);
    if (status == LD_OK) status = ld_elf_emit_ifunc(&backend);
    if (status == LD_OK) status = ld_elf_emit_static_pie_metadata(&backend);
    if (status == LD_OK) status = ld_elf_build_eh_frame_hdr(&backend);

    uint64_t entry = 0U;
    if (status == LD_OK) {
        const char *entry_name = ctx->options->entry_symbol &&
                                                 *ctx->options->entry_symbol
                                         ? ctx->options->entry_symbol
                                         : "_start";
        ld_elf_global_t *entry_symbol = ld_elf_global_get(
                &backend, entry_name, false);
        if (!entry_symbol || (!entry_symbol->definition &&
                              !entry_symbol->common &&
                              entry_symbol->synthetic == LD_ELF_SYNTHETIC_NONE)) {
            status = ld_elf_fail(ctx, LD_SYMBOL_ERROR,
                                 "ELF entry symbol '%s' is undefined",
                                 entry_name);
        } else {
            status = ld_elf_global_value(&backend, entry_symbol, &entry);
            if (status == LD_OK && entry == 0U) {
                status = ld_elf_fail(ctx, LD_SYMBOL_ERROR,
                                     "ELF entry symbol '%s' resolves to zero",
                                     entry_name);
            }
        }
    }
    if (status == LD_OK) status = ld_elf_emit_image(&backend, entry);
    ctx->backend_state = NULL;
    ld_elf_backend_deinit(&backend);
    return status;
}

int ld_link_elf(const ld_options_t *options) {
    if (!options || options->os != LD_OS_LINUX) return LD_INVALID_ARGUMENT;
    if (options->arch != LD_ARCH_ARM64 && options->arch != LD_ARCH_AMD64 &&
        options->arch != LD_ARCH_RISCV64) {
        if (options->diagnostic) {
            options->diagnostic(options->diagnostic_context, LD_DIAG_ERROR,
                                "the ELF linker currently supports Linux "
                                "x86_64, aarch64, and riscv64 outputs");
        }
        return LD_UNSUPPORTED;
    }
    ld_elf_context_t ctx;
    ld_elf_context_init(&ctx, options);
    int status = ld_elf_load_options_inputs(&ctx);
    if (status == LD_OK) status = ld_elf_load_libraries(&ctx);
    if (status == LD_OK) status = ld_elf_link_loaded(&ctx);
    ld_elf_context_deinit(&ctx);
    return status;
}
