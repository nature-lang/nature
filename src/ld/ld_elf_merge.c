#include "ld_elf_merge.h"

#include "utils/uthash.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * This is a C translation of the batch-linking parts of Elf/Merge.zig and
 * Object.initInputMergeSections/resolveMergeSubsections from Zig commit
 * 738d2be9d6b6ef3ff3559130c05159ef53336224.  Like that implementation,
 * merge sections containing relocations stay on the ordinary input-section
 * path.  Zig allocator, incremental, GC, and Atom state are not copied.
 */

typedef struct ld_elf_merge_fragment {
    const uint8_t *bytes;
    uint64_t size;
    uint64_t align;
    uint64_t output_offset;
    UT_hash_handle hh;
} ld_elf_merge_fragment_t;

typedef struct {
    uint64_t input_offset;
    uint64_t size;
    ld_elf_merge_fragment_t *fragment;
} ld_elf_merge_mapping_t;

struct ld_elf_merge_group {
    ld_elf_output_section_t *output;
    uint32_t type;
    uint64_t flags;
    uint64_t entry_size;
    uint64_t base_offset;
    uint64_t size;
    uint64_t align;
    ld_elf_merge_fragment_t *fragments;
    size_t fragment_count;
    ld_elf_merge_group_t *next;
};

struct ld_elf_merge_input {
    ld_elf_section_t *section;
    ld_elf_merge_group_t *group;
    ld_elf_merge_mapping_t *mappings;
    size_t mapping_count;
    size_t mapping_capacity;
    ld_elf_merge_input_t *next;
};

static bool merge_add_overflow(uint64_t left, uint64_t right,
                               uint64_t *result) {
    if (left > UINT64_MAX - right) return true;
    *result = left + right;
    return false;
}

static bool merge_align(uint64_t value, uint64_t alignment,
                        uint64_t *result) {
    if (alignment == 0U) alignment = 1U;
    uint64_t mask = alignment - 1U;
    if ((alignment & mask) != 0U || value > UINT64_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

void ld_elf_merge_plan_init(ld_elf_merge_plan_t *plan,
                            ld_elf_context_t *ctx) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
    plan->ctx = ctx;
}

void ld_elf_merge_plan_deinit(ld_elf_merge_plan_t *plan) {
    if (!plan) return;
    ld_elf_merge_input_t *input = plan->inputs;
    while (input) {
        ld_elf_merge_input_t *next = input->next;
        if (input->section) input->section->merge_input = NULL;
        free(input->mappings);
        free(input);
        input = next;
    }
    ld_elf_merge_group_t *group = plan->first_group;
    while (group) {
        ld_elf_merge_group_t *next = group->next;
        ld_elf_merge_fragment_t *fragment, *temporary;
        HASH_ITER(hh, group->fragments, fragment, temporary) {
            HASH_DEL(group->fragments, fragment);
            free(fragment);
        }
        free(group);
        group = next;
    }
    memset(plan, 0, sizeof(*plan));
}

bool ld_elf_merge_section_eligible(const ld_elf_section_t *section) {
    if (!section ||
        (section->header.sh_flags & LD_ELF_SHF_MERGE) == 0U ||
        section->header.sh_type != LD_ELF_SHT_PROGBITS ||
        section->relocation_count != 0U || section->nobits) {
        return false;
    }
    return (section->header.sh_flags & LD_ELF_SHF_STRINGS) != 0U ||
           section->header.sh_entsize != 0U;
}

static ld_elf_merge_group_t *merge_find_group(
        ld_elf_merge_plan_t *plan, const ld_elf_section_t *section,
        ld_elf_output_section_t *output) {
    for (ld_elf_merge_group_t *group = plan->first_group; group;
         group = group->next) {
        if (group->output == output && group->type == section->header.sh_type &&
            group->flags == section->header.sh_flags) {
            return group;
        }
    }
    ld_elf_merge_group_t *group = calloc(1, sizeof(*group));
    if (!group) return NULL;
    group->output = output;
    group->type = section->header.sh_type;
    group->flags = section->header.sh_flags;
    group->align = 1U;
    if (plan->last_group) {
        plan->last_group->next = group;
    } else {
        plan->first_group = group;
    }
    plan->last_group = group;
    return group;
}

static int merge_mapping_push(ld_elf_merge_plan_t *plan,
                              ld_elf_merge_input_t *input,
                              uint64_t input_offset, uint64_t size,
                              ld_elf_merge_fragment_t *fragment) {
    if (input->mapping_count == input->mapping_capacity) {
        if (input->mapping_capacity > SIZE_MAX / 2U) {
            return ld_elf_fail(plan->ctx, LD_OUTPUT_ERROR,
                               "too many ELF merge-section entities");
        }
        size_t next = input->mapping_capacity
                              ? input->mapping_capacity * 2U
                              : 16U;
        if (next > SIZE_MAX / sizeof(*input->mappings)) {
            return ld_elf_fail(plan->ctx, LD_OUTPUT_ERROR,
                               "ELF merge-section mapping overflows");
        }
        void *items = realloc(input->mappings,
                              next * sizeof(*input->mappings));
        if (!items) {
            return ld_elf_fail(plan->ctx, LD_IO_ERROR,
                               "out of memory mapping ELF merge section '%s'",
                               input->section->name);
        }
        input->mappings = items;
        input->mapping_capacity = next;
    }
    input->mappings[input->mapping_count++] = (ld_elf_merge_mapping_t) {
            .input_offset = input_offset,
            .size = size,
            .fragment = fragment,
    };
    return LD_OK;
}

static int merge_add_entity(ld_elf_merge_plan_t *plan,
                            ld_elf_merge_input_t *input,
                            uint64_t input_offset, uint64_t size) {
    if (size > UINT_MAX) {
        return ld_elf_fail(
                plan->ctx, LD_UNSUPPORTED,
                "ELF merge entity at offset 0x%llx in section '%s' is too "
                "large",
                (unsigned long long) input_offset, input->section->name);
    }
    const uint8_t *bytes = input->section->data + (size_t) input_offset;
    ld_elf_merge_fragment_t *fragment = NULL;
    HASH_FIND(hh, input->group->fragments, bytes, (unsigned) size, fragment);
    if (!fragment) {
        fragment = calloc(1, sizeof(*fragment));
        if (!fragment) {
            return ld_elf_fail(plan->ctx, LD_IO_ERROR,
                               "out of memory deduplicating ELF merge section "
                               "'%s'",
                               input->section->name);
        }
        fragment->bytes = bytes;
        fragment->size = size;
        fragment->align = input->section->header.sh_addralign
                                  ? input->section->header.sh_addralign
                                  : 1U;
        HASH_ADD_KEYPTR(hh, input->group->fragments, fragment->bytes,
                        (unsigned) fragment->size, fragment);
        if (input->group->fragment_count == SIZE_MAX) {
            HASH_DEL(input->group->fragments, fragment);
            free(fragment);
            return ld_elf_fail(plan->ctx, LD_OUTPUT_ERROR,
                               "too many ELF merge-section fragments");
        }
        input->group->fragment_count++;
    } else if (input->section->header.sh_addralign > fragment->align) {
        fragment->align = input->section->header.sh_addralign;
    }
    return merge_mapping_push(plan, input, input_offset, size, fragment);
}

static bool merge_null_unit(const uint8_t *bytes, uint64_t size) {
    for (uint64_t i = 0U; i < size; i++) {
        if (bytes[i] != 0U) return false;
    }
    return true;
}

int ld_elf_merge_add_section(ld_elf_merge_plan_t *plan,
                             ld_elf_section_t *section,
                             ld_elf_output_section_t *output) {
    if (!plan || !plan->ctx || !section || !output ||
        !ld_elf_merge_section_eligible(section)) {
        return LD_INVALID_ARGUMENT;
    }
    if (!section->data || section->data_size != section->header.sh_size) {
        return ld_elf_fail(plan->ctx, LD_INVALID_INPUT,
                           "ELF merge section '%s' has invalid contents",
                           section->name);
    }

    uint64_t entity_size = section->header.sh_entsize;
    bool strings =
            (section->header.sh_flags & LD_ELF_SHF_STRINGS) != 0U;
    if (strings && entity_size == 0U) entity_size = 1U;
    if (entity_size == 0U) return LD_UNSUPPORTED;
    if (section->header.sh_size % entity_size != 0U) {
        return ld_elf_fail(
                plan->ctx, LD_INVALID_INPUT,
                "ELF merge section '%s' size 0x%llx is not a multiple of "
                "sh_entsize 0x%llx",
                section->name,
                (unsigned long long) section->header.sh_size,
                (unsigned long long) entity_size);
    }

    ld_elf_merge_group_t *group = merge_find_group(plan, section, output);
    if (!group) {
        return ld_elf_fail(plan->ctx, LD_IO_ERROR,
                           "out of memory creating ELF merge section '%s'",
                           section->name);
    }
    if (group->entry_size == 0U || entity_size < group->entry_size)
        group->entry_size = entity_size;

    ld_elf_merge_input_t *input = calloc(1, sizeof(*input));
    if (!input) {
        return ld_elf_fail(plan->ctx, LD_IO_ERROR,
                           "out of memory recording ELF merge section '%s'",
                           section->name);
    }
    input->section = section;
    input->group = group;
    input->next = plan->inputs;
    plan->inputs = input;
    section->merge_input = input;

    uint64_t offset = 0U;
    while (offset < section->header.sh_size) {
        uint64_t size = entity_size;
        if (strings) {
            uint64_t cursor = offset;
            for (;;) {
                if (cursor > section->header.sh_size - entity_size) {
                    return ld_elf_fail(
                            plan->ctx, LD_INVALID_INPUT,
                            "string in ELF merge section '%s' is not NUL "
                            "terminated",
                            section->name);
                }
                const uint8_t *unit =
                        section->data + (size_t) cursor;
                cursor += entity_size;
                if (merge_null_unit(unit, entity_size)) break;
            }
            size = cursor - offset;
        }
        int status = merge_add_entity(plan, input, offset, size);
        if (status != LD_OK) return status;
        offset += size;
    }
    return LD_OK;
}

static int merge_fragment_compare(const void *left, const void *right) {
    const ld_elf_merge_fragment_t *a =
            *(ld_elf_merge_fragment_t *const *) left;
    const ld_elf_merge_fragment_t *b =
            *(ld_elf_merge_fragment_t *const *) right;
    if (a->align != b->align) return a->align < b->align ? -1 : 1;
    if (a->size != b->size) return a->size < b->size ? -1 : 1;
    int order = memcmp(a->bytes, b->bytes, (size_t) a->size);
    return order < 0 ? -1 : order > 0 ? 1
                                      : 0;
}

int ld_elf_merge_finalize(ld_elf_merge_plan_t *plan) {
    if (!plan || !plan->ctx) return LD_INVALID_ARGUMENT;
    for (ld_elf_merge_group_t *group = plan->first_group; group;
         group = group->next) {
        if (group->fragment_count > SIZE_MAX / sizeof(void *)) {
            return ld_elf_fail(plan->ctx, LD_OUTPUT_ERROR,
                               "ELF merge-section fragment list overflows");
        }
        ld_elf_merge_fragment_t **ordered =
                group->fragment_count
                        ? malloc(group->fragment_count * sizeof(*ordered))
                        : NULL;
        if (group->fragment_count && !ordered) {
            return ld_elf_fail(plan->ctx, LD_IO_ERROR,
                               "out of memory sorting ELF merge section '%s'",
                               group->output->name);
        }
        size_t count = 0U;
        ld_elf_merge_fragment_t *fragment, *temporary;
        HASH_ITER(hh, group->fragments, fragment, temporary) {
            ordered[count++] = fragment;
        }
        if (count > 1U)
            qsort(ordered, count, sizeof(*ordered), merge_fragment_compare);
        group->size = 0U;
        group->align = 1U;
        for (size_t i = 0U; i < count; i++) {
            fragment = ordered[i];
            uint64_t offset;
            if (!merge_align(group->size, fragment->align, &offset) ||
                merge_add_overflow(offset, fragment->size, &group->size)) {
                free(ordered);
                return ld_elf_fail(
                        plan->ctx, LD_OUTPUT_ERROR,
                        "ELF merge section '%s' layout overflows",
                        group->output->name);
            }
            fragment->output_offset = offset;
            if (fragment->align > group->align)
                group->align = fragment->align;
        }
        free(ordered);

        if (!merge_align(group->output->size, group->align,
                         &group->base_offset) ||
            merge_add_overflow(group->base_offset, group->size,
                               &group->output->size)) {
            return ld_elf_fail(plan->ctx, LD_OUTPUT_ERROR,
                               "ELF output merge section '%s' overflows",
                               group->output->name);
        }
        group->output->file_size = group->output->size;
        if (group->align > group->output->align)
            group->output->align = group->align;
        if ((group->output->flags & LD_ELF_SHF_MERGE) != 0U &&
            (group->output->entry_size == 0U ||
             group->entry_size < group->output->entry_size)) {
            group->output->entry_size = group->entry_size;
        }
    }
    for (ld_elf_merge_input_t *input = plan->inputs; input;
         input = input->next) {
        input->section->output_offset = input->group->base_offset;
    }
    return LD_OK;
}

int ld_elf_merge_emit(const ld_elf_merge_plan_t *plan) {
    if (!plan || !plan->ctx) return LD_INVALID_ARGUMENT;
    for (ld_elf_merge_group_t *group = plan->first_group; group;
         group = group->next) {
        ld_elf_output_section_t *output = group->output;
        ld_elf_merge_fragment_t *fragment, *temporary;
        HASH_ITER(hh, group->fragments, fragment, temporary) {
            uint64_t destination;
            if (merge_add_overflow(group->base_offset,
                                   fragment->output_offset, &destination) ||
                destination > output->file_size ||
                fragment->size > output->file_size - destination ||
                !output->data) {
                return ld_elf_fail(
                        plan->ctx, LD_OUTPUT_ERROR,
                        "ELF merge fragment does not fit output section '%s'",
                        output->name);
            }
            memcpy(output->data + (size_t) destination, fragment->bytes,
                   (size_t) fragment->size);
        }
    }
    return LD_OK;
}

bool ld_elf_merge_map_input(const ld_elf_section_t *section,
                            uint64_t input_offset, uint64_t width,
                            uint64_t *output_offset, bool *alive,
                            uint64_t *available) {
    if (!section || !section->merge_input) return false;
    const ld_elf_merge_input_t *input = section->merge_input;
    for (size_t i = 0U; i < input->mapping_count; i++) {
        const ld_elf_merge_mapping_t *mapping = &input->mappings[i];
        if (input_offset < mapping->input_offset ||
            input_offset - mapping->input_offset >= mapping->size) {
            continue;
        }
        uint64_t within = input_offset - mapping->input_offset;
        if (width > mapping->size - within) return false;
        if (output_offset)
            *output_offset = mapping->fragment->output_offset + within;
        if (alive) *alive = true;
        if (available) *available = mapping->size - within;
        return true;
    }
    return false;
}
