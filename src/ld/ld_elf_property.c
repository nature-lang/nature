#include "ld_elf_property.h"

#include "elf_format.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    LD_ELF_PROPERTY_MERGE_AND = 0,
    LD_ELF_PROPERTY_MERGE_OR,
    LD_ELF_PROPERTY_MERGE_MAX,
    LD_ELF_PROPERTY_MERGE_PRESENT,
} ld_elf_property_merge_t;

struct ld_elf_property_entry {
    uint32_t type;
    uint32_t data_size;
    uint64_t value;
    ld_elf_property_merge_t merge;
};

typedef struct {
    ld_elf_property_entry_t *items;
    size_t count;
    size_t capacity;
} ld_elf_property_entry_list_t;

static uint32_t ld_elf_property_read_u32(const uint8_t *bytes) {
    return (uint32_t) bytes[0] | ((uint32_t) bytes[1] << 8U) |
           ((uint32_t) bytes[2] << 16U) |
           ((uint32_t) bytes[3] << 24U);
}

static uint64_t ld_elf_property_read_u64(const uint8_t *bytes) {
    return (uint64_t) ld_elf_property_read_u32(bytes) |
           ((uint64_t) ld_elf_property_read_u32(bytes + 4U) << 32U);
}

static void ld_elf_property_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t) value;
    bytes[1] = (uint8_t) (value >> 8U);
    bytes[2] = (uint8_t) (value >> 16U);
    bytes[3] = (uint8_t) (value >> 24U);
}

static void ld_elf_property_write_u64(uint8_t *bytes, uint64_t value) {
    ld_elf_property_write_u32(bytes, (uint32_t) value);
    ld_elf_property_write_u32(bytes + 4U, (uint32_t) (value >> 32U));
}

static bool ld_elf_property_align(size_t value, size_t alignment,
                                  size_t *result) {
    if (alignment == 0U) return false;
    size_t mask = alignment - 1U;
    if ((alignment & mask) != 0U ||
        value > SIZE_MAX - mask) {
        return false;
    }
    *result = (value + mask) & ~mask;
    return true;
}

static bool ld_elf_property_zero_padding(const uint8_t *data, size_t begin,
                                         size_t end) {
    for (size_t i = begin; i < end; i++) {
        if (data[i] != 0U) return false;
    }
    return true;
}

static ld_elf_property_result_t ld_elf_property_push(
        ld_elf_property_entry_list_t *list,
        ld_elf_property_entry_t entry) {
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2U) {
            return LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
        }
        size_t next = list->capacity ? list->capacity * 2U : 4U;
        if (next > SIZE_MAX / sizeof(*list->items)) {
            return LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
        }
        void *items = realloc(list->items, next * sizeof(*list->items));
        if (!items) return LD_ELF_PROPERTY_OUT_OF_MEMORY;
        list->items = items;
        list->capacity = next;
    }
    list->items[list->count++] = entry;
    return LD_ELF_PROPERTY_OK;
}

static ld_elf_property_entry_t *ld_elf_property_find(
        ld_elf_property_entry_t *entries, size_t count, uint32_t type) {
    for (size_t i = 0; i < count; i++) {
        if (entries[i].type == type) return &entries[i];
    }
    return NULL;
}

static ld_elf_property_result_t ld_elf_property_semantics(
        ld_arch_t arch, uint32_t type, uint32_t data_size,
        ld_elf_property_merge_t *merge) {
    uint32_t expected_size;
    if (type == LD_ELF_GNU_PROPERTY_STACK_SIZE) {
        expected_size = 8U;
        *merge = LD_ELF_PROPERTY_MERGE_MAX;
    } else if (type == LD_ELF_GNU_PROPERTY_NO_COPY_ON_PROTECTED) {
        expected_size = 0U;
        *merge = LD_ELF_PROPERTY_MERGE_PRESENT;
    } else if (type >= LD_ELF_GNU_PROPERTY_UINT32_AND_LO &&
               type <= LD_ELF_GNU_PROPERTY_UINT32_AND_HI) {
        expected_size = 4U;
        *merge = LD_ELF_PROPERTY_MERGE_AND;
    } else if (type >= LD_ELF_GNU_PROPERTY_UINT32_OR_LO &&
               type <= LD_ELF_GNU_PROPERTY_UINT32_OR_HI) {
        expected_size = 4U;
        *merge = LD_ELF_PROPERTY_MERGE_OR;
    } else if (arch == LD_ARCH_ARM64 &&
               type == LD_ELF_GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
        expected_size = 4U;
        *merge = LD_ELF_PROPERTY_MERGE_AND;
    } else if (arch == LD_ARCH_AMD64 &&
               type == LD_ELF_GNU_PROPERTY_X86_FEATURE_1_AND) {
        expected_size = 4U;
        *merge = LD_ELF_PROPERTY_MERGE_AND;
    } else if (arch == LD_ARCH_AMD64 &&
               (type == LD_ELF_GNU_PROPERTY_X86_FEATURE_2_NEEDED ||
                type == LD_ELF_GNU_PROPERTY_X86_ISA_1_NEEDED ||
                type == LD_ELF_GNU_PROPERTY_X86_FEATURE_2_USED ||
                type == LD_ELF_GNU_PROPERTY_X86_ISA_1_USED)) {
        expected_size = 4U;
        *merge = LD_ELF_PROPERTY_MERGE_OR;
    } else {
        return LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY;
    }
    return data_size == expected_size ? LD_ELF_PROPERTY_OK
                                      : LD_ELF_PROPERTY_INVALID_PROPERTY;
}

static ld_elf_property_result_t ld_elf_property_parse_descriptor(
        ld_elf_property_plan_t *plan, ld_arch_t arch, const uint8_t *data,
        size_t descriptor_offset, size_t descriptor_size,
        ld_elf_property_entry_list_t *object_entries) {
    size_t cursor = 0U;
    ld_elf_property_entry_list_t descriptor_entries = {0};
    ld_elf_property_result_t result = LD_ELF_PROPERTY_OK;
    while (cursor < descriptor_size) {
        if (descriptor_size - cursor < 8U) {
            plan->error_offset = descriptor_offset + cursor;
            result = LD_ELF_PROPERTY_INVALID_PROPERTY;
            goto cleanup;
        }
        uint32_t type = ld_elf_property_read_u32(data + cursor);
        uint32_t data_size = ld_elf_property_read_u32(data + cursor + 4U);
        plan->error_property_type = type;
        if (ld_elf_property_find(descriptor_entries.items,
                                 descriptor_entries.count, type)) {
            plan->error_offset = descriptor_offset + cursor;
            result = LD_ELF_PROPERTY_DUPLICATE_PROPERTY;
            goto cleanup;
        }
        size_t value_offset = cursor + 8U;
        if ((size_t) data_size > descriptor_size - value_offset) {
            plan->error_offset = descriptor_offset + cursor;
            result = LD_ELF_PROPERTY_INVALID_PROPERTY;
            goto cleanup;
        }
        size_t value_end = value_offset + (size_t) data_size;
        size_t next;
        if (!ld_elf_property_align(value_end, 8U, &next) ||
            next > descriptor_size) {
            plan->error_offset = descriptor_offset + cursor;
            result = LD_ELF_PROPERTY_INVALID_PROPERTY;
            goto cleanup;
        }
        if (!ld_elf_property_zero_padding(data, value_end, next)) {
            plan->error_offset = descriptor_offset + value_end;
            result = LD_ELF_PROPERTY_INVALID_PROPERTY;
            goto cleanup;
        }
        ld_elf_property_merge_t merge;
        result = ld_elf_property_semantics(
                arch, type, data_size, &merge);
        if (result != LD_ELF_PROPERTY_OK) {
            plan->error_offset = descriptor_offset + cursor;
            goto cleanup;
        }
        uint64_t value = data_size == 8U
                                 ? ld_elf_property_read_u64(
                                           data + value_offset)
                                 : (data_size == 4U
                                            ? ld_elf_property_read_u32(
                                                      data + value_offset)
                                            : 1U);
        result = ld_elf_property_push(
                &descriptor_entries,
                (ld_elf_property_entry_t) {
                        .type = type,
                        .data_size = data_size,
                        .value = value,
                        .merge = merge,
                });
        if (result != LD_ELF_PROPERTY_OK) goto cleanup;
        cursor = next;
    }

    /*
     * GCC 13 emits valid x86 GNU property descriptors in semantic rather
     * than numeric type order (for example 0xc0010002 followed by
     * 0xc0010001). GNU ld accepts those inputs and canonicalizes its output,
     * so input order is not a validity condition here. A duplicate within
     * one descriptor remains malformed. Across independent notes in one
     * object, keep the first occurrence as before.
     */
    for (size_t i = 0; i < descriptor_entries.count; i++) {
        const ld_elf_property_entry_t *entry = &descriptor_entries.items[i];
        if (ld_elf_property_find(object_entries->items,
                                 object_entries->count, entry->type)) {
            continue;
        }
        result = ld_elf_property_push(object_entries, *entry);
        if (result != LD_ELF_PROPERTY_OK) goto cleanup;
    }

cleanup:
    free(descriptor_entries.items);
    return result;
}

static ld_elf_property_result_t ld_elf_property_parse_input(
        ld_elf_property_plan_t *plan, ld_arch_t arch,
        const ld_elf_property_input_t *input,
        ld_elf_property_entry_list_t *object_entries) {
    if (!input->data || input->size == 0U) {
        plan->error_offset = 0U;
        return LD_ELF_PROPERTY_TRUNCATED_NOTE;
    }
    size_t cursor = 0U;
    while (cursor < input->size) {
        if ((cursor & 7U) != 0U || input->size - cursor < 12U) {
            plan->error_offset = cursor;
            return LD_ELF_PROPERTY_TRUNCATED_NOTE;
        }
        const uint8_t *header = input->data + cursor;
        uint32_t name_size = ld_elf_property_read_u32(header);
        uint32_t descriptor_size = ld_elf_property_read_u32(header + 4U);
        uint32_t note_type = ld_elf_property_read_u32(header + 8U);
        if (name_size != 4U || note_type != LD_ELF_NT_GNU_PROPERTY_TYPE_0) {
            plan->error_offset = cursor;
            return LD_ELF_PROPERTY_INVALID_NOTE;
        }
        size_t name_offset = cursor + 12U;
        if ((size_t) name_size > input->size - name_offset) {
            plan->error_offset = cursor;
            return LD_ELF_PROPERTY_TRUNCATED_NOTE;
        }
        if (memcmp(input->data + name_offset, "GNU\0", 4U) != 0) {
            plan->error_offset = name_offset;
            return LD_ELF_PROPERTY_INVALID_OWNER;
        }
        size_t descriptor_offset;
        if (!ld_elf_property_align(name_offset + (size_t) name_size, 8U,
                                   &descriptor_offset) ||
            descriptor_offset > input->size ||
            (size_t) descriptor_size > input->size - descriptor_offset) {
            plan->error_offset = cursor;
            return LD_ELF_PROPERTY_TRUNCATED_NOTE;
        }
        if (!ld_elf_property_zero_padding(
                    input->data, name_offset + (size_t) name_size,
                    descriptor_offset)) {
            plan->error_offset = name_offset + (size_t) name_size;
            return LD_ELF_PROPERTY_INVALID_NOTE;
        }
        ld_elf_property_result_t result = ld_elf_property_parse_descriptor(
                plan, arch, input->data + descriptor_offset,
                descriptor_offset, descriptor_size, object_entries);
        if (result != LD_ELF_PROPERTY_OK) return result;
        size_t descriptor_end =
                descriptor_offset + (size_t) descriptor_size;
        size_t next;
        if (!ld_elf_property_align(descriptor_end, 8U, &next) ||
            next > input->size) {
            plan->error_offset = descriptor_end;
            return LD_ELF_PROPERTY_TRUNCATED_NOTE;
        }
        if (!ld_elf_property_zero_padding(input->data, descriptor_end,
                                          next)) {
            plan->error_offset = descriptor_end;
            return LD_ELF_PROPERTY_INVALID_NOTE;
        }
        cursor = next;
    }
    return LD_ELF_PROPERTY_OK;
}

void ld_elf_property_plan_init(ld_elf_property_plan_t *plan) {
    if (!plan) return;
    memset(plan, 0, sizeof(*plan));
    plan->error_input_index = SIZE_MAX;
    plan->error_property_type = UINT32_MAX;
}

void ld_elf_property_plan_deinit(ld_elf_property_plan_t *plan) {
    if (!plan) return;
    free(plan->entries);
    free(plan->output);
    memset(plan, 0, sizeof(*plan));
}

ld_elf_property_result_t ld_elf_property_add_object(
        ld_elf_property_plan_t *plan, ld_arch_t arch,
        const ld_elf_property_input_t *inputs, size_t input_count) {
    if (!plan || (input_count != 0U && !inputs) || plan->finalized) {
        return LD_ELF_PROPERTY_INVALID_ARGUMENT;
    }
    plan->error_input_index = SIZE_MAX;
    plan->error_offset = 0U;
    plan->error_property_type = UINT32_MAX;
    ld_elf_property_entry_list_t object_entries = {0};
    ld_elf_property_result_t result = LD_ELF_PROPERTY_OK;
    for (size_t i = 0; i < input_count; i++) {
        result = ld_elf_property_parse_input(plan, arch, &inputs[i],
                                             &object_entries);
        if (result != LD_ELF_PROPERTY_OK) {
            plan->error_input_index = i;
            goto cleanup;
        }
    }

    for (size_t i = 0; i < plan->count; i++) {
        ld_elf_property_entry_t *entry = &plan->entries[i];
        if (entry->merge == LD_ELF_PROPERTY_MERGE_AND &&
            !ld_elf_property_find(object_entries.items,
                                  object_entries.count, entry->type)) {
            entry->value = 0U;
        }
    }
    for (size_t i = 0; i < object_entries.count; i++) {
        ld_elf_property_entry_t *incoming = &object_entries.items[i];
        ld_elf_property_entry_t *entry = ld_elf_property_find(
                plan->entries, plan->count, incoming->type);
        if (!entry) {
            ld_elf_property_entry_t copy = *incoming;
            if (copy.merge == LD_ELF_PROPERTY_MERGE_AND &&
                plan->object_count != 0U) {
                copy.value = 0U;
            }
            ld_elf_property_entry_list_t plan_entries = {
                    .items = plan->entries,
                    .count = plan->count,
                    .capacity = plan->capacity,
            };
            result = ld_elf_property_push(&plan_entries, copy);
            plan->entries = plan_entries.items;
            plan->count = plan_entries.count;
            plan->capacity = plan_entries.capacity;
            if (result != LD_ELF_PROPERTY_OK) goto cleanup;
            continue;
        }
        switch (entry->merge) {
            case LD_ELF_PROPERTY_MERGE_AND:
                entry->value &= incoming->value;
                break;
            case LD_ELF_PROPERTY_MERGE_OR:
                entry->value |= incoming->value;
                break;
            case LD_ELF_PROPERTY_MERGE_MAX:
                if (incoming->value > entry->value)
                    entry->value = incoming->value;
                break;
            case LD_ELF_PROPERTY_MERGE_PRESENT:
                entry->value = 1U;
                break;
        }
    }
    if (plan->object_count == SIZE_MAX) {
        result = LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
        goto cleanup;
    }
    plan->object_count++;

cleanup:
    free(object_entries.items);
    return result;
}

static int ld_elf_property_compare_entries(const void *left,
                                           const void *right) {
    const ld_elf_property_entry_t *a = left;
    const ld_elf_property_entry_t *b = right;
    if (a->type == b->type) return 0;
    return a->type < b->type ? -1 : 1;
}

ld_elf_property_result_t ld_elf_property_finalize(
        ld_elf_property_plan_t *plan) {
    if (!plan || plan->finalized) return LD_ELF_PROPERTY_INVALID_ARGUMENT;
    size_t descriptor_size = 0U;
    size_t output_count = 0U;
    for (size_t i = 0; i < plan->count; i++) {
        const ld_elf_property_entry_t *entry = &plan->entries[i];
        if (entry->merge != LD_ELF_PROPERTY_MERGE_PRESENT &&
            entry->value == 0U) {
            continue;
        }
        size_t entry_size;
        if (!ld_elf_property_align(8U + entry->data_size, 8U,
                                   &entry_size) ||
            descriptor_size > SIZE_MAX - entry_size) {
            return LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
        }
        descriptor_size += entry_size;
        output_count++;
    }
    if (output_count == 0U) {
        plan->finalized = true;
        return LD_ELF_PROPERTY_OK;
    }
    if (descriptor_size > UINT32_MAX || descriptor_size > SIZE_MAX - 16U) {
        return LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
    }
    size_t output_size = 16U + descriptor_size;
    uint8_t *output = calloc(1, output_size);
    if (!output) return LD_ELF_PROPERTY_OUT_OF_MEMORY;
    qsort(plan->entries, plan->count, sizeof(*plan->entries),
          ld_elf_property_compare_entries);
    ld_elf_property_write_u32(output, 4U);
    ld_elf_property_write_u32(output + 4U, (uint32_t) descriptor_size);
    ld_elf_property_write_u32(output + 8U,
                              LD_ELF_NT_GNU_PROPERTY_TYPE_0);
    memcpy(output + 12U, "GNU\0", 4U);
    size_t cursor = 16U;
    for (size_t i = 0; i < plan->count; i++) {
        const ld_elf_property_entry_t *entry = &plan->entries[i];
        if (entry->merge != LD_ELF_PROPERTY_MERGE_PRESENT &&
            entry->value == 0U) {
            continue;
        }
        ld_elf_property_write_u32(output + cursor, entry->type);
        ld_elf_property_write_u32(output + cursor + 4U,
                                  entry->data_size);
        if (entry->data_size == 4U) {
            ld_elf_property_write_u32(output + cursor + 8U,
                                      (uint32_t) entry->value);
        } else if (entry->data_size == 8U) {
            ld_elf_property_write_u64(output + cursor + 8U,
                                      entry->value);
        }
        if (!ld_elf_property_align(cursor + 8U + entry->data_size, 8U,
                                   &cursor)) {
            free(output);
            return LD_ELF_PROPERTY_OUTPUT_OVERFLOW;
        }
    }
    plan->output = output;
    plan->output_size = output_size;
    plan->finalized = true;
    return LD_ELF_PROPERTY_OK;
}

const char *ld_elf_property_result_string(
        ld_elf_property_result_t result) {
    switch (result) {
        case LD_ELF_PROPERTY_OK:
            return "ok";
        case LD_ELF_PROPERTY_INVALID_ARGUMENT:
            return "invalid argument";
        case LD_ELF_PROPERTY_OUT_OF_MEMORY:
            return "out of memory";
        case LD_ELF_PROPERTY_TRUNCATED_NOTE:
            return "truncated GNU property note";
        case LD_ELF_PROPERTY_INVALID_NOTE:
            return "invalid GNU property note header or padding";
        case LD_ELF_PROPERTY_INVALID_OWNER:
            return "GNU property note has a non-GNU owner";
        case LD_ELF_PROPERTY_INVALID_PROPERTY:
            return "invalid GNU property entry";
        case LD_ELF_PROPERTY_UNSORTED_PROPERTY:
            return "GNU property entries are not sorted by type";
        case LD_ELF_PROPERTY_DUPLICATE_PROPERTY:
            return "duplicate GNU property type";
        case LD_ELF_PROPERTY_UNSUPPORTED_PROPERTY:
            return "unsupported GNU property type";
        case LD_ELF_PROPERTY_OUTPUT_OVERFLOW:
            return "GNU property output overflows host limits";
        default:
            return "unknown GNU property error";
    }
}
